// crossfade.h - a timeline catch-up (TimelineFollow::Move::fade, after a host xrun) heard as a short crossfade from the
// audio at the old read position to the new one, instead of a hard skip or a second of one-frame skips (E579). The
// caller reads each block at the new position as always and, while active(), the first frames(n) frames once more at
// position - delta(), then mix()es every channel with them and advance()s. State: the jump and how far the fade is.
// Header only, no allocation (audio thread): the plug-in and the tests use it.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vti {

class Crossfade {
public:
    static constexpr int LEN = 128; // frames (~3 ms): long enough to hide the step, short enough to stay one event

    void start(int64_t delta) { delta_ = delta, done_ = 0; }
    void stop() { done_ = LEN; } // a hard move (anchor, jump) or the old audio is not readable: the rest is cut
    bool active() const { return done_ < LEN; }
    int64_t delta() const { return delta_; }
    // frames of an n-frame block still inside the fade (0 when none)
    int frames(int n) const { return active() ? std::min(n, LEN - done_) : 0; }

    // dst (k frames at the new position) becomes old -> dst, raised cosine; the same gains for every channel of a block
    void mix(float *dst, const float *old, int k) const
    {
        for (int i = 0; i < k; i++) {
            const float w = (float)(0.5 - 0.5 * std::cos(3.14159265358979323846 * (done_ + i + 1) / (LEN + 1)));
            dst[i] = old[i] + w * (dst[i] - old[i]);
        }
    }
    void advance(int k) { done_ += k; }
    // k frames from `from` are in a ring of `ring` frames whose newest is `avail`: not past it (a jump back) and not
    // overwritten yet (AudioIn::read_at keeps 1024 frames of room behind the writer); else the caller jumps hard
    static bool readable(int64_t from, int k, int64_t avail, int64_t ring)
    {
        return from + k <= avail && from >= avail - ring + 1024;
    }

private:
    int64_t delta_ = 0;
    int done_ = LEN;
};

} // namespace vti
