// vticore_winusb.c - Windows backend of vticore.h on the inbox WinUSB driver (src/inf/vti_winusb.inf binds
// WinUSB to MI_03 and MI_04, each with its own device interface GUID). Packing helpers are shared with the
// libusb backend via vticore_midi.c.
#include "vticore.h"
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include "writeretry.h"
#include "winread.h" // the reader loop: overlapped reads without a timeout (R3-4), error rules of vtireaderr.h
#include "rxqueue.h" // vti_rx_len: one packet per read (R3-6)
#include "linkrule.h" // link at open, unlink at close like the original driver (E524)
#include "idlepace.h" // vti_drop_idle: the Snow's idle packets on bulk EP 0x85 (E387)
#include "winslot.h" // which Virus when several are connected (#45)
#include <stdio.h>
#include <stdlib.h>

static const GUID GUID_MI03 = {0xCF293C20, 0xF175, 0x42AB, {0xB2, 0xBE, 0xA0, 0x2F, 0xEC, 0x94, 0x09, 0x37}};
static const GUID GUID_MI04 = {0x5B45FAF8, 0x316B, 0x4B94, {0xAA, 0xBF, 0xFF, 0x44, 0x2A, 0x85, 0xC5, 0xE7}};
static const uint8_t HANDSHAKE[4] = {0x4e, 0x73, 0x52, 0x01}; // spec §3, sent raw on EP 0x05

typedef struct {
    HANDLE file;
    WINUSB_INTERFACE_HANDLE usb;
} iface;

struct vti {
    iface mi[2]; // [0] = MI_03, [1] = MI_04
    vti_rx_cb cb;
    void *user;
    volatile LONG dead;
    HANDLE stop_ev; // set at the close: the readers cancel their pending reads and end
    HANDLE rd[2];
    int linked; // the last link message sent (E524)
};

static int open_iface(const GUID *g, iface *out)
{
    wchar_t *path = winslot_path(g, vti_slot); // the interface of this broker's Virus (#45)
    if (!path)
        return -1;
    int ok = -1;
    out->file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (out->file != INVALID_HANDLE_VALUE && WinUsb_Initialize(out->file, &out->usb)) {
        ok = 0;
        // Writes must never block forever (a stuck device hung the DAW, E042): 1 s on both OUT pipes.
        ULONG t = 1000;
        WinUsb_SetPipePolicy(out->usb, VTI_EP_MI03_OUT, PIPE_TRANSFER_TIMEOUT, sizeof t, &t);
        WinUsb_SetPipePolicy(out->usb, VTI_EP_MI04_OUT, PIPE_TRANSFER_TIMEOUT, sizeof t, &t);
        // Clear halts left by aborted transfers of a previous client (reads failed with error 31).
        static const uint8_t eps[] = {VTI_EP_MI03_OUT, VTI_EP_MI03_IN, VTI_EP_MI04_OUT, VTI_EP_MI04_IN};
        for (size_t i = 0; i < sizeof eps; i++)
            WinUsb_ResetPipe(out->usb, eps[i]);
    }
    else
        vti_log("vti: open %ls failed: %lu\n", path, GetLastError());
    free(path);
    return ok;
}

static iface *iface_for(vti *v, uint8_t ep)
{
    return &v->mi[(ep & 0x0f) == 0x06];
}

typedef struct {
    vti *v;
    uint8_t ep;
} rd_arg;

// one endpoint's reads (winread.h): up to VTI_RD_SLOTS overlapped reads, each with its own event and buffer
typedef struct {
    vti *v;
    iface *f;
    uint8_t ep;
    ULONG len;     // the endpoint's wMaxPacketSize: a full packet ends the read at once (R3-6)
    int snow_idle; // the Snow's bulk EP 0x85 (E387)
    unsigned long idle;
    OVERLAPPED ov[VTI_RD_SLOTS];
    HANDLE ev[VTI_RD_SLOTS];
    uint8_t buf[VTI_RD_SLOTS][VTI_RX_MAX];
} rd_ctx;

static unsigned long rd_submit(void *p, int s)
{
    rd_ctx *c = p;
    memset(&c->ov[s], 0, sizeof c->ov[s]);
    c->ov[s].hEvent = c->ev[s];
    ResetEvent(c->ev[s]);
    if (WinUsb_ReadPipe(c->f->usb, c->ep, c->buf[s], c->len, NULL, &c->ov[s]))
        return 0;
    DWORD e = GetLastError();
    return e == ERROR_IO_PENDING ? 0 : e ? e : ERROR_GEN_FAILURE;
}

static int rd_finish(void *p, int s, int stoppable, unsigned long *got, unsigned long *err)
{
    rd_ctx *c = p;
    HANDLE h[2] = {c->ev[s], c->v->stop_ev}; // the read first: data that came wins over the stop
    if (stoppable && WaitForMultipleObjects(2, h, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
        return 0;
    ULONG n = 0;
    *err = WinUsb_GetOverlappedResult(c->f->usb, &c->ov[s], &n, TRUE) ? 0 : GetLastError();
    *got = n;
    return 1;
}

static void rd_cancel(void *p)
{
    rd_ctx *c = p;
    WinUsb_AbortPipe(c->f->usb, c->ep);
}

static void rd_deliver(void *p, int s, unsigned long got)
{
    rd_ctx *c = p;
    int kept = c->snow_idle ? vti_drop_idle(c->buf[s], (int)got) : (int)got;
    c->idle += (got - (unsigned long)kept) / 4;
    if (kept)
        c->v->cb(c->v->user, c->ep, c->buf[s], kept);
}

static void rd_error(void *p, unsigned long e)
{
    rd_ctx *c = p;
    vti_log("vti: read ep %02x: error %lu\n", c->ep, e);
}

static void rd_reset(void *p) // stalled pipe: clear the halt and keep reading
{
    rd_ctx *c = p;
    WinUsb_ResetPipe(c->f->usb, c->ep);
}

static int rd_nap(void *p)
{
    rd_ctx *c = p;
    return WaitForSingleObject(c->v->stop_ev, 10) == WAIT_OBJECT_0;
}

static DWORD WINAPI reader(LPVOID p)
{
    rd_arg a = *(rd_arg *)p;
    free(p);
    rd_ctx c = {.v = a.v, .f = iface_for(a.v, a.ep), .ep = a.ep, .len = VTI_RX_MAX};
    // no timeout: the reads stay pending until data, an error or the cancel at the stop (winread.h, R3-4)
    ULONG timeout = 0;
    WinUsb_SetPipePolicy(c.f->usb, a.ep, PIPE_TRANSFER_TIMEOUT, sizeof timeout, &timeout);
    // only the Snow's EP 0x85 is bulk, and it answers every read with FF FE 00 00 (E387): drop those here like the
    // libusb reader does, or ~2000 of them a second reach every client and push the E392 handshake out of pre-roll
    for (UCHAR i = 0;; i++) {
        WINUSB_PIPE_INFORMATION pi;
        if (!WinUsb_QueryPipe(c.f->usb, 0, i, &pi))
            break;
        if (pi.PipeId == a.ep) {
            c.len = (ULONG)vti_rx_len(pi.MaximumPacketSize);
            c.snow_idle = a.ep == VTI_EP_MI03_IN && pi.PipeType == UsbdPipeTypeBulk;
        }
    }
    int ok = 1;
    for (int s = 0; s < VTI_RD_SLOTS; s++)
        ok &= (c.ev[s] = CreateEventW(NULL, TRUE, FALSE, NULL)) != NULL;
    // the Snow's bulk EP 0x85 answers every read at once (E387): two pending there would double its idle reads,
    // which this path does not pace (#42, E547), so one; the interrupt endpoints ask once a frame (bInterval 1)
    vti_rd_ops o = {&c, c.snow_idle ? 1 : VTI_RD_SLOTS, rd_submit, rd_finish, rd_cancel, rd_deliver, rd_error, rd_reset, rd_nap};
    if (!ok)
        vti_log("vti: read ep %02x: no event (%lu)\n", a.ep, GetLastError());
    else if (vti_rd_run(&o) == VTI_RD_DEAD) // removed (also 433, vtireaderr.h), or stalled beyond recovery
        a.v->dead = 1;
    for (int s = 0; s < VTI_RD_SLOTS; s++)
        if (c.ev[s])
            CloseHandle(c.ev[s]);
    if (c.idle)
        vti_log("vti: %lu idle packets (FF FE 00 00) dropped on bulk EP 0x85\n", c.idle);
    return 0;
}

vti *vti_open(vti_rx_cb cb, void *user)
{
    vti *v = calloc(1, sizeof *v);
    v->cb = cb;
    v->user = user;
    v->stop_ev = CreateEventW(NULL, TRUE, FALSE, NULL); // manual reset: both readers see it
    if (!v->stop_ev || open_iface(&GUID_MI03, &v->mi[0]) || open_iface(&GUID_MI04, &v->mi[1])) {
        vti_log("vti: Virus TI WinUSB interfaces not found (is vti_winusb.inf installed?)\n");
        vti_close(v);
        return NULL;
    }
    // Readers first: if nobody drains EP 0x85/0x86, the device stops accepting OUT data, including the
    // handshake (E042).
    uint8_t eps[2] = {VTI_EP_MI03_IN, VTI_EP_MI04_IN};
    for (int i = 0; i < 2; i++) {
        rd_arg *a = malloc(sizeof *a);
        a->v = v;
        a->ep = eps[i];
        v->rd[i] = CreateThread(NULL, 0, reader, a, 0, NULL);
    }
    if (vti_write(v, VTI_EP_MI03_OUT, HANDSHAKE, sizeof HANDSHAKE)) {
        vti_close(v);
        return NULL;
    }
    v->linked = 1;
    return v;
}

void vti_close(vti *v)
{
    if (!v)
        return;
    if (v->linked && !v->dead) // the original's vtimidi.sys unlinks when its last stream closes (E524)
        vti_link(v, 0);
    if (v->stop_ev)
        SetEvent(v->stop_ev);
    for (int i = 0; i < 2; i++)
        if (v->rd[i]) {
            WaitForSingleObject(v->rd[i], 2000);
            CloseHandle(v->rd[i]);
        }
    for (int i = 0; i < 2; i++) {
        if (v->mi[i].usb)
            WinUsb_Free(v->mi[i].usb);
        if (v->mi[i].file && v->mi[i].file != INVALID_HANDLE_VALUE)
            CloseHandle(v->mi[i].file);
    }
    if (v->stop_ev)
        CloseHandle(v->stop_ev);
    free(v);
}

struct write_ctx {
    vti *v;
    iface *f;
    uint8_t ep;
    DWORD err; // last error that was not the pipe timeout
    vti_keep_fn keep;
    void *u;
};

static int write_keep(void *ctx) // a packet waits on a busy Virus: go on unless it is gone or the caller says stop
{
    struct write_ctx *c = ctx;
    return !c->v->dead && (!c->keep || c->keep(c->u));
}

static int write_packet(void *ctx, const uint8_t *data, int n, int *done)
{
    struct write_ctx *c = ctx;
    ULONG got = 0;
    // one wMaxPacketSize packet per call (R12-10): a whole message in one call left the resume point to how exactly
    // WinUSB counts a cancelled multi-packet transfer; one packet is either taken (got = n, also when the ACK raced
    // the 1 s pipe timeout's cancel) or not (0), so the retry repeats or skips nothing
    BOOL ok = WinUsb_WritePipe(c->f->usb, c->ep, (PUCHAR)data, (ULONG)n, &got, NULL);
    *done = (int)got;
    if (ok)
        return 0;
    DWORD e = GetLastError();
    if (e == ERROR_SEM_TIMEOUT)
        return VTI_XFER_TIMEOUT;
    c->err = e;
    return -1;
}

static vti_tap_cb g_tap;
void vti_set_tap(vti_tap_cb cb) { g_tap = cb; }

// a control request as the tap's record: setup[8], i32 result, then the data that went or came
static void tap_control(uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, int32_t r, const uint8_t *data)
{
    if (!g_tap)
        return;
    uint8_t rec[12] = {rt, req, (uint8_t)val, (uint8_t)(val >> 8), (uint8_t)idx, (uint8_t)(idx >> 8), (uint8_t)len, (uint8_t)(len >> 8)};
    memcpy(rec + 8, &r, 4);
    g_tap(2, 0, rec, 12, data, (rt & 0x80) ? (r > 0 ? r : 0) : len);
}

int vti_write_keep(vti *v, uint8_t ep, const uint8_t *data, int len, vti_keep_fn keep, void *u)
{
    if (g_tap) // before the transfer: the order on the wire
        g_tap(0, ep, data, len, NULL, 0);
    struct write_ctx c = {v, iface_for(v, ep), ep, 0, keep, u};
    int max = ep == VTI_EP_MI03_OUT ? 8 : 32, timeouts = 0; // wMaxPacketSize, as the libusb backend
    // a timed-out packet was NAKed, never taken (the link is lossless, E239): try it again while the Virus is busy
    int r = vti_write_chunks(&c, write_packet, data, len, max, VTI_WRITE_TRIES, &timeouts, write_keep);
    if (timeouts)
        vti_log("vti: write ep %02x: %d timeout(s), %s\n", ep, timeouts,
                !r ? "then accepted" : r == VTI_WRITE_STOPPED ? "stopped (device or client gone)" : "gave up");
    if (r == VTI_WRITE_STUCK) { // the existing failed-device path closes and reopens it (writeretry.h)
        vti_log("vti: write ep %02x: refused for %d s: treating the Virus as failed\n", ep, VTI_WRITE_TRIES);
        v->dead = 1;
    }
    if (r && c.err)
        vti_log("vti: write ep %02x: error %lu\n", ep, c.err);
    // a stalled OUT pipe refuses every later write until its halt is cleared (R3-5; the libusb backend does it, E503)
    if (r && c.err == ERROR_GEN_FAILURE)
        WinUsb_ResetPipe(c.f->usb, ep);
    return r;
}

int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len) { return vti_write_keep(v, ep, data, len, NULL, NULL); }

int vti_control(vti *v, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data,
                uint16_t len)
{
    WINUSB_SETUP_PACKET sp = {bmRequestType, bRequest, wValue, wIndex, len};
    ULONG done = 0;
    // Issued through the MI_04 handle; WinUSB may rewrite wIndex for interface-recipient requests (E040).
    if (!WinUsb_ControlTransfer(v->mi[1].usb, sp, data, len, &done, NULL)) {
        vti_log("vti: control %02x/%02x wIndex %04x: error %lu\n", bmRequestType, bRequest, wIndex,
                GetLastError());
        tap_control(bmRequestType, bRequest, wValue, wIndex, len, -1, data);
        return -1;
    }
    tap_control(bmRequestType, bRequest, wValue, wIndex, len, (int32_t)done, data);
    return (int)done;
}

int vti_audio(vti *v, int on, const char *event_name) // broker audio exists only behind vtibroker (vticore_pipe.c)
{
    (void)v;
    (void)on;
    (void)event_name;
    return -1;
}

int vti_is_dead(vti *v)
{
    return v->dead != 0;
}

int vti_link(vti *v, int on)
{
    if (!v || v->dead)
        return -1;
    uint8_t m[4];
    vti_link_msg(m, on);
    int r = vti_write(v, VTI_EP_MI03_OUT, m, sizeof m);
    if (!r)
        v->linked = on;
    vti_log("vti: link %s (4E 73 52 %02X) -> %d\n", on ? "on" : "off", m[3], r);
    return r;
}

int vti_linked(vti *v) { return v && v->linked; }
