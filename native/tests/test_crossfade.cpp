// Crossfade: a timeline catch-up (TimelineFollow fade move) rendered as a short fade from the audio at the old read
// position to the new one. The model reads a sine "ring" block by block as process() does: each block at the
// timeline's position, during the fade also at the old one (position - delta), and mixes them.
#include "check.h"
#include "device/crossfade.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace vti;

namespace {

float ring(int64_t f) { return (float)(0.5 * std::sin(2 * 3.14159265358979 * 440.0 * (double)f / 44100.0)); }

// `blocks` blocks of `block` frames from frame 10000; the timeline moves by `delta` at the start of block `at`
std::vector<float> render(int block, int blocks, int at, int64_t delta, bool fade)
{
    Crossfade x;
    std::vector<float> out, dst(block), old(block);
    int64_t pos = 10000;
    for (int b = 0; b < blocks; b++) {
        if (b == at) {
            pos += delta;
            if (fade)
                x.start(delta);
        }
        for (int i = 0; i < block; i++)
            dst[i] = ring(pos + i);
        if (const int k = x.frames(block)) {
            for (int i = 0; i < k; i++)
                old[i] = ring(pos - x.delta() + i);
            x.mix(dst.data(), old.data(), k);
            x.advance(k);
        }
        out.insert(out.end(), dst.begin(), dst.end());
        pos += block;
    }
    return out;
}

float worst_step(const std::vector<float> &v)
{
    float w = 0;
    for (size_t i = 1; i < v.size(); i++)
        w = std::max(w, std::fabs(v[i] - v[i - 1]));
    return w;
}

} // namespace

TEST(crossfade_has_no_sample_jump)
{
    // 250 frames at 440 Hz = half a cycle off: a hard jump steps by up to the full amplitude; the fade's steps stay
    // near the sine's own (0.5 * 2 pi 440 / 44100 = 0.031) - forward and back, blocks smaller and larger than the fade
    for (int64_t delta : {250, -250, 384, -384}) {
        float hard = 0; // the hard jump's step depends on the sine's phase at the move: the largest over the blocks
        for (int block : {32, 100, 128, 512}) {
            hard = std::max(hard, worst_step(render(block, 24, 8, delta, false)));
            CHECK(worst_step(render(block, 24, 8, delta, true)) < 0.05f);
        }
        CHECK(hard > 0.2f);
    }
}

TEST(crossfade_lasts_its_length)
{
    // before the move the old audio, from LEN frames after it the new one exactly; between them a mix
    for (int block : {32, 100, 512}) {
        const int at = 4;
        const int64_t delta = 300, start = (int64_t)at * block;
        const std::vector<float> v = render(block, 16, at, delta, true);
        for (int64_t i = 0; i < (int64_t)v.size(); i++) {
            const int64_t pos = 10000 + i + (i >= start ? delta : 0);
            if (i >= start + Crossfade::LEN || i < start)
                CHECK(v[i] == ring(pos));
        }
        CHECK(std::fabs(v[start] - ring(10000 + start)) < 0.01f); // the fade's first frame is still nearly the old audio
    }
}

TEST(crossfade_stop_ends_it)
{
    Crossfade x;
    CHECK(!x.active());
    CHECK_EQ(x.frames(64), 0);
    x.start(200);
    CHECK(x.active());
    CHECK_EQ(x.frames(64), 64);
    x.advance(64);
    CHECK_EQ(x.frames(256), Crossfade::LEN - 64);
    x.stop();
    CHECK_EQ(x.frames(64), 0);
}

TEST(crossfade_old_frames_must_be_in_the_ring)
{
    // a 16384-frame ring keeps the newest 16384 - 1024 frames readable (AudioIn::read_at): a fade from older ones, or
    // from frames not there yet (the timeline jumped back), falls back to the hard jump
    const int64_t avail = 100000, ring = 16384;
    CHECK(Crossfade::readable(avail - 640 - 4096, 128, avail, ring));    // forward by 4096 (AT_ONCE)
    CHECK(Crossfade::readable(avail - ring + 1024, 128, avail, ring));   // the oldest readable frame
    CHECK(!Crossfade::readable(avail - ring + 1023, 128, avail, ring));  // overwritten already
    CHECK(Crossfade::readable(avail - 128, 128, avail, ring));           // ends at the newest frame
    CHECK(!Crossfade::readable(avail - 127, 128, avail, ring));          // one frame not there yet
}
