# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-24 (later): #37 contest of 4 research agents (libusb / Logic / timeline / original): all agree the cause is Logic (tracks that are not live are processed ahead, block switches 256<->2048 while playing in the rc7 log), not libusb or Catalina (USB 9:1 packets, 0 errors). Asked the reporter to test Record Enable + Process Buffer Range Small (issuecomment-7125502214). Design A 'keep the rest inside the lead room' + B (every forward move and 1-frame step faded) approved by the user: failing tests first (agent writing them into test_timelinefollow.cpp, model in work/issue37-keep), user checks the tests before code. Setup tip logic-live added (setuptips.h + test). Earlier today: rc7 reply 7123137920; PR #59 (insolentconstruct, E585-E588, supersedes the closed #55-#58) merged into round-0.8.44 (rebase merge, 679a06e; the repo allows only rebase merges), thanked (issuecomment-7136860882); our keep-rest entry renumbered E589 and rebased onto it (d6ff162); Thank-you section on the site with the next build from main.

## Where things stand

- **0.8.43 Latest** since 2026-11-21 11:45 UTC (release CI 44900063944 all green incl. the release job, 8 assets,
  site rebuilt by hand); 0.8.42 and 0.8.43 release details in log/2026-11.md (2026-11-22 entry).
- **Round 0.8.44** on branch round-0.8.44, latest pre-release **v0.8.44-rc7** (tag on the release-text commit after e8d463c; rc6 fe65ab5): marker fix (E568), Play
  pre-shift (E574), first Play at small blocks (E577), #51 two-clock guard (E578), xrun crossfade (E579), Logic
  latency nudge (E580), burst learning fix (E581), library-save logging (E576). 897 tests, hil.sh ALL PASS. After rc6: E582 (Logic burst room + crossfaded rest), E584 (Logic loss paid twice: window waits beyond +JUMP); 915 tests; in rc7. Mutation timelinefollow.h: 88/119 + the E582/E584 kills, every survivor left equivalent.
- Our TI2 Desktop: back on Linux (2026-11-22 evening, broker socket active); it was on the Mac 16:40-~18:00 for the #41 Logic test and was FROZEN on arrival there (power cycle; cause open). User flashed ROM Z on it in early November (E500). From a VM: VM=win11 scripts/vm.sh detach, then systemctl --user start tirevive-broker.socket.
- Linux: ~/.vst3 = dev build 601a0dd3 (7c605c2, E589 keep-rest, for the user's REAPER test 2026-11-24; REAPER 7.86 portable, 7.83 backup in ~/.local/opt/dl/reaper/backup-7.83-20261124); rc7 code 52fa05f5 in work/plugin-backup-rc7-52fa05f5; 40329e7a in work/plugin-backup-rc6b-40329e7a, rc6 code in work/plugin-backup-rc6-85bf0974. Backups: work/plugin-backup-ce68817b (rc3 code),
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

1. **Wait on rc7 (v0.8.44-rc7):** #51, #48 (when distorted + rc7), PR #59 DONE (merged 2026-11-24, E585-E588). #43: 7 quiet days reached 2026-11-24 but insolentconstruct commented (not the reporter): close decision with the user. rc7 not yet installed on the win11 VM and the Mac (routine after a release). #41 DONE (closed 2026-11-23).
   #51: remove VTI_NATIVE_MARGIN, 15-20 min, diag.log ('pulled back' gone? E578/E579).
2. **NEXT CODE: #37 design A + B (user-approved 2026-11-24)**: A = after a pre-shifted Play keep the rest while err stays inside the stamp-lead room (timeline behind by R plays stamped notes exactly; sim S051/S052: leads 1100-2000 sample-exact on the model), predictor = max of last 4 bursts + 128 + block/8, beyond the room one crossfade back to mid-room; B = forward moves always faded, 1-frame steps through Crossfade. Model on the rc7 Plays: block 512 from the third Play 0 hard / 0 steps / 8 of 9 clean (rc7: 6 hard, 325 steps, 0 clean); 2048: 4 of 7 clean. Cost: live keys up to ~40 ms later for that pass. IMPLEMENTED (d6ff162, 2026-11-24): E589; tests first (9 failing on the stub incl. guards for E337/#51/drift/no marker, step spacing 128, E581 xrun test now crossfaded), then timelinefollow.h rest reservoir + moved(), crossfade.h gain table, virusplugin.cpp (set_room every block, direct read fades 1-frame steps, 'kept' log line, live stamp S + rest). vtitests 928/928, all targets build; gui_duplicate_editors fails (also with the installed 23-Nov plug-in, not investigated). Not run: hardware, Logic, mutation, coverage, risk.py, hil.sh. Switch (user 2026-11-24): Control Center option keep-rest, ON by default, keep-rest.off = rc7 behaviour (diag.cpp table, read in initialize(), set_room(0)); README + site options sentence; CLI toggle checked in a throwaway home; GUI Options page not looked at. Other agent ideas kept: steps via read_resampled at 44.1 kHz (direct path skips a frame per step, virusplugin.cpp:2303), correction only in silence (last_sound), AU mSampleTime/mHostTime probe (auwrapper.mm:1528), broker SCHED_FIFO 10 below Darwin's range (unverified). Waits: #37 reporter (Record Enable + Small, 5 Plays on the notes, log).
   Still open beside A/B: a burst without a Play window (setActive alone, Play again without stop, buffer switch, Logic live/not-live switch) waits 0.5 s (22-395 pulled back; timeline agent's design C: ungate the pre-shift, needs a per-block trace first); our own 6022 us process() at block 64 during the RAM bank sync (lock with the sync?).
3. Release text (build.yml + infopage.h): written for marker, pre-shift, first Play, #51, library saves, xrun crossfade,
   Logic nudge; add E581 and anything later before the final tag. Local build shows a stale hash in its version line
   (cmake configure-time; cosmetic).
4. Website: rebuild (FAQ 'Every note sounds twice' is in build.py + README since 694a51e) after tirevive-site-check;
   fix the Mac install gaps (Open Anyway for pkg AND .command, restart after parking drivers, Access .pkg); check on
   our Mac with a Safari download first. Installs: rc6 on the win11 VM and the Mac not done.
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
