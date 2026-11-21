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
    static constexpr int64_t SETTLE = 2048; // after Play: ahead this long = REAPER's burst (7 x 256) is over, at the latest
    static constexpr int64_t CALM = 1024; // ...and only if err spread less than this before Play (REAPER ~480)

    struct Move {
        int64_t delta; // added to the timeline: 0, +-1 (a drift step) or the jump
        bool jump;
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
    void activated() { fresh_left_ = 2 * window_; }

    // Play (or a loop restart): REAPER then processes 7 blocks ahead at once and stays ahead (Linux, 1792 frames at
    // block 256, also with anticipative FX off). The timeline is then ahead of the ring and each block reads at its
    // newest frame (frames repeated or skipped: crackle) - for the half second of the jump window, exactly while the
    // Play block's notes sound. So for one second after Play an ahead beyond JUMP jumps as soon as the burst is over
    // (err stops falling; SETTLE frames at the latest), by the most-ahead error seen, so no read is ahead afterwards
    // (what the error recovered in the burst's last block becomes latency: behind only adds latency). One jump per
    // Play, only ahead, and only after a calm cadence: in a rough one (E337) the most-ahead error is jitter, and
    // jumping by it moved the band past AT_ONCE - back and forth (device, --rough + --transport). There everything
    // stays as without Play. No block since the anchor: the cadence is unknown, so not calm either
    void play_started()
    {
        const int64_t lo = cur_min_ < prev_min_ ? cur_min_ : prev_min_, hi = cur_max_ > prev_max_ ? cur_max_ : prev_max_;
        const bool calm = hi >= lo && hi - lo < CALM; // hi < lo: no err in either bucket
        play_left_ = calm || fresh_left_ > 0 ? 2 * window_ : 0;
        play_frames_ = 0, play_min_ = 0;
    }

    // a new anchor: the timeline starts afresh, and so does the error's range - it was measured against the old
    // timeline. An activation stays: REAPER's Play block can be an anchor block (E351)
    void anchor()
    {
        restart();
        last_err_ = 0;
        cur_frames_ = 0, cur_min_ = INT64_MAX, cur_max_ = INT64_MIN, prev_min_ = INT64_MAX, prev_max_ = INT64_MIN;
    }

    // one block: its err and its length in Virus frames; `marker` = the Virus' stamp clock is tied to the ring (only
    // then may the timeline jump without a new anchor, E279)
    Move update(int64_t err, int64_t frames, bool marker)
    {
        spread(err, frames);
        const int64_t last = last_err_;
        last_err_ = err;
        if (fresh_left_ > 0)
            fresh_left_ -= frames;
        if (marker && (err > AT_ONCE || err < -AT_ONCE)) { // REAPER processing ahead after Play (up to -8548): no waiting
            restart();
            return {err, true};
        }
        if (play_left_ > 0) {
            play_left_ -= frames;
            if (marker && err < -JUMP) {
                play_min_ = play_frames_ == 0 || err < play_min_ ? err : play_min_;
                play_frames_ += frames;
                // in the burst the ring does not move (microseconds per block): err falls by each whole block; the
                // first block where it does not fall = the burst is over. SETTLE: in case it never seems to end
                if ((play_frames_ > frames && err >= last) || play_frames_ >= SETTLE) {
                    const int64_t d = play_min_;
                    restart();
                    return {d, true};
                }
            } else
                play_frames_ = 0, play_min_ = 0;
        }
        const int side = err > JUMP ? 1 : err < -JUMP ? -1 : 0;
        if (marker && side != 0) {
            if (side != far_side_)
                far_side_ = side, far_frames_ = 0, far_sum_ = 0, far_n_ = 0;
            far_frames_ += frames, far_sum_ += err, far_n_++;
            if (far_frames_ >= window_) { // beyond JUMP on one side for half a second: a real offset
                const int64_t d = far_sum_ / far_n_;
                restart();
                return {d, true};
            }
        } else
            far_side_ = 0, far_frames_ = 0, far_sum_ = 0, far_n_ = 0;

        mean_frames_ += frames, mean_sum_ += err, mean_n_++;
        if (mean_frames_ >= 4 * window_) {
            const int64_t mean = mean_sum_ / mean_n_;
            mean_frames_ = 0, mean_sum_ = 0, mean_n_ = 0;
            if (marker && (mean > JUMP || mean < -JUMP)) { // an offset hidden in a rough cadence
                restart();
                return {mean, true};
            }
            owed_ = mean > STEP ? mean - HOLD : mean < -STEP ? mean + HOLD : 0;
        }
        if (owed_ != 0) {
            const int64_t d = owed_ > 0 ? 1 : -1;
            owed_ -= d;
            return {d, false};
        }
        return {0, false};
    }

private:
    // after a jump: the windows start afresh and a Play's jump is used up; the error's range stays (each Play's own
    // jump is in it, E351)
    void restart()
    {
        far_side_ = 0, far_frames_ = 0, far_sum_ = 0, far_n_ = 0;
        mean_frames_ = 0, mean_sum_ = 0, mean_n_ = 0;
        owed_ = 0;
        play_left_ = 0, play_frames_ = 0, play_min_ = 0;
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
    int64_t mean_frames_ = 0, mean_sum_ = 0, mean_n_ = 0;
    int64_t owed_ = 0;
    int64_t play_left_ = 0, play_frames_ = 0, play_min_ = 0;
    int64_t fresh_left_ = 0;
    int64_t last_err_ = 0; // the previous block's err
    int64_t cur_frames_ = 0, cur_min_ = INT64_MAX, cur_max_ = INT64_MIN, prev_min_ = INT64_MAX, prev_max_ = INT64_MIN;
};

} // namespace vti
