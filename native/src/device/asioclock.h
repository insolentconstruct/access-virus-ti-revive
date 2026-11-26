// asioclock.h - the ASIO driver's per-buffer rules (asio/asiodriver.cpp), header only: the driver builds on Windows
// only, the tests run them everywhere.
#pragma once
#include <cstdint>
extern "C" {
#include "vtiaudioshm.h"
}

namespace vti {

// the read position starts again at the newest frame: at the start, after a stall (more than 8192 frames behind) and
// on a new stream (the ring went back)
inline bool asio_resync(int64_t pos, int64_t newest) { return pos < 0 || newest - pos > 8192 || newest < pos; }

// the Virus' 16-bit input samples to the host's 32-bit integers, and the host's outputs back to 16 bits (rounded, the
// top clipped: full scale + half a step would wrap to the bottom)
inline int32_t asio_in32(int16_t s) { return (int32_t)s * 65536; }
inline int16_t asio_out16(int32_t s)
{
    const int64_t v = ((int64_t)s + 32768) >> 16;
    return (int16_t)(v > 32767 ? 32767 : v);
}

// the input latency reported to the host (R5-5): a buffer's first frame is `buffer` frames old once the buffer is complete,
// and its last frame came in one USB packet (1 ms) and then waited in the broker for the rest of its piece (0 to
// VTI_AUDIO_NP packets, half of that on average): buffer + (VTI_AUDIO_NP / 2 + 1) ms of frames. Inferred from the path,
// not measured (a loopback test through the Virus would tell); 4 ms pieces at 256: 388 (44.1 kHz), 400 (48 kHz)
inline long asio_in_latency(long buffer, int rate) { return buffer + (long)(VTI_AUDIO_NP + 2) * rate / 2000; }

} // namespace vti
