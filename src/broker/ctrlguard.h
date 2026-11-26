// ctrlguard.h - which USB control requests a broker client may send to the Virus (AGENTS.md rule 2). Our clients only
// read standard information (GET_DESCRIPTOR for the model, E115). Vendor requests reach the Virus' flash protocol
// (E026/E040; the write/erase requests of an OS update or "Burn To Flash" are not known, E169), class requests the
// audio function, standard host-to-device requests its configuration - none of them may pass a broker. Plain C, no
// platform headers: the Windows and the POSIX broker use it, the unit test calls it directly.
#pragma once
#include <stdint.h>
#include "vtiproto.h" // VTIMSG_MAX

// 1 = pass it to the Virus, 0 = refuse it (the client gets -1)
static inline int vti_control_allowed(const uint8_t setup[8])
{
    uint8_t rt = setup[0], req = setup[1];
    if (rt != 0x80 && rt != 0x81 && rt != 0x82) // device-to-host, standard, to device / interface / endpoint
        return 0;
    return req == 0 /* GET_STATUS */ || req == 6 /* GET_DESCRIPTOR */ || req == 8 /* GET_CONFIGURATION */ ||
           req == 10 /* GET_INTERFACE */;
}

// may a client's VTIMSG_WRITE go to endpoint `ep`? Only the plug-in channel EP 0x06 (MI04 OUT): every client writes
// there and nowhere else. EP 0x05 is the USB chip's command endpoint (the one known command is the driver's handshake,
// E030, which the broker sends itself); an IN endpoint would make libusb run an IN transfer for the client (audit
// 2026-11-17 #2)
static inline int vti_write_allowed(uint8_t ep) { return ep == 0x06; }

// The plug-in's sendguard.h rule for every client (Control Center, virusctl, a buggy one): never the Multi parameters
// 0-3 of page 0x72 (F0 00 20 33 01 dev 72 part param value F7: Flags, Original Bank, Original Patch; issue #7,
// E255/E258) nor status 0xF4 (the DSP's private commands, E236/E248). A client's USB-MIDI packets go through this per
// cable, so a message cut between two writes is caught in the write that carries its parameter byte.
typedef struct {
    uint8_t b[16][9]; // per cable: the first 9 bytes of the SysEx going on
    uint8_t n[16];    // bytes of it seen (capped at 9), 0 = none going on
} vti_sxwatch;

// 1 = refuse the whole write
static inline int vti_write_blocked(vti_sxwatch *w, const uint8_t *d, int len)
{
    int hit = 0;
    for (int i = 0; i + 3 < len; i += 4) {
        const int cin = d[i] & 0xF;
        uint8_t *b = w->b[d[i] >> 4], *n = &w->n[d[i] >> 4];
        hit |= d[i + 1] == 0xF4 || d[i + 2] == 0xF4 || d[i + 3] == 0xF4; // every other byte is below 0x80 or F0/F7
        if (cin < 0x4 || cin > 0x7) // not SysEx
            continue;
        for (int k = 1; k <= (cin == 0x5 ? 1 : cin == 0x6 ? 2 : 3); k++) {
            if (d[i + k] == 0xF0)
                *n = 0;
            if ((d[i + k] == 0xF0 || *n) && *n < 9)
                b[(*n)++] = d[i + k];
        }
        hit |= *n == 9 && b[1] == 0x00 && b[2] == 0x20 && b[3] == 0x33 && b[4] == 0x01 && b[6] == 0x72 && b[8] <= 3;
        if (cin != 0x4) // CIN 5-7: the SysEx ended
            *n = 0;
    }
    return hit;
}

// the data length of a client's control request: wLength, capped to the reply buffer, and for a host-to-device request
// never more than the `sent` bytes the message carries (audit #20: the rest was uninitialised stack, logged in Nerd mode)
static inline uint16_t vti_control_len(const uint8_t setup[8], uint32_t sent)
{
    uint32_t wl = (uint32_t)(setup[6] | setup[7] << 8);
    if (wl > VTIMSG_MAX - 16)
        wl = VTIMSG_MAX - 16;
    if (!(setup[0] & 0x80) && wl > sent)
        wl = sent;
    return (uint16_t)wl;
}
