/* writeretry.h - a device write in packets, with retries when a packet times out.
 *
 * Why: the Virus' USB chip NAKs an OUT packet while its 64-byte link ring is full, and that ring drains only as
 * fast as the host CPU acknowledges every byte over a toggle handshake (E239) - the link is lossless, so a timed-out
 * packet was never accepted and a timeout only means the Virus was busy for that long (a patch load, a multi
 * set-up). Dropping the rest of the message there (the old behaviour, E231, and after 5 s until T5) broke a SysEx
 * dump or lost a stamped event. The original never times out an OUT transfer and never cuts a message (VirusUSB.sys
 * 0xf10095e0/0xf1009740, R3-1): so a packet is retried for as long as the Virus is merely busy (it refused EP 0x06
 * for ~8.7 s on our TI2, E591 (3)), up to a bound well above that; a Virus that refuses one packet for that long
 * counts as failed (the caller reopens it). A real error stops at once, and so does keep() == 0 (client gone, device
 * gone, broker exiting), asked only after a timeout so the normal path costs nothing.
 *
 * xfer(ctx, data, n, &done) sends one packet of n bytes: returns 0 when accepted (done = n), VTI_XFER_TIMEOUT when
 * it timed out (done = bytes accepted before that, 0 for an interrupt packet), anything else on error.
 */
#pragma once
#include <stdint.h>

#define VTI_XFER_TIMEOUT 1
#define VTI_WRITE_TRIES 30   /* tries of one packet in a row (1 s each): 30 s, over 3x the longest refusal seen */
#define VTI_WRITE_ERROR -1   /* a real transfer error */
#define VTI_WRITE_STUCK -2   /* one packet refused `attempts` times in a row: treat the device as failed */
#define VTI_WRITE_STOPPED -3 /* keep(ctx) said stop */

typedef int (*vti_xfer_fn)(void *ctx, const uint8_t *data, int n, int *done);

/* Writes len bytes in packets of at most max bytes. A packet is retried after a timeout, up to `attempts` tries in
 * a row; `timeouts` (optional) counts the retried ones, `keep` (optional) is asked before each retry. Returns 0 when
 * everything was accepted, else a VTI_WRITE_* code. */
static inline int vti_write_chunks(void *ctx, vti_xfer_fn xfer, const uint8_t *data, int len, int max, int attempts,
                                   int *timeouts, int (*keep)(void *ctx))
{
    int tries = 0;
    for (int off = 0; off < len;) {
        int n = len - off < max ? len - off : max, done = 0;
        int r = xfer(ctx, data + off, n, &done);
        if (done > 0)
            off += done, tries = 0;
        if (r == 0) {
            if (done <= 0) /* accepted nothing without an error: never spin */
                return VTI_WRITE_ERROR;
            continue;
        }
        if (r != VTI_XFER_TIMEOUT)
            return VTI_WRITE_ERROR;
        if (timeouts)
            (*timeouts)++;
        if (++tries >= attempts)
            return VTI_WRITE_STUCK;
        if (keep && !keep(ctx))
            return VTI_WRITE_STOPPED;
    }
    return 0;
}
