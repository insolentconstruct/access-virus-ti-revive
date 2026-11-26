// sxhold.h - one client's SysEx open on the wire holds the other clients' writes (T6, research R12-3, R3-2).
// Why: the original lets one program at a time open the Virus' raw pipe, so nothing else ever lands inside its dump.
// The broker serves several clients, and the Virus' host CPU parses every cable but 5 with one byte parser (E252): a
// note or a request of another client inside a running cable-3 dump ends or corrupts it (E270). So while one client's
// SysEx is open (F0 sent, F7 not yet; vti_sxwatch of ctrlguard.h follows it per client), the others wait - bounded:
// until it ends, its client goes, 2 s pass without a byte from it (stale), or the waiter has waited 2 s. Its own
// writes are never held (the plug-in's events during its own dump go on cable 5, dumppacer.h). Plain C, no platform
// headers: both brokers use it; times are the broker's ms clock (wraps fine as uint32_t differences).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ctrlguard.h" // vti_sxwatch

#define VTI_SX_HOLD_MS 2000

typedef struct {
    const void *who; // the client whose SysEx is open on the wire, NULL = none
    uint32_t last;   // ms: its last write
} vti_sxhold;

static inline int vti_sx_open(const vti_sxwatch *w)
{
    for (int i = 0; i < 16; i++)
        if (w->n[i])
            return 1;
    return 0;
}

// 1 = client c's write waits (it started waiting at `start`)
static inline int vti_sx_waits(const vti_sxhold *h, const void *c, uint32_t now, uint32_t start)
{
    return h->who && h->who != c && now - h->last < VTI_SX_HOLD_MS && now - start < VTI_SX_HOLD_MS;
}

// after client c's write went to the Virus; w = its SysEx state after that write
static inline void vti_sx_wrote(vti_sxhold *h, const void *c, const vti_sxwatch *w, uint32_t now)
{
    if (vti_sx_open(w))
        h->who = c, h->last = now;
    else if (h->who == c)
        h->who = NULL;
}

// client c leaves: 1 = it left a SysEx open on the wire (the caller logs it), and it holds nobody from now on
static inline int vti_sx_gone(vti_sxhold *h, const void *c)
{
    if (h->who != c)
        return 0;
    h->who = NULL;
    return 1;
}
