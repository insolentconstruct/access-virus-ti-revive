// The ASIO driver's per-buffer rules (asioclock.h): when its read position starts again, and the sample conversion.
#include "check.h"
#include "device/asioclock.h"

using namespace vti;

TEST(asioclock_resync_at_start_stall_and_new_stream)
{
    CHECK(asio_resync(-1, 5000));           // the start
    CHECK(!asio_resync(1000, 1000 + 8192)); // 8192 behind still plays on
    CHECK(asio_resync(1000, 1000 + 8193));  // a stall
    CHECK(asio_resync(5000, 4999));         // the ring started over
    CHECK(!asio_resync(5000, 5000));
}

TEST(asioclock_sample_conversion)
{
    CHECK_EQ(asio_in32(1), 65536);
    CHECK_EQ(asio_in32(-32768), INT32_MIN);
    CHECK_EQ(asio_out16(65536), 1);
    CHECK_EQ(asio_out16(32768), 1); // half a step rounds up
    CHECK_EQ(asio_out16(32767), 0);
    CHECK_EQ(asio_out16(-32768), 0);
    CHECK_EQ(asio_out16(-32769), -1);
    CHECK_EQ(asio_out16(INT32_MAX), 32767); // would wrap to -32768 unclipped
    CHECK_EQ(asio_out16(INT32_MIN), -32768);
    for (int s = -32768; s < 32768; s += 257)
        CHECK_EQ(asio_out16(asio_in32((int16_t)s)), s); // round trip
}

// R5-5: the input latency the driver reports. A buffer's first frame is `buffer` frames old once the buffer is complete;
// its last frame came in one USB packet (1 ms) and then waited in the broker for the rest of its piece (0 to
// VTI_AUDIO_NP packets, half of that on average). Before, only the buffer was reported (256 at 256; the original 392)
TEST(asioclock_input_latency_counts_the_usb_path)
{
    CHECK_EQ(asio_in_latency(256, 44100), 256 + 132); // (4 / 2 + 1) packets of 44.1 frames
    CHECK_EQ(asio_in_latency(256, 48000), 256 + 144);
    CHECK_EQ(asio_in_latency(64, 44100), 64 + 132);
    CHECK_EQ(asio_in_latency(2048, 48000), 2048 + 144);
}
