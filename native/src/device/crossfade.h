// crossfade.h - a timeline catch-up (TimelineFollow::Move::fade, after a host xrun) heard as a short crossfade from the
// audio at the old read position to the new one, instead of a hard skip or a second of one-frame skips (E579). Since
// E589 every move forward, and each one-frame drift step too (delta +-1: no click per step). The
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

    // the gains once, here (not on the audio thread): since E589 a fade also renders every one-frame step, up to one
    // per block for a second or two, and a cosine per frame and channel there was work for nothing
    Crossfade()
    {
        for (int i = 0; i < LEN; i++)
            gain_[i] = (float)(0.5 - 0.5 * std::cos(3.14159265358979323846 * (i + 1) / (LEN + 1)));
    }

    void start(int64_t delta) { delta_ = delta, done_ = 0; }
    void stop() { done_ = LEN; } // a hard move (anchor, jump) or the old audio is not readable: the rest is cut
    bool active() const { return done_ < LEN; }
    int64_t delta() const { return delta_; }
    // frames of an n-frame block still inside the fade (0 when none)
    int frames(int n) const { return active() ? std::min(n, LEN - done_) : 0; }

    // dst (k frames at the new position) becomes old -> dst, raised cosine; the same gains for every channel of a block
    void mix(float *dst, const float *old, int k) const
    {
        for (int i = 0; i < k; i++)
            dst[i] = old[i] + gain_[done_ + i] * (dst[i] - old[i]);
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
    float gain_[LEN]; // raised cosine, 0 -> 1
};

} // namespace vti
