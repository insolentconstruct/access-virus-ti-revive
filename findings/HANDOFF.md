# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-22: HANDOFF trimmed to the current state (0.8.42/0.8.43 release history, done NEXT items and closed issues moved to log/2026-11.md); #41 library save cause E576 fixed by the reporter, our fix eb55734 waits on the Linux smoke test; rc3 waits on #37.

## Where things stand

- **0.8.43 Latest** since 2026-11-21 11:45 UTC (release CI 44900063944 all green incl. the release job, 8 assets,
  site rebuilt by hand); 0.8.42 and 0.8.43 release details in log/2026-11.md (2026-11-22 entry).
- **Round 0.8.44** on branch round-0.8.44: #37 marker fix (E568, rc1) and Play PRE-SHIFT (E574, v0.8.44-rc3, tag
  3ffe6b2, CI 44949570657) out as pre-releases; #41 silent library-save fix eb55734 (E576).
- Our TI2 Desktop: ON THE MAC since 2026-11-22 16:40 (user moved it for the #41 Logic test); it was FROZEN on arrival and needed a power cycle (cause open: at 16:40 only unit/GUI tests ran on Linux). ctest gui_duplicate_editors fails on Linux without it (present 0), rerun when it is back. Back from a VM:
  VM=win11 scripts/vm.sh detach, then systemctl --user start tirevive-broker.socket. User flashed ROM Z
  on it in early November (E500).
- Linux: DEV build of round-0.8.44 with E577/E578 in ~/.vst3 (plug-in 2b2cd1e3; before 79dd2443 at f47d3e8) for the library-save smoke test
  (eb55734), WAITING ON THE USER. Previous plug-in ce68817b (rc3 code) backed up in work/plugin-backup-ce68817b,
  My Patches.mid backup in work/lib-test; rc2 backup work/rc2-plugin-backup.
- win11 VM: 0.8.43 release (setup /quiet exit 0, VST3 e36f8e6c), shut down (use Stop-Computer: virsh shutdown is
  ignored). rc1/rc3 not installed there. TUNED 2026-11-14 (E533): vCPUs pinned to P-cores
  4-7, emulator 2-3, topology 1/2/2, no emulated sound / SPICE USB redirect / balloon, evmcs+avic off, ISOs off;
  guest High performance, USB selective suspend off (AC). Backup: work/vm/win11-before-tuning-20261114.xml.
  2026-11-19: emulated sound card BACK (ich9 -> SPICE audio, user wanted sound in the VM);
  before hil-win timing runs consider removing it again (backup without it: work/vm/win11-before-sound-20261119.xml).
  Before VM timing tests: `sudo cpupower frequency-set -g performance` (user; on since 2026-11-15 08:10).
  win10 VM: snapshot `pre-i36` (0.8.36). No spare USB controller for VFIO (user: no extra hardware for now).
- Mac (macOS 27.2.1): runs pkg aba91689 (0.8.43 + E566 fix; hil-mac ALL PASS). The 0.8.43 RELEASE folder
  work/rel-0.8.43/mx/TI-Revive-0.8.43-release (pkg aded39c3) is not pushed yet (Mac asleep): scripts/mac.sh push to
  ~/Downloads when awake, the user installs. rc1/rc3 not pushed (no SSH answer). Option files there: live-later.on
  (left from M2), klog.on, noteprobe.on, live-notes.on, diag.on.
- Private-data check (user 2026-11-15): `.claude/hooks/private-data.py` runs as git pre-commit/commit-msg/pre-push
  hook (installed here with `--install`, worktrees share it) and as Claude PreToolUse hook for `gh` posts; personal
  words in the git-ignored work/private-patterns.txt. A block means: move it to work/HANDOFF-private.md. 33 unit tests (93633f8: heredocs count only for gh stdin bodies).
- Branches: `libusb-audio` and `libusb-catalina` (worktree ../virus-ti-re-catalina) are merged into round-0.8.42 and
  done (dev-mac there = the Catalina build; on the round dev-mac stays the normal build), `catalina-test` (old).

## Open issues (checked with the user 2026-11-22; check GitHub only together with the user)

| Issue | Topic | Waits on | Last from us |
|---|---|---|---|
| #32 | (no change since 11-10) | reporter | before 11-10 |
| #47 | NEW 2026-11-18: Snow "connected but not found" in Ableton; form says Windows 11 / TI Desktop, text macOS 27.0.1 / Snow; no logs, no version (E134 or E115 pattern?) | reporter: platform, model, version, Access/AURA, diag.log, broker log, ioreg, MIDI Drivers ls (issuecomment-7003657269) | 2026-11-19 |
| #37 | libusb pkg on Catalina (E561). Marker fixed in rc1 (E568/E569). Play burst: rc2 no help (E571); burst >= reported latency (E572); original same, Access docs 2 bars pre-roll (E573); rc3 = PRE-SHIFT at Play (E574: REAPER 256 perfect, 2048 ok), posted 2026-11-21 (issuecomment-7069327555) | reporter: rc3 test (Play twice, small + large buffer, log) | 2026-11-21 |
| #38 | macOS 27 Live: AU/editor crash = SOLVED by the reporter (Access .pkg was missing; plug-in opens; Virus not connected yet) | reporter: note after connecting the Virus, then close (issuecomment-6948868845) | 2026-11-17 |
| #40 | (ours) macOS libusb audio as an option? | #39 reporter; #37 = first real data point (works) | 2026-11-13 |
| #41 | (the #30 reporter, Snow, Logic) Multi-mode wish: NOT BUILT for now (user; E575, history in log). Low-latency wish answered (D button / Live Mode / Plugin Latency), asked to try. Library save: CAUSE (E576) deny-everyone ACL from a Time Machine copy; chmod -R -N fix (issuecomment-7078878581) WORKS (reporter 12:13); maintainer asked 'issue solved?'. Our silent-failure FIX eb55734: commit_library (memory changes only when the file was written, test), library_saved logs every library write + LCD 'Library not saved:' (SAVE menu, edits, Create Empty Library). Built + 875 tests on Linux; NOT run in a DAW yet | reporter: confirm solved (then close jointly); us: Linux smoke test of eb55734 | 2026-11-22 |
| #43 | original TI, OS 5.1.7, macOS: `tirevive-cc restore` stops at a fixed slot ("another patch after the restore"). CAUSE (E549): byte 10 (patch flags, E156) is not kept by the Virus on a RAM store, the read-back compared it; Rom A slot 7 = 0x76. FIX on round-0.8.42 (controlcenter.cpp patch_difference: byte 10 left out, a real mismatch names the byte; 3 new tests); original checked with REA (stores byte 10 unchanged, no read-back). Not run on hardware. rc6 RELEASED (pre-release, tag e84ded1, CI 44121551201 all 9 jobs green, 7 assets, 20:45 UTC); reply posted. Mutation on controlcenter.cpp not done (the copy ../tirevive-mut fails unmutated, rebuild there) | reporter: test rc6 (posted issuecomment-6960600649) | 2026-11-17 |
| #42 | Snow, M4 Pro, macOS 15.8.2: vtibroker ~33 % CPU idle; cause E547 (Snow idle stream on bulk EP 0x85, ~3300/s). rc5 TESTED by the reporter (E550): ~830/s, 15-19 %, knobs fine; 33 sync reports in 6 min (asked if audible). Reply with why + how the original does it (E548) posted (issuecomment-6957394873). NEW (user 2026-11-17): option snow-idle-slow (off by default, Control Center Options; 4 ms after 1 s quiet; broker reads the flag file every 2 s; tests first; not run on a Snow). Sim confirmed from the Snow's USB chip code (E551, S147): idle packet armed on every read, no command switches it off 2026-11-18 reporter: no dropouts or timing problems in a quick Ableton test | reporter: 0.8.42 with the option on, CPU (issuecomment-7020102746) | 2026-11-19 |
| #51 | NEW 2026-11-20: Windows 11, Apollo Twin as DAW interface (two clocks, ~90 ppm): click every ~2 min = read pulled back when err passes the 512 margin (E564) | reporter: setx VTI_NATIVE_MARGIN 1024 test + full diag.log (issuecomment-7044953063); us: TimelineFollow fix in a round | 2026-11-20 |
| #48 (dup #44) | M1, Sequoia, Ableton, TI2 Keyboard, 0.8.42: distorted after reopening a saved project, only through the Mac (unit outputs silent = parts on USB), output device a UMC1820 (two clocks), buffer change worse, project reload at 11:40 local fixed it. Log of 07:34: transport clean like #33 (E532), no AURA conflict, one full-scale window before a reload (E559); cause open | reporter: new log covering 11:35-11:40, UMC1820 sample rate (issuecomment-7038034557) | 2026-11-20 |
| #45 | NEW: two Viruses (TI Keyboard + TI2 Desktop) at once, one plug-in instance each, in Live. TI Revive: one Virus only (first found). Original 5.1.7 plug-in: device list, one owner instance per Virus (E553, static; driver side unknown). Replies posted (not yet, later round, asked OS + can they test: issuecomment-6980767270; follow-up with E553, asked if they used two with the original: issuecomment-6981406654) 2026-11-18 reporter (French): two editors in Ableton were always buggy with the original; will beta test; asked OS again in French (issuecomment-6984586414). DECIDED: maintainer posted 2026-11-18 it is on the list, first test build to them by Fri/Sat (2026-11-20/21) IN WORK (round-0.8.42, user 2026-11-18: one broker per Virus, assignment like the original, all 3 OS, numbered MIDI ports, ASIO stays Virus 1): devslot.h/winslot.h (-slot N, -N names, key = USB port), slotpick.h (owner per Virus, empty slot 2+ given up after 3 s, project record TIRevive/Virus), RAM cache virus-N, Mac Core Audio by USB serial in UID (UNVERIFIED), systemd tirevive-broker@N. Review findings 1-7 fixed (findings/audit-2026-11-18-multidevice.md); dev build installed on Linux with tirevive-broker@2-4 (systemd starts broker 2: checked), hil.sh ALL PASS (H1, H2x2, H3, H6). Never run with two Viruses; mismatch question not done | reporter: test 0.8.42 with both Viruses (released 2026-11-19, issuecomment-7020102331) | 2026-11-19 |

Closed issues and their history: log/2026-11.md (2026-11-22 entry). Quiet-issue rule: 7 days after our last
request, close with the AGENTS.md message, only in a joint check with the user (#40 is ours: no closing).

## Not verified on hardware

Full list: `python3 -I scripts/evidence.py list --no-hardware` (libusb stall level 2, Windows error 433, setup
unplug step on the Access driver, Mac "Virus TI MIDI" played through, Keyboard/Polar unlink, Mac pkg refusing < 14.4).

## Still open from round 0.8.42 (the full list moved to log/2026-11.md, 2026-11-19)

- Linux MIDI ports: no message seen through them yet (Desktop has no keys; the sim's virtual Keyboard could).
- Update check: not run on Windows (curl.exe via CreateProcess). hil-win (LOW): H1's hung first audio read after
  power-on + attach; drift limit 100 ppm vs 11 s items.
- Proposed (sim list): freeze guard (73 00 10 vv vs stream rate), single re-sync after a detected +64 (E492/E498),
  burst guard, fewer broker resets (E495), lower safety-net floor.

## Standing notes

- Weekly releases: one round per week, one test run (risk.py, hil.sh, checklist artifact via scripts/checklist.py). Raise native/CMakeLists.txt VERSION at every round start.
- Coverage: `scripts/coverage.py` (recipe in `findings/testing.md`). Mutation: `risk.py` names the files for
  `scripts/mutation.py`.
- Never `nativehost --stress*` on the real Virus at small blocks (E438). Never `virsh screenshot` with a Virus in a VM.
- One VM at a time (30 GB host). The tirevive-hil task in win11 runs hil-win in the console session; Live or the
  Control Center open in the VM make it stop ("close first").
- Xvfb grabs of the Control Center stay black (GTK, no WM): check its look on the user's desktop.
- Stop hook (handoff-check.sh + handoff-turn.py): a turn that edits the repo, commits, pushes, posts on GitHub or
  starts CI without touching this file is stopped once to update it.

## NEXT

1. #41 NEW 2026-11-22 14:01: 0.8.43 Mac, Logic 13, bus recording 32 ms BEFORE the grid (I/O 32, Processing small); no latency-path change 0.8.42-rc3..0.8.43 (E546 stale-latency kind suspected); asked log after one take + sample rate + I/O 128->32 test (posted 2026-11-22). Reporter 15:23: I/O 128 -> 32 FIXED it = Logic kept a stale latency (E546 kind; same pattern #37/#33). Posted issuecomment-7081427381 (nudge planned, buffer change = workaround). DONE in code: one-time latency re-announce ~1 s after the first activation (LatencyWatch::nudge, 890 tests; Linux: needs the host's run loop, REAPER has one); CI 45040270538 (6014095) all green; Mac package pkg 680a131b pushed to the Mac ~/Downloads/TI-Revive-0.8.44-nudge (INSTALL-COMMAND + Install test build.command), installed (60140954). Logic test 17:02-17:04: project opened BEFORE the Virus was ready: plug-in reported 3085 (= E546's stale value), nudge fired at 3085 (wasted), Virus connected -> 2816 announced by the old watch, Logic read it; take #05 all 16 beats 0.00 ms (on grid). Chris's stale case not reproduced on our Mac. DONE 1d3b029: nudge once ~1 s after the latency settled and the Virus' audio is open (audio_ready), 892 tests; CI 45042384835 running, Mac package next; then repeat the Logic test (fresh start at I/O 32), then rc5 (crossfade E579 + guard fix + nudge) for #41/#37/#51 for a Logic test on our Mac (bus recording at I/O 32 after project open). Earlier: library-save smoke test of eb55734 (Linux dev build f47d3e8, REAPER, 2026-11-22): Create Empty Library x2 logged 'created' + file written; drag-drop into a library logged 'patch(es) dropped', file 26 -> 1080 bytes; Patches folder read-only: 'NOT saved: cannot write ...mid.tmp' twice, file unchanged; folder writable again. User: LCD showed 'Library not saved:', the dropped patch disappeared from the browser = PASS. SAVE-menu path into a slot not tried. Test files New Library.2mid.mid / New Library3.mid to delete.
2. #37: v0.8.44-rc3 WAITS for the reporter (Play twice, small + large buffer, log). Note at Play stays ~burst late
   (unavoidable, Access docs: 2 bars pre-roll, E573). #37 AU cache installer step if the reporter confirms the stale
   AU cache. Not done: rc1/rc3 on the win11 VM and the Mac.
3. Release text: 0.8.44 "New in" in build.yml + What's New (infopage.h) written 2026-11-22 (marker, Play pre-shift, library saves, Ventura confirmed); update the library line after the smoke test and add anything later in the round.
4. #51 (E564) FIX in code (E578, model: 0 pull-backs; TI2 --ppm runs no regression, two-clock case not on hardware) + first-Play jumps at block < 240 FIXED (E577, SETTLE = AT_ONCE, TI2 A/B). hil.sh ALL PASS (work/hil/20261122-140302), 879 tests. User REAPER retest at 128 PASSED 2026-11-22 (plug-in 2b2cd1e3 in ~/.vst3: 'works perfect, no jumps'; log: Play 1 26 pull-backs inside the burst, 3 later Plays clean). v0.8.44-rc4 TAGGED on a7118fe (annotated), release CI 45028420320 ALL GREEN (11 jobs, pre-release, 8 assets); posted #37 issuecomment-7080088942 (mac-libusb, Play twice, small + large buffer, log) and #51 issuecomment-7080089172 (setup.exe, remove VTI_NATIVE_MARGIN, 15-20 min, diag.log). Waits on both reporters. Not done: rc4 on win11 VM / Mac. 2026-11-22 later: user heard a short metallic sound at moments = host xruns (REAPER lost 4 blocks; 1-frame catch-up steps ~375/s), NOT caused by rc4 (old builds same on the TI2); fixed E579: crossfaded catch-up (FADE 192, 128-frame raised cosine, crossfade.h), also for the non-Play offset jumps; two #51 guard bugs fixed. 888 tests, hil.sh ALL PASS, TI2 xrun runs 0 steps/0 hard jumps. Installed for the user's REAPER test (planned with ALSA hw:3,0: pactl set-card-profile alsa_card.pci-0000_07_00.0 off, back with output:analog-stereo+input:iec958-stereo). Open: our process() 6022 us at block 64 during RAM bank sync (lock?). Then rc5 for #37/#51.
5. Issues: quiet-close candidates #38 (11-17) and #43 (11-17) from 2026-11-24, in a joint check; #32: look up its
   date first. Still to read: replies from #45 (two real Viruses: first real-world test), #42 (snow-idle-slow CPU),
   #44 (recall), #47, #43, #37.
6. Round to discuss with the user: Windows MIDI ports, test automation, Linux MIDI with the sim's Keyboard.
   Wish (#39, timemachine70 2026-11-20, answered issuecomment-7039532288): resize the plug-in window by dragging
   (today only right-click > Size 100-200 %, viruseditor.h canResize false); a later round.
7. Open from round 0.8.42: the 53 ms stall on the sim's Virus 2 (sim wants a rerun with VTISIM_WARMUP=1); two real
   Viruses, and two on Windows/macOS, untested (Mac Core Audio pick by USB serial unverified); Live on macOS may host
   waiting instances in another process ("another program's plug-in" lines, not checked); W2's largest ASIO buffer not
   in the log; Windows MIDI ports wait for Windows' own MIDI Services (end of November update). #50 follow-up: fix
   core_audio_pick from the next log (E563: second start fails; AudioDeviceStart on two Virus devices in one process).
8. Proposals accepted 2026-11-19 (test automation): hil-mac started over SSH with `open` (try once), MIDI port checks
   in hil.sh / hil-mac, one install script for test builds, REAPER runs via scripts/reaper-open.sh (started), smart
   plug for H4 (hil.sh --power exists; user decides on the hardware).
