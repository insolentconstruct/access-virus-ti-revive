# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-25: rc9 (v0.8.44-rc9 at d255cc1, CI 45626198935 green) = rc8 + E591/E592 lost-answer fixes; release text + What's New updated; posted #51/#48/#37; installed Linux + win11, Mac package pushed (user installs). #48: Virus on a USB3 hub that must stay, answered why reopening helps. #37 design A+B is IMPLEMENTED since rc8 (d6ff162, option 'Clean first notes after Play'), nothing waits on a test review.

## Where things stand

- **0.8.43 Latest** since 2026-11-21 11:45 UTC (release CI 44900063944 all green incl. the release job, 8 assets,
  site rebuilt by hand); 0.8.42 and 0.8.43 release details in log/2026-11.md (2026-11-22 entry).
- **Round 0.8.44** on branch round-0.8.44, latest pre-release **v0.8.44-rc7** (tag on the release-text commit after e8d463c; rc6 fe65ab5): marker fix (E568), Play
  pre-shift (E574), first Play at small blocks (E577), #51 two-clock guard (E578), xrun crossfade (E579), Logic
  latency nudge (E580), burst learning fix (E581), library-save logging (E576). 897 tests, hil.sh ALL PASS. After rc6: E582 (Logic burst room + crossfaded rest), E584 (Logic loss paid twice: window waits beyond +JUMP); 915 tests; in rc7. Mutation timelinefollow.h: 88/119 + the E582/E584 kills, every survivor left equivalent.
- Our TI2 Desktop: back on Linux (2026-11-22 evening, broker socket active); it was on the Mac 16:40-~18:00 for the #41 Logic test and was FROZEN on arrival there (power cycle; cause open). User flashed ROM Z on it in early November (E500). From a VM: VM=win11 scripts/vm.sh detach, then systemctl --user start tirevive-broker.socket.
- E591 fixes A-E DONE (E592, user 2026-11-24 'implement all'): efcbd2e (A-C), df67e27 (D broker), E commit (re-send cables once, then display warning). 946 tests; TI2 hil.sh ALL PASS + --idle-first clean with the new plug-in AND new broker (installed in ~/.local/lib/tirevive, backup vtibroker.bak-rc8). Not run: a Virus that really stops answering, D on macOS. rc9 (user 2026-11-25 go): release text + What's New for E591/E592 added, tag v0.8.44-rc9; CI 45626198935 green (11 jobs, pre-release 8 assets); posted #51 -7166973072, #48 -7166973580, #37 -7166974106 (2026-11-25). Linux: rc9 by install.sh (plug-in 11c8f137, broker 756b6fe9). win11: MSI /quiet exit 0 (VST3 fd22e21a), VM shut down. Mac: pkg 95e67abe + INSTALL-COMMAND.txt + "Install test build.command" in ~/Downloads/TI-Revive-0.8.44-rc9, user installs. Waits: edy013 (#51 RAM D fill), encoderaudio (#48 rc9; hub must stay, answered 11-25 why reopening helps + hub-port tip; open: check whether a session reopen also restarts the broker MIDI IN read), packman83 (#37 rc9 Logic).
- Issues 2026-11-24 late (with the user): #37 asked for rc8, #48 asked (no SysEx replies in exactly the distorted sessions, cured by a Virus off/on?), #40 answered, #43 + #38 closed. Open: 53 51 48 47 45 42 40 37 32.
- Issues 2026-11-25 (with the user): only #48 new (Virus on a USB3 hub, nothing seen at the drop) -> posted: connect directly, retest (issuecomment-7164087174). #47 + #45 reach 7 quiet days on 11-26, #53 on 11-30.
- REAPER test of the rc8 code DONE 2026-11-24 (log): keep-rest on = 0 fades / 0 jumps at 512, 2048 and with Anticipative FX (Logic-like 12.4k read-ahead, 1 Play only; off: fades + blocks pulled back); duplicate-track takeover 2 s. Maybe 4-5 more Anticipative Plays.
- Linux: ~/.vst3 = dev build 10e55b71 (9a87ec5: rc8 + E592 fixes; broker c5441638 installed), for the user's REAPER test 2026-11-24; REAPER 7.86 portable, 7.83 backup in ~/.local/opt/dl/reaper/backup-7.83-20261124); rc7 code 52fa05f5 in work/plugin-backup-rc7-52fa05f5; 40329e7a in work/plugin-backup-rc6b-40329e7a, rc6 code in work/plugin-backup-rc6-85bf0974. Backups: work/plugin-backup-ce68817b (rc3 code),
  work/rc2-plugin-backup, My Patches.mid in work/lib-test. ALSA test: Sound Core3D off in PipeWire with
  pactl set-card-profile alsa_card.pci-0000_07_00.0 off, back with output:analog-stereo+input:iec958-stereo.
- win11 VM: 0.8.43 release (setup /quiet exit 0, VST3 e36f8e6c), shut down (use Stop-Computer: virsh shutdown is
  ignored). rc1-rc6 not installed there. TUNED 2026-11-14 (E533): vCPUs pinned to P-cores
  4-7, emulator 2-3, topology 1/2/2, no emulated sound / SPICE USB redirect / balloon, evmcs+avic off, ISOs off;
  guest High performance, USB selective suspend off (AC). Backup: work/vm/win11-before-tuning-20261114.xml.
  2026-11-19: emulated sound card BACK (ich9 -> SPICE audio, user wanted sound in the VM);
  before hil-win timing runs consider removing it again (backup without it: work/vm/win11-before-sound-20261119.xml).
  Before VM timing tests: `sudo cpupower frequency-set -g performance` (user; on since 2026-11-15 08:10).
  win10 VM: snapshot `pre-i36` (0.8.36). No spare USB controller for VFIO (user: no extra hardware for now).
- Mac (macOS 27.2.1): runs the test build pkg f4092089 (1d3b029, settled nudge; ~/Downloads/TI-Revive-0.8.44-nudge2), not rc6. Before: pkg aba91689 (0.8.43 + E566 fix; hil-mac ALL PASS). The 0.8.43 RELEASE folder
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
| #37 | Logic on Catalina (libusb pkg, E561), rc6. Reporter 2026-11-22: crackle on the FIRST notes wherever Play starts, same at I/O 128/512; log work/issue37-rc6/...-2246.txt. Cause (E582): Logic's burst varies ~1200 frames between Plays -> pull-backs + hard jumps + steps at each Play. rc7 (E582+E584): reporter 2026-11-23 'much better than rc6'; left: the post-Play crossfade lands on the first notes (2048: shift ~11800 then 400-2300 crossfaded; 512: Logic burst 5300-7400) + 138-174 1-frame steps after Plays (metallic) + 43/139 pulled back at 22:48:43/22:49:03; 1 bar lead-in = clean. Reply 7123137920 | us: smooth the leftover steps, move the post-Play correction off the first notes | 2026-11-22 |
| #38 | macOS 27 Live: AU/editor crash = SOLVED by the reporter (Access .pkg was missing; plug-in opens; Virus not connected yet) | reporter: note after connecting the Virus, then close (issuecomment-6948868845) | 2026-11-17 |
| #40 | (ours) macOS libusb audio as an option? | #39 reporter; #37 = first real data point (works) | 2026-11-13 |
| #53 | NEW 2026-11-22: M1 Max, Sequoia 15.8.3, latest OS + TI Revive: every note distorted with an echo/repeat, some lag; asks if ROM banks can load automatically at start. User answered 2026-11-23 (power cycle after the first bank sync, else plug-in log). Echo/repeat may be the FAQ "every note sounds twice" case (E530/E531); ROM question not answered yet | reporter: log | 2026-11-23 |
| #43 | original TI, OS 5.1.7, macOS: `tirevive-cc restore` stops at a fixed slot ("another patch after the restore"). CAUSE (E549): byte 10 (patch flags, E156) is not kept by the Virus on a RAM store, the read-back compared it; Rom A slot 7 = 0x76. FIX on round-0.8.42 (controlcenter.cpp patch_difference: byte 10 left out, a real mismatch names the byte; 3 new tests); original checked with REA (stores byte 10 unchanged, no read-back). Not run on hardware. rc6 RELEASED (pre-release, tag e84ded1, CI 44121551201 all 9 jobs green, 7 assets, 20:45 UTC); reply posted. Mutation on controlcenter.cpp not done (the copy ../tirevive-mut fails unmutated, rebuild there) | reporter: test rc6 (posted issuecomment-6960600649) | 2026-11-17 |
| #42 | Snow, M4 Pro, macOS 15.8.2: vtibroker ~33 % CPU idle; cause E547 (Snow idle stream on bulk EP 0x85, ~3300/s). rc5 TESTED by the reporter (E550): ~830/s, 15-19 %, knobs fine; 33 sync reports in 6 min (asked if audible). Reply with why + how the original does it (E548) posted (issuecomment-6957394873). NEW (user 2026-11-17): option snow-idle-slow (off by default, Control Center Options; 4 ms after 1 s quiet; broker reads the flag file every 2 s; tests first; not run on a Snow). Sim confirmed from the Snow's USB chip code (E551, S147): idle packet armed on every read, no command switches it off 2026-11-18 reporter: no dropouts or timing problems in a quick Ableton test | reporter: 0.8.42 with the option on, CPU (issuecomment-7020102746) | 2026-11-19 |
| #51 | Windows 11, Apollo Twin (two clocks ~90 ppm): clicks = pull-backs at the margin (E564); fix E578 + guard fix + xrun crossfade (E579), rc6 posted (issuecomment-7083119458) | reporter: rc6, remove VTI_NATIVE_MARGIN, 15-20 min, diag.log | 2026-11-22 |
| #48 (dup #44) | M1, Sequoia, Ableton, TI2 Keyboard, 0.8.42: distorted after reopening a saved project, only through the Mac, UMC1820 (two clocks) at 44.1 kHz (reporter 2026-11-23); reloading the SESSION (not the plug-in) fixes it. Log of 07:34: transport clean (E532), one full-scale window before a reload (E559); cause open. Log 2026-11-23 20:27 (work/issue48-1123, CC 0.8.42): 8 plug-in starts 11-20/11-23, audio complete and in step, no peak near full scale = no distorted stretch in it; first start 'Virus ready after 11.5 s (no reply to the probe)'. Asked: when distorted, update to rc7, log right after | reporter: time of the distortion, rc7 | 2026-11-23 |
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

0. **Wait on rc9 (2026-11-25):** #51 edy013 (RAM D fills? handover line if the double instance returns), #48 encoderaudio
   (rc9 with the hub; open for us: does a session reopen also restart the broker's MIDI IN read, or only the plug-in's
   connection), #37 packman83 (rc9 in Logic: Record Enable, Small, I/O 256, 5 Plays on the notes, log). Mac: user runs
   the installer in ~/Downloads/TI-Revive-0.8.44-rc9. Not run anywhere: a Virus that really stops answering, the broker
   reset (D) on macOS/Windows, rc9 loaded in a DAW.
0b. **Code review fixes (2026-11-25) done:** 69 review items, 62 fixed, 7 skipped with reason (work/review-todo-
   2026-11-25.md); commits f609a75, beb1df0 (E593), 92af40a, debc889, 0654c5e; vtitests 963 + brokertests 4 + vlatests 3
   pass on Linux. Not run anywhere: Windows broker pipe DACL + first-instance (B4-B6) and the client pipe owner check (D17,
   owner = user/SYSTEM/Administrators) -> win11 VM, normal + elevated DAW, before the release; ASIO stop-from-callback (D3); macOS broker/keyport (B8). Left:
   directmon.h `which` param (needs plug-in caller), plug-in still waits 3 s on Broker::audio (late replies now dropped),
   store_patch refusing an unsynced RAM bank ("Not saved") is kept, decided by the user 2026-11-26.
0c. **Research original vs ours (2026-11-26): ALL FIXED, nothing run on hardware.** 12 static agents
   (work/research-2026-11-26/SUMMARY.md, R1-R12.md), then T1-T23 one worker at a time (work/fix-todo-2026-11-26.md:
   23 resolved with summary, 9 kept with reason), commits 01aceeb..HEAD, E594-E602, broker protocol 6 (WRITTEN +
   OWNER). vtitests 1081, brokertests 13, ctest 8/8 (serial + -j4), Linux broker + mingw exes clean; macOS not built.
   Then (user 2026-11-26) clean sync like the original (E161 update): no notes/F8 during a bank sync, no pause.
   hil.sh on the TI2 with this build installed (backup of 0.8.43 in work/installed-backup-0.8.43): H1-H6 pass
   (work/hil/2026-11-26-clean-sync; first note now ~12 s, limit 15 s, d1160ca). Sweep blocks 32-512 x 44.1/48 kHz:
   10/10 pass, 0 drops, margin min 307 (work/hil/2026-11-26-sweep). Full sync with notes: 512/512, silent, cache
   identical (E161, index 210167a). Round build stays installed; NEXT: the user's own tests (DAW smoke, TI2 items
   below; asked 2026-11-26), then CI build + hil-win in the win11 VM. REAPER smoke: ok, incl. the user's Virus
   off/on while playing (E603: back in ~20 s, no drops after it).
   CI 45879876669 failed (test_broker -O3 fortify truncation, fixed in the test); running: CI 45880538174 (c561c9b),
   CI green; installed on Linux (install.sh) + win11 (setup.exe /quiet). L1: hil.sh H1-H6 pass, sweep 10/10 (no sleep:
   needs the user's sudo). W1 hil-win: H1/H2-2/H3/H6 pass, H2-1 drift +173 ppm only (8-note fit, VM clock, as 0.8.42).
   User Linux: LC4 L2-L6 pass (L5/L6 into RAM D), L7 skipped (no original project), logs clean. LC5 sleep pass
   (work/hil/20261126-204122, slept 46 s). User Windows (VM, Live): WC4 W2
   (buffer 256 only) W3 W4 pass; user's hil-win rerun ALL PASS (results-20261126-205652). Mac: MC4 MC5 M2 pass (user), M1 hil-mac
   FAIL (Claude): H1-H3 silent, the start-up check re-read RAM D (4 slots changed in L5/L6) and STALLED 11-49 s with
   no reply/timeout line, the clean sync dropped all notes; H6's 2nd sync read the 4 in 7.8 s. OPEN, top: reproduce on
   Linux with the pre-L5 RAM D cache (work/cache-backup-2026-11-26/RAM D; current one saved as RAM D.after-L5);
   out queue was 0 during the stall (not the transact queue wait); suspect wait_until_quiet()'s uncapped dumping()
   loop or an unlogged reply wait. hil-mac rerun ALL PASS (results-20261126-211945, cache current). Virus on the Mac; checklist scripts/checklists/round-0.8.44.json published as
   https://claude.ai/artifact/FhiP1yThnqamYEPb36HBig (marks in rounds/0.8.44/marks).
   OWED before the release (ask the user first, rule 2, no flood): hil.sh + hil-win at blocks 32-512 both rates
   (T14 NP 4 = top risk); win11 VM (T5 writes, T17 overlapped reads, T6/T7 protocol, B4-B6/D17 pipe security);
   own TI2: a request that never answers (T1), reconnect + late Virus (T3/T4), state upload with notes (T7/T8/T9),
   SAVE/sort/close (T2/T11), CC backup + restore into a user-named RAM bank (T19), original routed project (T15).
1. **Quiet issues:** #47 + #45 reach 7 days on 2026-11-26 (close message, with the user), #53 on 2026-11-30, #42 asked
   nothing since 11-20 (reporter promised a weekend test). #51: remove VTI_NATIVE_MARGIN, 15-20 min, diag.log.
2. **#37 after A+B (code in since rc8, E589):** still open: a burst without a Play window (setActive alone, Play again
   without stop, buffer switch, Logic live/not-live switch) waits 0.5 s (design C: ungate the pre-shift, needs a
   per-block trace first); our own 6 ms process() stall at block 64 during the bank sync. Not heard in Logic yet.
3. Release text (build.yml + infopage.h): written for marker, pre-shift, first Play, #51, library saves, xrun crossfade,
   Logic nudge, E581, keep-rest, #51 handover, E591/E592 (rc9); add anything later before the final tag. Local build shows a stale hash in its version line
   (cmake configure-time; cosmetic).
4. Website: rebuild (FAQ 'Every note sounds twice' is in build.py + README since 694a51e) after tirevive-site-check;
   fix the Mac install gaps (Open Anyway for pkg AND .command, restart after parking drivers, Access .pkg); check on
   our Mac with a Safari download first.
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
