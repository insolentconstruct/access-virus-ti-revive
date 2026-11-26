// state.h - the plug-in's project state (the original's "MIDI" container of SysEx/MIDI records), its editor-only
// records and the meta parameter value mapping of editordevice.xml. Pure functions, unit tested (tests/).
#pragma once
#include "model/remotedevice.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vti {

// "MIDI" + u32 BE length, then records of u32 BE length + message; like the original (0x180060080) a length whose
// big-endian value has one of its top 10 bits set is read little-endian, a declared total longer than the data loads
// nothing, and a record longer than the rest of the total ends the list
std::vector<std::vector<uint8_t>> parse_container(const std::vector<uint8_t> &d);
std::vector<uint8_t> build_container(const std::vector<std::vector<uint8_t>> &recs);
// a 524-byte single dump of `part` (F0 .. 10 00 <part>: edit buffer of that part)
bool is_part_dump(const std::vector<uint8_t> &m, int part);
// a truncated project lacks part dumps: each part without one gets the dump from `defaults` (the plug-in's default
// project), appended like the default keeps them after its multi; returns how many were added (audit #22)
int add_missing_part_dumps(std::vector<std::vector<uint8_t>> &recs, const std::vector<std::vector<uint8_t>> &defaults);
// values per part (-1: globals) rebuilt from the state: those the state does not carry (the System Clock the Virus
// reports, E445) are taken over from `prev`, else they fell back to defaults on screen at every patch load (audit #24)
void keep_unset_values(std::map<int, std::map<std::string, int>> &next,
                       const std::map<int, std::map<std::string, int>> &prev);
// E174: part dumps (edit buffers, bank byte 0) older than the current format are upgraded like the Virus does on
// receipt, so the editor shows and saves what the Virus plays; returns how many were upgraded
int upgrade_state_dumps(std::vector<std::vector<uint8_t>> &recs);
// E445: the globals System Clock/SA Freq, Frequency, Mode (0x0F, 0x10, 0x11, any part byte). Sent to the Virus they reach
// its USB chip, which can switch its audio clock behind the driver's back (E257); the rate belongs to the DAW's
// session, never to a project, and the original sends none of them (E327). So a project does not keep them
bool is_system_clock_global(const std::vector<uint8_t> &m);
// removes them from a project's records (a saved one may hold them), returns how many
int drop_system_clock_globals(std::vector<std::vector<uint8_t>> &recs);
// R8-4: like the original's setChunk (0x180079b50), every 524-byte single dump of a project gets the origin flag 0x20
// ("eLIB", E369: the Virus shows no stored location for a patch that may be edited) and both checksums afresh;
// returns how many were marked
int mark_project_dumps(std::vector<std::vector<uint8_t>> &recs);
// R8-6: the original removes 11-byte page 0x6F parameter 0x7E records (page D 126, Input/Atomizer, E276) at load;
// returns how many were removed
int drop_input_atomizer(std::vector<std::vector<uint8_t>> &recs);
// R8-3: the device type's part count (Snow 4, type table 0x1801c52f0): removes the edit-buffer dumps and channel
// messages of parts n and up, as the original saves and sends none of them; returns how many were removed
int keep_parts(std::vector<std::vector<uint8_t>> &recs, int n);
// R8-2: the original's start-up multi (`fixed`, 267 bytes) with the per-part Detune (bytes 137+p) and Output Select
// (185+p) of a project's stored multi and a fresh checksum, as 0x180070520 writes them at connect; `fixed` unchanged
// when `stored` is no 267-byte multi dump
std::vector<uint8_t> startup_multi(const std::vector<uint8_t> &fixed, const std::vector<uint8_t> &stored);
// R8-2: those two of a stored multi as part values (keys and part numbers from the model's VirusTI_MultiDump
// template `t`); the rest of the multi the original does not restore at start-up either
void multi_routing_values(const MidiMessage &t, const std::vector<uint8_t> &multi,
                          std::map<int, std::map<std::string, int>> &parts);
// R8-5: a project that does not parse is saved back as it came (the host would else store our default over it), as
// long as the instance's records stay what they were at that load (`now`); an edit after it saves the edit
struct UnreadState {
    std::vector<uint8_t> raw;
    std::vector<std::vector<uint8_t>> base;
    void keep(std::vector<uint8_t> d, std::vector<std::vector<uint8_t>> now)
    {
        raw = std::move(d);
        base = raw.empty() ? std::vector<std::vector<uint8_t>>{} : std::move(now);
    }
    void clear() { raw.clear(), base.clear(); }
    const std::vector<uint8_t> *saved(const std::vector<std::vector<uint8_t>> &now) const
    {
        return !raw.empty() && now == base ? &raw : nullptr;
    }
};
// editor-only per-part settings (link buttons): F0 7D 'V' 'T' 'N' 01 <part> <path> 00 <value> F7
std::vector<uint8_t> editor_record(const std::string &path, int part, int value);
bool parse_editor_record(const std::vector<uint8_t> &m, std::string &path, int &part, int &value);
// meta value -> client value (maps first, else a * meta + b) and back (false: no map entry / not invertible)
int meta_to_client(const MetaClient &c, int mv);
bool client_to_meta(const MetaClient &c, int cv, int &mv);

} // namespace vti
