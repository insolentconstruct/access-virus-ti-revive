// project state container, editor-only records, meta parameter mapping
#include "check.h"
#include "model/state.h"
#include <cstring>

using namespace vti;
using Bytes = std::vector<uint8_t>;

TEST(container_round_trip)
{
    std::vector<Bytes> recs = {{}, {0xB0, 7, 100}, Bytes(524, 0x55), {0xF0, 0x7D, 0xF7}};
    auto d = build_container(recs);
    CHECK(d.size() == 8 + 4 * recs.size() + 3 + 524 + 3);
    CHECK(d[0] == 'M' && d[3] == 'I');
    CHECK(parse_container(d) == recs);
}

TEST(container_damaged)
{
    auto d = build_container({{1, 2, 3}, {4, 5, 6, 7}});
    // R8-5: a declared total longer than the data loads nothing, like the original (0x180060080)
    CHECK(parse_container(Bytes(d.begin(), d.end() - 2)).empty());
    Bytes cut = d; // a record longer than the rest of the total: the records before it stay
    cut[7] -= 2;
    cut.resize(cut.size() - 2);
    CHECK(parse_container(cut).size() == 1);
    Bytes bad = d;
    bad[0] = 'X';
    CHECK(parse_container(bad).empty());
    CHECK(parse_container({}).empty());
    Bytes huge = d;
    huge[8] = 0x7F; // first record claims ~2 GB
    CHECK(parse_container(huge).empty());
}

TEST(part_dump_detection)
{
    Bytes m(524, 0);
    m[0] = 0xF0, m[6] = 0x10, m[7] = 0x00, m[8] = 5;
    CHECK(is_part_dump(m, 5));
    CHECK(!is_part_dump(m, 4));
    m.resize(267);
    CHECK(!is_part_dump(m, 5));
}

TEST(editor_record_round_trip)
{
    auto r = editor_record("VirusControl Links/LFO 1 Link", 3, 1);
    CHECK(r.front() == 0xF0 && r.back() == 0xF7 && r[1] == 0x7D);
    std::string path;
    int part = -1, value = -1;
    CHECK(parse_editor_record(r, path, part, value));
    CHECK(path == "VirusControl Links/LFO 1 Link" && part == 3 && value == 1);
    r = editor_record("A/B", 15, 200); // 7-bit on the wire
    CHECK(parse_editor_record(r, path, part, value) && value == (200 & 0x7F));
    CHECK(!parse_editor_record({0xF0, 0x00, 0x20, 0x33, 1, 2, 3, 4, 5, 6, 0xF7}, path, part, value)); // Access SysEx
    CHECK(!parse_editor_record({0xF0, 0x7D, 'V', 'T', 'N', 1, 0, 'A', 'B'}, path, part, value));       // cut off
}

TEST(meta_mapping)
{
    MetaClient lin; // client = 2 * meta + 1
    lin.a = 2, lin.b = 1;
    CHECK_EQ(meta_to_client(lin, 10), 21);
    int mv = -1;
    CHECK(client_to_meta(lin, 21, mv) && mv == 10);
    MetaClient mapped;
    mapped.maps = {{0, 5}, {64, 6}, {127, 7}};
    CHECK_EQ(meta_to_client(mapped, 6), 64);
    CHECK(client_to_meta(mapped, 127, mv) && mv == 7);
    CHECK(!client_to_meta(mapped, 3, mv)); // a client value without map entry does not move the meta
    MetaClient flat;
    flat.a = 0;
    CHECK(!client_to_meta(flat, 5, mv)); // not invertible
}

TEST(state_system_clock_globals_never_kept)
{
    // E445: a report of System Clock/Frequency saved with a project went back to the Virus at every load, its USB chip
    // switched clock behind Core Audio's back. The original sends none of the three (E327)
    const auto global = [](uint8_t part, uint8_t id, uint8_t v) {
        return Bytes{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, part, id, v, 0xF7};
    };
    CHECK(is_system_clock_global(global(0x00, 0x0F, 1))); // SA Freq
    CHECK(is_system_clock_global(global(0x00, 0x10, 0))); // Frequency, what the project of E445 held
    CHECK(is_system_clock_global(global(0x00, 0x11, 1))); // Mode
    CHECK(is_system_clock_global(global(0x40, 0x10, 1))); // the same, as the Virus reports it with another part byte
    // the neighbours and look-alikes stay
    CHECK(!is_system_clock_global(global(0x00, 0x0E, 0)));
    CHECK(!is_system_clock_global(global(0x00, 0x12, 0)));
    CHECK(!is_system_clock_global(global(0x00, 0x40, 0))); // Local On
    CHECK(!is_system_clock_global(global(0x00, 0x09, 1))); // USB IO configuration
    Bytes multi = global(0x00, 0x10, 0);
    multi[6] = 0x72; // page 0x72: a multi parameter with the same number
    CHECK(!is_system_clock_global(multi));
    const Bytes longer = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x10, 0x00, 0x00, 0xF7};
    CHECK(!is_system_clock_global(longer));
    Bytes other_maker = global(0x00, 0x10, 0);
    other_maker[1] = 0x7D;
    CHECK(!is_system_clock_global(other_maker));
    CHECK(!is_system_clock_global({}));
    CHECK(!is_system_clock_global({0xB0, 0x10, 0}));

    // an old project: those records go, everything else stays in its order
    const Bytes keep1 = global(0x00, 0x40, 0), keep2 = {0xB0, 7, 100}, keep3 = Bytes(524, 0x55);
    std::vector<Bytes> recs = {keep1, global(0x00, 0x10, 0), keep2, global(0x00, 0x0F, 1), {}, keep3};
    CHECK(drop_system_clock_globals(recs) == 2);
    CHECK((recs == std::vector<Bytes>{keep1, keep2, {}, keep3}));
    CHECK(drop_system_clock_globals(recs) == 0);
    // through the container, as a saved project comes back
    auto saved = parse_container(build_container({keep1, global(0x00, 0x10, 0)}));
    CHECK(drop_system_clock_globals(saved) == 1);
    CHECK((saved == std::vector<Bytes>{keep1}));
}

TEST(state_part_dumps_upgraded)
{
    // E174: the Virus converts a version-9 part dump itself and resets the newer fields (Input Mode, unison,
    // envelopes 3/4 ...) - so the state keeps part dumps in the current format; the editor's values then reach it
    std::vector<uint8_t> d(524, 0);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x03, 9};
    memcpy(d.data(), head, sizeof head);
    d[523] = 0xF7;
    std::vector<uint8_t> stored = d;
    stored[7] = 0x01; // a stored bank patch (not an edit buffer): the firmware converts those itself
    std::vector<uint8_t> current = d;
    current[9] = 12;
    std::vector<std::vector<uint8_t>> recs = {d, stored, current, {0xB0, 7, 100}};
    CHECK(upgrade_state_dumps(recs) == 1);
    CHECK(recs[0][9] == 12);                       // the edit-buffer dump of part 4: upgraded
    CHECK(recs[1] == stored);                       // stored patch untouched
    CHECK(recs[2] == current);                      // already current
    CHECK(recs[3] == (std::vector<uint8_t>{0xB0, 7, 100}));
    CHECK(upgrade_state_dumps(recs) == 0);         // idempotent
}

TEST(state_missing_part_dumps_come_from_the_default)
{
    // audit #22: a truncated project (parts 3..16 missing) dropped every edit and patch load of those parts
    auto part = [](int p, uint8_t mark) {
        Bytes d(524, mark);
        const uint8_t head[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, (uint8_t)p};
        std::memcpy(d.data(), head, 9);
        d[523] = 0xF7;
        return d;
    };
    std::vector<Bytes> defaults = {{0xF0, 0x11, 0xF7}};
    for (int p = 0; p < 16; p++)
        defaults.push_back(part(p, 0x01));
    std::vector<Bytes> recs = {part(0, 0x22), part(1, 0x22), {0xF0, 0x42, 0xF7}};
    CHECK_EQ(add_missing_part_dumps(recs, defaults), 14);
    CHECK_EQ(recs.size(), 17u);
    CHECK(recs[0][100] == 0x22 && recs[1][100] == 0x22); // the project's own parts stay
    for (int p = 0; p < 16; p++) {
        int n = 0;
        for (auto &r : recs)
            n += is_part_dump(r, p);
        CHECK_EQ(n, 1);
    }
    CHECK_EQ(add_missing_part_dumps(recs, defaults), 0); // complete: nothing added
}

TEST(state_values_the_state_lacks_are_kept)
{
    // audit #24: a patch load rebuilt every value from the state; the System Clock the Virus reported (never in the
    // state, E445) went back to its default on screen
    std::map<int, std::map<std::string, int>> prev = {{-1, {{"Global/Clock", 2}, {"Global/Tempo", 90}}},
                                                       {0, {{"Osc/Shape", 10}}}};
    std::map<int, std::map<std::string, int>> next = {{-1, {{"Global/Tempo", 120}}}, {0, {{"Osc/Shape", 64}}}};
    keep_unset_values(next, prev);
    CHECK_EQ(next[-1]["Global/Clock"], 2);  // not in the state: kept
    CHECK_EQ(next[-1]["Global/Tempo"], 120); // in the state: the state wins
    CHECK_EQ(next[0]["Osc/Shape"], 64);
}

// R8-5: little-endian lengths (record and total) when the big-endian value has one of its top 10 bits set
TEST(container_little_endian_lengths)
{
    Bytes d = {'M', 'I', 'D', 'I', 11, 0, 0, 0, 3, 0, 0, 0, 0xB0, 7, 100, 0, 0, 0, 0};
    auto recs = parse_container(d);
    CHECK_EQ(recs.size(), 2u);
    CHECK(recs[0] == Bytes({0xB0, 7, 100}));
    CHECK(recs[1].empty());
    d[4] = 12; // total one longer than the data: nothing
    CHECK(parse_container(d).empty());
}

static Bytes single(int part, int origin)
{
    Bytes m(524, 0);
    m[0] = 0xF0, m[1] = 0x00, m[2] = 0x20, m[3] = 0x33, m[4] = 0x01, m[5] = 0x10, m[6] = 0x10, m[8] = (uint8_t)part;
    m[9] = 12, m[10] = 0, m[11] = 2, m[12] = (uint8_t)origin, m[523] = 0xF7;
    return m;
}

// R8-4: every single dump of a project is marked "eLIB" (byte 10 = 0x20) with both checksums recomputed
TEST(state_project_dumps_marked_elib)
{
    std::vector<Bytes> recs = {{}, single(0, 4), single(3, 7), {0xB0, 7, 100}};
    CHECK_EQ(mark_project_dumps(recs), 2);
    for (int i : {1, 2}) {
        CHECK_EQ(recs[i][10], 0x20);
        unsigned a = 0;
        for (int b = 5; b < 265; b++)
            a += recs[i][b];
        CHECK_EQ(recs[i][265], a & 0x7F);
        for (int b = 265; b < 522; b++)
            a += recs[i][b];
        CHECK_EQ(recs[i][522], a & 0x7F);
    }
    CHECK(recs[3] == Bytes({0xB0, 7, 100}));
}

// R8-6: the original removes page 0x6F parameter 0x7E (Input/Atomizer) records at load
TEST(state_input_atomizer_records_dropped)
{
    std::vector<Bytes> recs = {{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6F, 0x02, 0x7E, 0x01, 0xF7},
                               {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6F, 0x02, 0x7D, 0x01, 0xF7},
                               {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x72, 0x02, 0x7E, 0x01, 0xF7}};
    CHECK_EQ(drop_input_atomizer(recs), 1);
    CHECK_EQ(recs.size(), 2u);
    CHECK_EQ(recs[0][8], 0x7D);
    CHECK_EQ(recs[1][6], 0x72);
}

// R8-3: a Snow keeps the dumps and channel messages of its 4 parts only
TEST(state_snow_keeps_four_parts)
{
    std::vector<Bytes> recs = {{}};
    for (int p = 0; p < 16; p++)
        recs.push_back(single(p, 0));
    for (int ch = 0; ch < 16; ch++)
        recs.push_back({(uint8_t)(0xB0 | ch), 7, 100});
    recs.push_back({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x2D, 0x02, 0xF7}); // a global stays
    CHECK_EQ(keep_parts(recs, 4), 24);
    CHECK_EQ(recs.size(), 1u + 4 + 4 + 1);
    for (int p = 0; p < 4; p++)
        CHECK(is_part_dump(recs[1 + p], p));
    CHECK_EQ(recs[8][0], 0xB3);
    CHECK_EQ(recs[9][6], 0x73);
    CHECK_EQ(keep_parts(recs, 16), 0);
    // round trip: what a Snow saves loads back with the same 4 parts
    CHECK(parse_container(build_container(recs)) == recs);
}

static Bytes multi(uint8_t fill)
{
    Bytes m(267, fill);
    m[0] = 0xF0, m[1] = 0x00, m[2] = 0x20, m[3] = 0x33, m[4] = 0x01, m[5] = 0x10, m[6] = 0x11, m[7] = 0, m[8] = 0;
    m[266] = 0xF7;
    return m;
}

// R8-2: the start-up multi gets the project's Detune (137+p) and Output Select (185+p), like 0x180070520
TEST(state_startup_multi_takes_detune_and_output)
{
    Bytes fixed = multi(0x40);
    unsigned a = 0;
    for (int b = 5; b < 265; b++)
        a += fixed[b];
    fixed[265] = a & 0x7F;
    Bytes stored = multi(0x11);
    stored[265] = 0; // the original's state keeps the multi without its checksum
    CHECK(startup_multi(fixed, fixed) == fixed);
    CHECK(startup_multi(fixed, Bytes{0xF0, 0xF7}) == fixed);
    auto m = startup_multi(fixed, stored);
    for (int b = 0; b < 267; b++) {
        const bool taken = (b >= 137 && b < 153) || (b >= 185 && b < 201);
        if (b != 265)
            CHECK_EQ(m[b], taken ? 0x11 : fixed[b]);
    }
    a = 0;
    for (int b = 5; b < 265; b++)
        a += m[b];
    CHECK_EQ(m[265], a & 0x7F);
}

// R8-2: the editor shows the stored multi's Detune and Output Select (names from the model's template)
TEST(state_multi_routing_values)
{
    MidiMessage t;
    auto add = [&](int index, const char *path, int part) {
        MessageByte b;
        b.index = index, b.content = path, b.part = part;
        t.bytes.push_back(b);
    };
    add(121, "Multi Part Parameter/Transpose", 0);
    add(137, "Multi Part Parameter/Detune", 0);
    add(138, "Multi Part Parameter/Detune", 1);
    add(185, "Multi Part Parameter/Output Select Control2", 0);
    add(185, "Multi Part Parameter/Output Select Control3", 0);
    Bytes stored = multi(0);
    stored[121] = 70, stored[137] = 3, stored[138] = 5, stored[185] = 2;
    std::map<int, std::map<std::string, int>> parts;
    multi_routing_values(t, stored, parts);
    CHECK_EQ(parts[0]["Multi Part Parameter/Detune"], 3);
    CHECK_EQ(parts[1]["Multi Part Parameter/Detune"], 5);
    CHECK_EQ(parts[0]["Multi Part Parameter/Output Select Control2"], 2);
    CHECK_EQ(parts[0]["Multi Part Parameter/Output Select Control3"], 2);
    CHECK(!parts[0].count("Multi Part Parameter/Transpose")); // not restored at start-up by the original either
    parts.clear();
    multi_routing_values(t, Bytes(10, 0), parts);
    CHECK(parts.empty());
}

// R8-5: a project that does not parse is saved back unchanged until the instance's state changes
TEST(state_unread_project_kept)
{
    Bytes raw = {'M', 'I', 'D', 'I', 0, 0, 0, 50, 1, 2};
    std::vector<Bytes> now = {{}, {0xB0, 7, 100}};
    UnreadState u;
    CHECK(!u.saved(now));
    u.keep(raw, now);
    CHECK(u.saved(now) && *u.saved(now) == raw);
    auto edited = now;
    edited[1][2] = 90;
    CHECK(!u.saved(edited));
    u.clear();
    CHECK(!u.saved(now));
    u.keep({}, now); // nothing came: nothing to keep
    CHECK(!u.saved(now));
}
