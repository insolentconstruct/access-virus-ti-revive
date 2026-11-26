// dragsend.h - one parameter message per value during a mouse drag, not one per mouse move.
//
// Why: the editor called ui_set() on every mouse move of a knob, slider or arpeggiator step drag, also while the value
// stayed inside one step. Automatable parameters pass the host and the automation limit (250/s), but the others
// (menus, globals, arpeggiator steps) go straight into the queue to the Virus, which the byte budget does not cover
// (review 2026-10-19; AGENTS.md rule 2: a parameter flood can hang the Virus' audio). A drag now sends a value only
// when it differs from the last one it sent for that parameter: at most one message per value step and parameter,
// also when one drag moves two (a graph handle's x and y, the arpeggiator step corner).
#pragma once
#include <map>
#include <string>
#include <utility>

namespace vti {

class DragSend {
public:
    void reset() { sent_.clear(); } // a new drag: its first values always go out
    // true when value differs from the last one sent for (path, part) in this drag; it then counts as sent
    bool changed(const std::string &path, int part, int value)
    {
        auto [it, added] = sent_.try_emplace({path, part}, value);
        if (!added && it->second == value)
            return false;
        it->second = value;
        return true;
    }

private:
    std::map<std::pair<std::string, int>, int> sent_;
};

} // namespace vti
