// state.cpp - the plug-in's project state records and the meta parameter value mapping (see state.h)
#include "model/state.h"
#include "model/bankdrop.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vti {

// ---- state container ("MIDI" + u32 BE length, records of u32 BE length + message) ----------------------------------

std::vector<std::vector<uint8_t>> parse_container(const std::vector<uint8_t> &d)
{
    std::vector<std::vector<uint8_t>> out;
    if (d.size() < 8 || memcmp(d.data(), "MIDI", 4))
        return out;
    auto len = [&](size_t i) { // big-endian, little-endian when that has a top-10 bit set (the original's fallback)
        const uint32_t be = (uint32_t)d[i] << 24 | d[i + 1] << 16 | d[i + 2] << 8 | d[i + 3];
        return (size_t)((be & 0xFFC00000u) ? (uint32_t)d[i + 3] << 24 | d[i + 2] << 16 | d[i + 1] << 8 | d[i] : be);
    };
    const size_t total = len(4);
    if (total > d.size() - 8)
        return out; // a truncated project: nothing, so the caller keeps the data as it came (R8-5)
    const size_t end = 8 + total;
    for (size_t pos = 8; pos + 4 <= end;) {
        size_t n = len(pos);
        pos += 4;
        if (pos + n > end)
            break;
        out.emplace_back(d.begin() + pos, d.begin() + pos + n);
        pos += n;
    }
    return out;
}

std::vector<uint8_t> build_container(const std::vector<std::vector<uint8_t>> &recs)
{
    std::vector<uint8_t> d = {'M', 'I', 'D', 'I', 0, 0, 0, 0};
    auto be32 = [&](size_t v) {
        for (int s = 24; s >= 0; s -= 8)
            d.push_back((uint8_t)(v >> s));
    };
    for (auto &r : recs) {
        be32(r.size());
        d.insert(d.end(), r.begin(), r.end());
    }
    size_t n = d.size() - 8;
    for (int i = 0; i < 4; i++)
        d[4 + i] = (uint8_t)(n >> (24 - 8 * i));
    return d;
}

bool is_system_clock_global(const std::vector<uint8_t> &m)
{
    static const uint8_t head[7] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73};
    return m.size() == 11 && !memcmp(m.data(), head, sizeof head) && m[8] >= 0x0F && m[8] <= 0x11 && m[10] == 0xF7;
}

int drop_system_clock_globals(std::vector<std::vector<uint8_t>> &recs)
{
    const size_t before = recs.size();
    recs.erase(std::remove_if(recs.begin(), recs.end(), is_system_clock_global), recs.end());
    return (int)(before - recs.size());
}

int upgrade_state_dumps(std::vector<std::vector<uint8_t>> &recs)
{
    int n = 0;
    for (auto &r : recs)
        if (r.size() == 524 && r[0] == 0xF0 && r[6] == 0x10 && r[7] == 0x00 && (int8_t)r[9] < 12 &&
            RemoteDevice::upgrade_single(r))
            n++;
    return n;
}

int mark_project_dumps(std::vector<std::vector<uint8_t>> &recs)
{
    int n = 0;
    for (auto &r : recs)
        if (r.size() == 524 && r[0] == 0xF0 && r[1] == 0x00 && r[2] == 0x20 && r[3] == 0x33 && r[4] == 0x01 &&
            r[6] == 0x10) {
            set_patch_origin(r, 0, 0, false);
            BankDrop::checksums(r);
            n++;
        }
    return n;
}

int drop_input_atomizer(std::vector<std::vector<uint8_t>> &recs)
{
    const size_t before = recs.size();
    recs.erase(std::remove_if(recs.begin(), recs.end(),
                              [](const std::vector<uint8_t> &r) {
                                  return r.size() == 11 && r[0] == 0xF0 && r[6] == 0x6F && r[8] == 0x7E;
                              }),
               recs.end());
    return (int)(before - recs.size());
}

int keep_parts(std::vector<std::vector<uint8_t>> &recs, int n)
{
    const size_t before = recs.size();
    recs.erase(std::remove_if(recs.begin(), recs.end(),
                              [n](const std::vector<uint8_t> &r) {
                                  const bool dump = r.size() == 524 && r[0] == 0xF0 && r[6] == 0x10 && r[7] == 0x00;
                                  const bool channel = (r.size() == 2 || r.size() == 3) && r[0] >= 0x80 && r[0] < 0xF0;
                                  return (dump && r[8] >= n) || (channel && (r[0] & 15) >= n);
                              }),
               recs.end());
    return (int)(before - recs.size());
}

static bool is_multi_dump(const std::vector<uint8_t> &m)
{
    return m.size() == 267 && m[0] == 0xF0 && m[6] == 0x11 && m[266] == 0xF7;
}

std::vector<uint8_t> startup_multi(const std::vector<uint8_t> &fixed, const std::vector<uint8_t> &stored)
{
    if (!is_multi_dump(fixed) || !is_multi_dump(stored))
        return fixed;
    auto m = fixed;
    std::copy(stored.begin() + 137, stored.begin() + 153, m.begin() + 137); // Detune, parts 1-16
    std::copy(stored.begin() + 185, stored.begin() + 201, m.begin() + 185); // Output Select, parts 1-16
    unsigned sum = 0; // the template's add7 checksum over bytes 5..264
    for (int i = 5; i < 265; i++)
        sum += m[i];
    m[265] = (uint8_t)(sum & 0x7F);
    return m;
}

void multi_routing_values(const MidiMessage &t, const std::vector<uint8_t> &multi,
                          std::map<int, std::map<std::string, int>> &parts)
{
    if (!is_multi_dump(multi))
        return;
    for (auto &b : t.bytes)
        if (b.part >= 0 && b.part < 16 && (b.index == 137 + b.part || b.index == 185 + b.part))
            parts[b.part][b.content] = multi[(size_t)b.index];
}

bool is_part_dump(const std::vector<uint8_t> &m, int part)
{
    return m.size() == 524 && m[0] == 0xF0 && m[6] == 0x10 && m[7] == 0x00 && m[8] == part;
}

void keep_unset_values(std::map<int, std::map<std::string, int>> &next,
                       const std::map<int, std::map<std::string, int>> &prev)
{
    for (auto &[part, values] : prev)
        next[part].insert(values.begin(), values.end()); // insert keeps what `next` already has
}

int add_missing_part_dumps(std::vector<std::vector<uint8_t>> &recs, const std::vector<std::vector<uint8_t>> &defaults)
{
    int added = 0;
    for (int p = 0; p < 16; p++) {
        auto has = [p](const std::vector<uint8_t> &r) { return is_part_dump(r, p); };
        if (std::any_of(recs.begin(), recs.end(), has))
            continue;
        auto d = std::find_if(defaults.begin(), defaults.end(), has);
        if (d != defaults.end()) {
            recs.push_back(*d);
            added++;
        }
    }
    return added;
}

// editor-only per-part settings (LFO link buttons, ...) have no Virus message: they are kept in the state as
// F0 7D 'V' 'T' 'N' 01 <part> <path ASCII> 00 <value> F7 (7D = non-commercial ID). TI Revive never sends them; the
// original's setChunk does not filter them (R8-8: only 0xFF and 6F/7E records), so a TI Revive project opened there
// most likely sends them to the Virus, which is not known to act on a 7D message (wire not traced)
std::vector<uint8_t> editor_record(const std::string &path, int part, int value)
{
    std::vector<uint8_t> m = {0xF0, 0x7D, 'V', 'T', 'N', 0x01, (uint8_t)(part & 0x7F)};
    for (char ch : path)
        m.push_back((uint8_t)ch & 0x7F);
    m.push_back(0);
    m.push_back((uint8_t)(value & 0x7F));
    m.push_back(0xF7);
    return m;
}

bool parse_editor_record(const std::vector<uint8_t> &m, std::string &path, int &part, int &value)
{
    if (m.size() < 11 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 'V' || m[3] != 'T' || m[4] != 'N' || m[5] != 0x01)
        return false;
    part = m[6];
    size_t i = 7;
    path.clear();
    while (i < m.size() && m[i] != 0)
        path += (char)m[i++];
    if (i + 2 >= m.size())
        return false;
    value = m[i + 1];
    return true;
}

int meta_to_client(const MetaClient &c, int mv)
{
    for (auto &m : c.maps)
        if (m.second == mv)
            return m.first;
    return (int)std::lround(c.a * mv + c.b);
}

bool client_to_meta(const MetaClient &c, int cv, int &mv)
{
    for (auto &m : c.maps)
        if (m.first == cv)
            return mv = m.second, true;
    if (!c.maps.empty() || c.a == 0)
        return false;
    mv = (int)std::lround((cv - c.b) / c.a);
    return true;
}

} // namespace vti
