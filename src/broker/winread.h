// winread.h - the Windows broker's MIDI IN path as plain C (the reader of vticore_winusb.c, the client queue of
// vtibroker.c); no Windows calls here, the unit test calls it with fakes.
//
// Reader, why (R3-4, R12-4): the original keeps its IN read pending without a timeout and resubmits it in the
// completion (VirusUSB.sys 0xf10092e0). We had one synchronous read with a 200 ms pipe timeout, the next one asked only
// after the clients had the bytes, and a read that failed (the timeout) dropped what it got: a packet that came in the
// cancel window was lost, with it the whole SysEx when it was the F0. Now `slots` overlapped reads stay pending
// without a timeout, the oldest is waited for first (one pipe finishes its reads in submit order), and each is asked
// again as soon as its bytes went on, so the endpoint is never left unasked (E517 (5)); a failed read's bytes go on
// too. An error or the stop signal cancels the reads still out and drains them in order (their bytes go on), then the
// rules of vtireaderr.h decide as before: give up on a gone device, clear a stall and read again.
#pragma once
#include <stdint.h>
#include "vtireaderr.h"
#include "vtiproto.h"

#define VTI_RD_SLOTS 2 // reads pending per endpoint, the original's ping-pong
#define VTI_RD_STOP 0  // vti_rd_run: the stop signal came
#define VTI_RD_DEAD 1  // the device is gone or stalled beyond recovery: the broker closes and reopens it

typedef struct {
    void *ctx;
    int slots; // reads pending, 1..VTI_RD_SLOTS
    unsigned long (*submit)(void *ctx, int slot); // start a read: 0 (pending), or the error of a read that did not start
    // wait for a slot's read: 1 = finished (got = its bytes, err = 0 or its error), 0 = the stop signal came first
    // (only when `stoppable`; the read is still pending then)
    int (*finish)(void *ctx, int slot, int stoppable, unsigned long *got, unsigned long *err);
    void (*cancel)(void *ctx); // cancel every pending read of the pipe: they finish with an error
    void (*deliver)(void *ctx, int slot, unsigned long got);
    void (*error)(void *ctx, unsigned long err); // log it
    void (*reset)(void *ctx);                    // clear a stalled pipe's halt
    int (*nap)(void *ctx);                       // wait a little after an error: 1 = the stop signal came
} vti_rd_ops;

static inline int vti_rd_run(const vti_rd_ops *o)
{
    int fails = 0; // error-31 reads in a row (vti_read_error_dead)
    for (;;) {
        unsigned long serr[VTI_RD_SLOTS], e = 0;
        int pend[VTI_RD_SLOTS], s = 0, stop = 0;
        const int n = o->slots < 1 ? 1 : o->slots > VTI_RD_SLOTS ? VTI_RD_SLOTS : o->slots;
        for (int i = 0; i < n; i++)
            pend[i] = !(serr[i] = o->submit(o->ctx, i));
        for (;;) {
            unsigned long got = 0, err = serr[s];
            if (pend[s]) {
                if (!o->finish(o->ctx, s, 1, &got, &err)) {
                    stop = 1;
                    break;
                }
                pend[s] = 0;
            }
            if (got) // a failed read's bytes too (R3-4)
                o->deliver(o->ctx, s, got);
            if (err && err != 121 /* ERROR_SEM_TIMEOUT: nothing wrong, ask again */) {
                e = err;
                break;
            }
            if (!err)
                fails = 0;
            pend[s] = !(serr[s] = o->submit(o->ctx, s));
            s = (s + 1) % n;
        }
        o->cancel(o->ctx); // what is still out, oldest first; its bytes go on
        for (int k = 0; k < n; k++) {
            const int t = (s + k) % n;
            unsigned long got = 0, err = 0;
            if (pend[t] && o->finish(o->ctx, t, 0, &got, &err) && got)
                o->deliver(o->ctx, t, got);
        }
        if (stop)
            return VTI_RD_STOP;
        if (e == 31) // ERROR_GEN_FAILURE: a stalled pipe
            fails++;
        o->error(o->ctx, e);
        if (vti_read_error_dead(e, fails))
            return VTI_RD_DEAD;
        if (e == 31)
            o->reset(o->ctx);
        if (o->nap(o->ctx))
            return VTI_RD_STOP;
    }
}

// Client queue, why (R12-7): a client that stops reading (a frozen DAW) filled its 256 KB queue and was dropped, which
// cost a reconnect and a re-sync; the original drops the one transfer that does not fit (0xf1009c20). A device packet
// (VTIMSG_RX) is now dropped and counted instead; any other message still ends the client, since a missing reply would
// leave it waiting. An observer loses any message, as before. (Parking as on libusb would stall every client's MIDI.)
static inline int vti_queue_full_drops_client(int observer, unsigned type) { return !observer && type != VTIMSG_RX; }

// the 1st, 2nd, 4th, 8th ... dropped packet of a client is logged: a DAW frozen for minutes does not flood the log
static inline int vti_drop_logged(uint64_t n) { return n && !(n & (n - 1)); }
