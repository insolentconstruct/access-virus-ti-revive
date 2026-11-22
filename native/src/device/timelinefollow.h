// timelinefollow.h - how the plug-in's Virus timeline follows the audio ring once it is anchored (E337). Per block the
// plug-in measures err = (newest ring frame - S) - timeline. With an uneven host cadence (Ableton at a 2048 sound-card
// buffer, every ~6 min for about a minute) that error swings +-800 frames within a second while its average stays put;
// reacting to each block's err jumped the read position back and forth (each jump skips or repeats 12-19 ms = a click)
// and stepped up to ~59 times a second. Here a jump needs the error to stay on one side beyond JUMP for a while, and
// the one-frame drift steps follow the error's mean over a few seconds. A real lasting offset (REAPER after a render,
// E279; a DAW stall) still jumps: after half a second when the error is steady, after ~2 s when the cadence is rough,
// at once when it is far off.
// Header only, no allocation (runs on the audio thread): the plug-in and the tests use it.
#pragma once
#include <cstdint>

namespace vti {

class TimelineFollow {
public:
    static constexpr int64_t JUMP = 512; // jump back to real time beyond this (with the marker only)...
    static constexpr int64_t AT_ONCE = 4096; // ...at once beyond this: no jitter comes near (E337: 852), REAPER ahead does
    static constexpr int64_t STEP = 256; // drift: start stepping when the mean error is beyond this...
    static constexpr int64_t HOLD = 128; // ...and step until it is back inside this
    static constexpr int64_t SETTLE = AT_ONCE; // after Play: ahead this long (and 16 blocks) = the burst is over, at the latest
    static constexpr int64_t CALM = 1024; // ...and only if err spread less than this before Play (REAPER ~480)
    // a catch-up beyond this at a window end: one crossfaded jump, not a one-frame step per block. Drift up to 500 ppm
    // passes STEP by at most ~48 frames a 2 s window (owed <= STEP - HOLD + 48 = 176); a host xrun leaves 256+ (E579)
    static constexpr int64_t FADE = 192;
    // ...and only in steady playback: not for this many half-second windows after a Play, a setActive(1), an anchor or a
    // hard jump (the Play's learn second and the two 2 s mean windows after it, where a burst's rest shows; rc5 faded
    // REAPER's bursts and the rest of a pre-shift, E581). There every move is as before E579: hard jumps and steps
    static constexpr int64_t QUIET = 8;

    struct Move {
        int64_t delta; // added to the timeline: 0, +-1 (a drift step) or the jump
        bool jump;
        bool fade = false; // a jump the read path crossfades (crossfade.h): the catch-up after a host xrun
    };

    // `frames`: the Virus frames of half a second (the jump window; the mean window is four of them). A new rate starts
    // the windows afresh, their frames and errors were counted at the old one; an activation keeps its time left
    void set_window(int64_t frames)
    {
        const int64_t old = window_;
        window_ = frames > 0 ? frames : 1;
        fresh_left_ = fresh_left_ * window_ / old;
        anchor();
    }

    // the host reactivated the plug-in (setActive(1)): REAPER does it right before every Play, then renders its burst
    // (the size of our latency: delay compensation, likely). A Play within a second of it is a fresh start, the cadence
    // before it says nothing - without this, fast restarts never looked calm (each Play's own jump is in the range),
    // waited the half second each and piled up (user, err -4320, 16 late events)
    void activated() { fresh_left_ = 2 * window_, quiet_left_ = QUIET * window_; }

    // the stamp-aligned read's margin behind the newest frame (virusplugin.h timing_m(), VTI_NATIVE_MARGIN): a block
    // with err below -margin reads frames that are not there yet and is pulled back (#51)
    void set_margin(int64_t m) { margin_ = m > 0 ? m : 512; }

    // Play (or a loop restart): REAPER then processes 7 blocks ahead at once and stays ahead (Linux, 1792 frames at
    // block 256, also with anticipative FX off). The timeline is then ahead of the ring and each block reads at its
    // newest frame (frames repeated or skipped: crackle) - for the half second of the jump window, exactly while the
    // Play block's notes sound. So for one second after Play an ahead beyond JUMP jumps as soon as the burst is over
    // (err stops falling; SETTLE frames at the latest), by the most-ahead error seen, so no read is ahead afterwards
    // (what the error recovered in the burst's last block becomes latency: behind only adds latency). One jump per
    // Play, only ahead, and only after a calm cadence: in a rough one (E337) the most-ahead error is jitter, and
    // jumping by it moved the band past AT_ONCE - back and forth (device, --rough + --transport). There everything
    // stays as without Play. No block since the anchor: the cadence is unknown, so not calm either
    // Returns a shift for the timeline (<= 0, see below). `from_stop`: a real Play, not a loop restart or a jump while
    // playing (those have no burst).
    // #37 (E571-E573): the burst is at least the latency the plug-in reports (the host renders its delay compensation
    // at once: REAPER ~1 x, Logic ~2 x) and the host stays that far ahead, so a jump after it made the burst's notes
    // late by its size. A Play from a stop measures its burst (the most-ahead error over a second, without our own
    // moves); the next one returns it as a shift back BEFORE the burst: its reads then hit frames that are there
    // (the quiet before Play), nothing jumps afterwards, and only notes inside the burst play at once instead of on
    // time. A burst larger or smaller than learned leaves the rest to the jumps below and is learned anew
    int64_t play_started(bool from_stop = false)
    {
        const int64_t lo = cur_min_ < prev_min_ ? cur_min_ : prev_min_, hi = cur_max_ > prev_max_ ? cur_max_ : prev_max_;
        const bool calm = hi >= lo && hi - lo < CALM; // hi < lo: no err in either bucket
        play_left_ = calm || fresh_left_ > 0 ? 2 * window_ : 0;
        play_frames_ = 0, play_min_ = 0;
        const bool learn = from_stop && play_left_ > 0;
        shift_ = learn && learned_ > JUMP ? learned_ : 0;
        learn_left_ = learn ? 2 * window_ : 0;
        learn_sum_ = -shift_, learn_min_ = 0;
        // where the burst is measured from: the error's normal low before Play (the cadence), but after a setActive
        // cycle's own burst within the last second (REAPER's start, a block switch: not calm) that low is the burst's -
        // learned 765 instead of ~3700 (E581). Then the low since the last jump (the rest of that burst, if any)
        const int64_t since = since_lo_ != INT64_MAX ? since_lo_ : last_err_;
        base_ = calm ? (lo < 0 ? lo : 0) : since < 0 ? since : 0;
        if (play_left_ > 0) // a Play window (no window in a rough cadence: everything stays as without Play)
            quiet_left_ = QUIET * window_;
        return -shift_;
    }

    // process() re-anchors on abs(err) > 8192: while a pre-shift is being eaten by the burst, this err is expected
    bool expects(int64_t err) const { return shift_ > 0 && err >= -JUMP && err <= shift_ + JUMP; }

    // a new anchor: the timeline starts afresh, and so does the error's range - it was measured against the old
    // timeline. An activation stays: REAPER's Play block can be an anchor block (E351)
    void anchor()
    {
        restart();
        last_err_ = 0;
        learn_left_ = 0, shift_ = 0; // the learned burst stays: it belongs to the host, not to this timeline
        quiet_left_ = QUIET * window_;
        cur_frames_ = 0, cur_min_ = INT64_MAX, cur_max_ = INT64_MIN, prev_min_ = INT64_MAX, prev_max_ = INT64_MIN;
    }

    // one block: its err and its length in Virus frames; `marker` = the Virus' stamp clock is tied to the ring (only
    // then may the timeline jump without a new anchor, E279)
    Move update(int64_t err, int64_t frames, bool marker)
    {
        spread(err, frames);
        const int64_t last = last_err_;
        last_err_ = err;
        since_lo_ = err < since_lo_ ? err : since_lo_;
        if (fresh_left_ > 0)
            fresh_left_ -= frames;
        if (quiet_left_ > 0)
            quiet_left_ -= frames;
        if (learn_left_ > 0) {
            learn_left_ -= frames;
            const int64_t raw = err + learn_sum_; // the error as if we had not moved since Play
            learn_min_ = raw < learn_min_ ? raw : learn_min_;
            if (learn_left_ <= 0) {
                const int64_t burst = base_ - learn_min_;
                learned_ = burst > JUMP ? (burst < 2 * window_ ? burst : 2 * window_) : 0;
                const bool shifted = shift_ > 0;
                shift_ = 0;
                if (shifted && marker && err > JUMP) // the burst was smaller than the shift: forward by the rest now
                    return jumped(err);
            } else if (shift_ > 0 && err >= -JUMP)
                return {0, false}; // pre-shifted: the burst eats the shift, no move meanwhile
        }
        if (marker && (err > AT_ONCE || err < -AT_ONCE)) // REAPER processing ahead after Play (up to -8548): no waiting
            return jumped(err);
        if (play_left_ > 0) {
            play_left_ -= frames;
            if (marker && err < -JUMP) {
                play_min_ = play_frames_ == 0 || err < play_min_ ? err : play_min_;
                play_frames_ += frames;
                // in the burst the ring does not move (microseconds per block): err falls by each whole block; the
                // first block where it does not fall = the burst is over. SETTLE: in case it never seems to end
                if ((play_frames_ > frames && err >= last) || play_frames_ >= settle(frames)) {
                    return jumped(play_min_);
                }
            } else
                play_frames_ = 0, play_min_ = 0;
        }
        const int side = err > JUMP ? 1 : err < -JUMP ? -1 : 0;
        if (marker && side != 0) {
            if (side != far_side_)
                far_side_ = side, far_frames_ = 0, far_sum_ = 0, far_n_ = 0;
            far_frames_ += frames, far_sum_ += err, far_n_++;
            if (far_frames_ >= window_) // beyond JUMP on one side for half a second: a real offset (an xrun of
                return faded(far_sum_ / far_n_); // 512+ frames too, E579: crossfaded)
        } else
            far_side_ = 0, far_frames_ = 0, far_sum_ = 0, far_n_ = 0;

        const int64_t paid = err - owed_; // err once the owed steps are paid: a window during a payback must not count it twice
        mean_frames_ += frames, mean_sum_ += paid, mean_n_++;
        mean_lo_ = paid < mean_lo_ ? paid : mean_lo_, mean_hi_ = paid > mean_hi_ ? paid : mean_hi_;
        mean_low_n_ += paid < -3 * margin_ / 4;
        if (mean_frames_ > 3 * window_) // the window's last half second: where the error is now, for a catch-up
            tail_sum_ += paid, tail_n_++;
        if (mean_frames_ >= 4 * window_) {
            const int64_t mean = mean_sum_ / mean_n_, lo = mean_lo_, hi = mean_hi_;
            const bool low = mean_low_n_ * 16 > mean_n_; // the band's low end, not one late delivery
            const int64_t tail = tail_n_ > 0 ? tail_sum_ / tail_n_ : mean;
            tail_sum_ = 0, tail_n_ = 0;
            mean_frames_ = 0, mean_sum_ = 0, mean_n_ = 0, mean_lo_ = INT64_MAX, mean_hi_ = INT64_MIN, mean_low_n_ = 0;
            if (marker && (mean > JUMP || mean < -JUMP)) // an offset hidden in a rough cadence (or an xrun, E579)
                return faded(mean);
            owed_ = mean > STEP ? mean - HOLD : mean < -STEP ? mean + HOLD : 0;
            // a host xrun (REAPER on Linux drops cycles): the ring ran on, the timeline did not. One frame per block
            // was ~375 skips a second for a second at block 128, a short metallic sound (E579): one crossfade instead,
            // by the last half second's offset (an xrun inside the window dilutes its mean: the rest became steps).
            // The #51 lift below keeps its steps (its tests: two clocks, a late anchor)
            const int64_t catchup = tail > STEP ? tail - HOLD : tail < -STEP ? tail + HOLD : 0;
            if (marker && quiet_left_ <= 0 && owed_ > 0 && catchup > FADE) // forward only: an xrun loses cycles
                return faded(catchup);
            // #51 (E564): two clocks park the mean at the band's edge and delivery jitter then takes err past -margin
            // (the read is pulled back: a click). A window whose low end passed 3/4 of the margin is lifted back to
            // half of it, if its whole range fits 1.5 margins; a wider one (rough cadence, E337) no position fits.
            // Only a low end passed in more than 1/16 of its blocks (one late USB delivery's dip lifted the band), and
            // only if the lifted mean stays inside STEP: past it the next window stepped the band back down, out and
            // back, hundreds of one-frame skips for nothing (E579)
            if (low && hi - lo < 3 * margin_ / 2 && lo + margin_ / 2 < owed_ && mean - (lo + margin_ / 2) <= STEP)
                owed_ = lo + margin_ / 2;
        }
        if (owed_ != 0) {
            const int64_t d = owed_ > 0 ? 1 : -1;
            owed_ -= d;
            return {d, false};
        }
        return {0, false};
    }

private:
    // a burst that never seems to end jumps after this: SETTLE, but at least 16 blocks - at block 2048 SETTLE alone ended
    // Logic's burst of ~6 blocks after the first one (E570, #37). SETTLE = AT_ONCE: a burst at least the latency
    // (3584 + block, E574) ran into 2048 mid-way at blocks below ~240 (REAPER at 128: the rest waited half a second,
    // 202 reads pulled back); a burst that keeps falling block by block reaches AT_ONCE first anyway
    static int64_t settle(int64_t frames) { return SETTLE > 16 * frames ? SETTLE : 16 * frames; }

    Move jumped(int64_t d)
    {
        restart();
        learn_sum_ += d; // the burst is still measured from Play on
        quiet_left_ = QUIET * window_;
        return {d, true};
    }

    // a lasting offset in steady playback (host xrun, E579): the same jump, which the read path crossfades
    // (crossfade.h). Forward only: an xrun leaves the timeline behind the ring, a host ahead of it is a burst (and the
    // old frames of a jump back are not in the ring yet anyway). Elsewhere the hard jump, as before (E581)
    Move faded(int64_t d)
    {
        if (d <= 0 || quiet_left_ > 0)
            return jumped(d);
        restart();
        learn_sum_ += d;
        return {d, true, true};
    }

    // after a jump: the windows start afresh and a Play's jump is used up; the error's range stays (each Play's own
    // jump is in it, E351)
    void restart()
    {
        far_side_ = 0, far_frames_ = 0, far_sum_ = 0, far_n_ = 0;
        mean_frames_ = 0, mean_sum_ = 0, mean_n_ = 0, mean_lo_ = INT64_MAX, mean_hi_ = INT64_MIN, mean_low_n_ = 0;
        tail_sum_ = 0, tail_n_ = 0;
        owed_ = 0;
        play_left_ = 0, play_frames_ = 0, play_min_ = 0;
        since_lo_ = INT64_MAX;
    }

    // the error's range over the last 0.5-1 s (two half-second buckets): how calm the cadence is
    void spread(int64_t err, int64_t frames)
    {
        cur_min_ = err < cur_min_ ? err : cur_min_, cur_max_ = err > cur_max_ ? err : cur_max_;
        if ((cur_frames_ += frames) >= window_)
            prev_min_ = cur_min_, prev_max_ = cur_max_, cur_frames_ = 0, cur_min_ = INT64_MAX, cur_max_ = INT64_MIN;
    }

    int64_t window_ = 22050;
    int far_side_ = 0;
    int64_t far_frames_ = 0, far_sum_ = 0, far_n_ = 0;
    int64_t mean_frames_ = 0, mean_sum_ = 0, mean_n_ = 0, mean_lo_ = INT64_MAX, mean_hi_ = INT64_MIN, mean_low_n_ = 0;
    int64_t tail_sum_ = 0, tail_n_ = 0; // the mean window's last half second
    int64_t owed_ = 0;
    int64_t margin_ = 512; // virusplugin.h timing_m()
    int64_t play_left_ = 0, play_frames_ = 0, play_min_ = 0;
    int64_t fresh_left_ = 0;
    int64_t quiet_left_ = 0; // frames until a catch-up may be crossfaded again (QUIET)
    int64_t last_err_ = 0; // the previous block's err
    int64_t since_lo_ = INT64_MAX; // the lowest err since the last jump or anchor
    int64_t learned_ = 0;  // the last Play's burst (frames), at most a second
    int64_t shift_ = 0;    // the pre-shift of this Play while its burst eats it
    int64_t learn_left_ = 0, learn_sum_ = 0, learn_min_ = 0, base_ = 0;
    int64_t cur_frames_ = 0, cur_min_ = INT64_MAX, cur_max_ = INT64_MIN, prev_min_ = INT64_MAX, prev_max_ = INT64_MIN;
};

} // namespace vti
