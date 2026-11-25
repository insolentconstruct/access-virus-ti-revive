// patchhistory.h - "Revert to" and "Recently Used" of the original's LOAD menu (E220), per part: the patch that was on
// the part before the last load (a revert swaps it with the current one, edits included, so a second revert goes back)
// and the loaded patches, newest first, at most MAX (the original keeps 10 per part); a patch already in the list
// moves to the top, a load from the list keeps the order. Kept for the session only, like the original (not in the
// project). Patches are 524-byte single dumps. Header only. Not thread safe: the caller locks.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vti {

class PatchHistory {
public:
    using Patch = std::vector<uint8_t>;
    static constexpr int PARTS = 16, MAX = 10;

    // a patch was loaded into `part`; `before` is what the part held (edits included)
    bool loaded(int part, const Patch &before, const Patch &patch)
    {
        if (part < 0 || part >= PARTS)
            return false;
        auto &p = parts_[part];
        p.revert = before;
        for (size_t i = 0; i < p.recent.size(); i++)
            if (same(p.recent[i], patch)) {
                p.recent.erase(p.recent.begin() + (long)i);
                break;
            }
        p.recent.insert(p.recent.begin(), patch);
        if (p.recent.size() > (size_t)MAX)
            p.recent.resize(MAX);
        return true;
    }
    // Recently Used entry `i` to load into `part` (empty if none): Revert gets `current`, the list keeps its order
    Patch take_recent(int part, int i, const Patch &current)
    {
        if (part < 0 || part >= PARTS || i < 0 || i >= (int)parts_[part].recent.size())
            return {};
        parts_[part].revert = current;
        return parts_[part].recent[i];
    }
    // "Revert to": the patch to load; `current` takes its place for the way back
    Patch revert(int part, const Patch &current)
    {
        if (!can_revert(part))
            return {};
        Patch back = parts_[part].revert;
        parts_[part].revert = current;
        return back;
    }

    bool can_revert(int part) const { return part >= 0 && part < PARTS && !parts_[part].revert.empty(); }
    std::string revert_name(int part) const { return can_revert(part) ? name(parts_[part].revert) : ""; }
    const std::vector<Patch> &recent(int part) const { return parts_[part < 0 || part >= PARTS ? 0 : part].recent; }
    std::vector<std::string> recent_names(int part) const
    {
        std::vector<std::string> v;
        for (auto &p : recent(part))
            v.push_back(name(p));
        return v;
    }

    // the patch name (10 characters at 249, trailing spaces removed)
    static std::string name(const Patch &p)
    {
        if (p.size() < 259)
            return "";
        std::string n(p.begin() + 249, p.begin() + 259);
        n.erase(n.find_last_not_of(' ') + 1);
        return n;
    }

    // the same patch from another place: everything but bank/program (7, 8) and the checksums (265, 522)
    static bool same(const Patch &a, const Patch &b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
            if (i != 7 && i != 8 && i != 265 && i != 522 && a[i] != b[i])
                return false;
        return true;
    }

private:
    struct Part {
        Patch revert;
        std::vector<Patch> recent;
    };
    std::array<Part, PARTS> parts_;
};

} // namespace vti
