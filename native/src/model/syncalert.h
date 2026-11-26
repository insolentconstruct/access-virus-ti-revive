// syncalert.h - the original's sync error page (E205): "A sync error occurred. Press Stop and Play to clear." is the
// skin's Info Panel page 1. Virus Control 5.1.7 counts the Virus' Sync Information Events (global 0x54 = 1: an event
// arrived too late for its time stamp) and adds 20 to a level per event (0x180081980); its idle routine lowers the
// level by 1 per call (0x180080ba0); when the level is above 60 while the host plays and Live Mode is off, the page
// shows; the next Play clears it and the counters since Play (0x180081570). A single late event after a short stall
// therefore shows nothing, a burst does. Header only: the plug-in and the tests use it. Not thread safe: one caller.
#pragma once

namespace vti {

class SyncAlert {
public:
    static constexpr int PER_EVENT = 20, THRESHOLD = 60;

    void event() // a Sync Information Event (value 1) from the Virus
    {
        total_++;
        since_play_++;
        level_ += PER_EVENT;
        check();
    }
    void tick() // the original's idle call; the plug-in calls it every 50 ms
    {
        if (level_ > 0)
            level_--;
    }
    void play(bool playing) // the host's transport; a new start clears the page
    {
        if (playing && !playing_) {
            set_page(0);
            since_play_ = 0;
            level_ = 0;
        }
        playing_ = playing;
    }
    void live_mode(bool on) { live_ = on; }

    int page() const { return page_; }
    int level() const { return level_; } // test hook: the tests follow the level
    int total() const { return total_; }
    int since_play() const { return since_play_; }
    bool take_change() // the page changed since the last call (the editor shows the new one)
    {
        const bool c = changed_;
        changed_ = false;
        return c;
    }

private:
    void check()
    {
        if (playing_ && !live_ && level_ > THRESHOLD && page_ == 0)
            set_page(1);
    }
    void set_page(int p)
    {
        if (p != page_) {
            page_ = p;
            changed_ = true;
        }
    }
    int level_ = 0, total_ = 0, since_play_ = 0, page_ = 0;
    bool playing_ = false, live_ = false, changed_ = false;
};

} // namespace vti
