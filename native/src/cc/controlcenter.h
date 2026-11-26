// controlcenter.h - the core of TI Revive's Control Center (the standalone replacement of Access' "Virus Control
// Center"), shared by the CLI (tools/tirevive-cc) and the app (ccapp/).
//
// It reads with requests whose behaviour is documented in findings/evidence.md - standard USB GET_DESCRIPTOR (E115),
// bank info 39 -> 14 and single dump request 30 -> 10 (E027, E031, E037). The one write is restore_ram: single dumps
// into a RAM bank, the messages the plug-in's browser sends (checked against the original, E294-E303) - never the
// original Control Center's restore, which writes the Virus' memory and flash through USB vendor requests (E313).
// Firmware/OS updates, "Burn To Flash", factory reset and device renaming are out of scope for good: they write the
// Virus' flash and can brick it (AGENTS.md rule 2).
#pragma once
#include "../device/broker.h"
#include "../device/memsync.h"
#include <functional>
#include <string>
#include <vector>

namespace vti::cc {

struct DeviceInfo {
    uint16_t vendor = 0, product = 0, bcd = 0;
    std::string manufacturer, product_name;
};

// The USB identity of the connected Virus through the broker (no serial number: it would end up in bug reports).
bool read_device_info(Broker &broker, DeviceInfo &info, std::string &error);

// May bulk work (a backup, a restore, the bank list: loops of dump requests or dumps) start? The original Control
// Center refuses to run while its plug-in has the Virus (R11-1, E596); ours shares the Virus with the plug-in, where
// two requesters overlap and a plug-in note can end a dump in pieces. False with `error` (a message for the user)
// while a plug-in owns the Virus, and when the broker does not say (older than this Control Center). Device info and
// Nerd mode do not need it.
bool virus_free(Broker &broker, std::string &error);

// The patch name of a 524-byte single dump (10 characters, trailing blanks removed).
std::string patch_name(const Dump &d);

// The plug-in's bank cache (the same folder, so a backup only fetches what changed since the plug-in's last sync).
std::string cache_dir();

// Default backup folder: <Documents>/Access Music/Virus TI/Backups/<YYYY-MM-DD HHMM>.
std::string default_backup_dir();

// Reads RAM banks 1..4 (`banks`, e.g. "1234") - cached programs whose fingerprint still matches are not requested
// again - and writes "RAM A.mid".. to `out_dir` in the original's library format (readable by the TI Revive plug-in
// and Access' Virus Control). progress(bank, program, fetched) per program. `stop` (optional) cancels between two
// requests (what was fetched stays in the cache, no library is written for the bank). Refused while a plug-in owns
// the Virus (virus_free), checked before each bank.
// Not a snapshot (about 2 min a bank, R11-4): after a bank it asks the bank info once more and reads the programs
// whose fingerprint changed meanwhile (a store on the panel or by a plug-in) again, once; when they still differ, or
// the bank info gets no answer, the library is written all the same and `warning` (optional) says it may be mixed.
bool backup_ram(Broker &broker, const std::string &banks, const std::string &out_dir,
                const std::function<void(int, int, bool, const Dump &)> &progress, std::string &error,
                const std::atomic<bool> *stop = nullptr, std::string *warning = nullptr);

// One write of a RAM restore: the backup's patch addressed to (bank, slot), checksums recomputed.
struct RestoreWrite {
    int slot = 0;
    Dump dump;
};
// What restoring RAM bank 1..4 from `backup` (a library's dumps by position, e.g. a backup's "RAM B.mid") writes:
// the patches whose bytes differ from `current` (the bank as the Virus has it; bank/program bytes and checksums
// left out of the comparison), or that `current` does not know. Empty slots and anything but a single dump in the
// backup are left alone. Empty for another bank.
std::vector<RestoreWrite> restore_plan(int bank, const std::vector<Dump> &backup, const Bank &current);
// The RAM bank a library's file name stands for: "RAM A".."RAM D" as a word anywhere in it (any case), else 0.
int bank_from_name(const std::string &stem);
// A single dump as it goes out: USB-MIDI on cable 3, 64 bytes per write, like the plug-in (dumppacer.h, E270).
std::vector<std::vector<uint8_t>> dump_pieces(const Dump &d);
// What a restore did besides the count (for the caller's message).
struct RestoreReport {
    std::vector<int> slots; // the programs written and read back, in order
    std::string backup;     // the library saved before the first write (its name, no folder; "" = nothing to write)
    bool confirmed = true;  // false: the broker reports no writes (older than protocol 6), so none was confirmed
};
// Restores RAM bank 1..4 from `backup`: brings the bank cache up to date (bank info + changed programs), saves that
// bank as the library "RAM X before restore <date time>" in the Backups folder (rule 2: backed up first, R11-3; when
// it cannot be saved nothing is written), writes the programs restore_plan names (each in pieces 10 ms apart, like the
// plug-in), waits for the broker's report that each went out (R11-7, bounded), reads each one back and compares, and
// saves the cache. progress(done, total) per written program; `written` = programs written. False with `error` when
// a plug-in owns the Virus (virus_free, checked before each program too), the Virus does not answer, the broker did
// not send a piece or does not report it, a read-back differs, or `stop` cancels ("cancelled"); once something may
// have been written, `error` also names the programs written and the library (no folder: it goes into logs, rule 5).
bool restore_ram(Broker &broker, int bank, const std::vector<Dump> &backup,
                 const std::function<void(int, int)> &progress, int &written, std::string &error,
                 const std::atomic<bool> *stop = nullptr, RestoreReport *report = nullptr);

// Writes the dumps as one .syx file (the single dumps back to back, as patch librarians expect).
bool write_syx(const std::vector<Dump> &dumps, const std::string &path, std::string &error);

} // namespace vti::cc
