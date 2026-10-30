# Donkey Kong 64: opening-cutscene black-screen investigation

**Status:** investigation started; awaiting a reproducible console log. This document does not propose a runtime fix yet.

## Symptom to isolate

Donkey Kong 64 becomes stuck on a black screen during the opening cutscene. The exact freeze point, whether audio continues, whether controller input still advances anything, ROM region/revision, and active framebuffer settings need to be established from the captured log. Those details distinguish a graphics/presentation failure from game execution stopping while waiting on an emulation-visible resource.

## Related GLideN64 reports

These are leads, not proof that Salsa64 has the same bug:

- [GLideN64 #355 — European DK64 crashes after the Rare logo / when starting a new game](https://github.com/gonetz/GLideN64/issues/355). The report associates startup failures with `copyDepthToRDRAM` and `copyToRDRAM`, and the new-game failure additionally with `detectCFB`; the reporter says the USA version did not show the same failure.
- [GLideN64 #2392 — DK64 camera issue and depth-buffer copies](https://github.com/gonetz/GLideN64/issues/2392). The reporter found camera behavior changed when `copyDepthToRDRAM` was disabled; the GLideN64 maintainer also noted that DK64 hung after the startup logo in their setup and questioned whether this option was needed.
- [GLideN64 #2582 — black screen and input lock during level changes](https://github.com/gonetz/GLideN64/issues/2582). This was tied to enabling the emulator-assisted framebuffer read/write option. The maintainer described that option as poorly supported and potentially dangerous. It concerns transitions rather than necessarily this opening cutscene, so it is a second, separate lead.
- The current upstream [GLideN64 per-game profile](https://github.com/gonetz/GLideN64/blob/master/ini/GLideN64.custom.ini) sets `copyDepthToRDRAM=1` and `N64DepthCompare=0` for DK64. The older reports and current profile do not point to one universally safe setting; region, plugin/core version, and exact option semantics matter.
- [ares #1652](https://github.com/ares-emulator/ares/issues/1652) is a particularly close symptom match for the USA ROM: the intro softlocked to a black screen after the first Crocodile Isle section while the music continued. The reporter later said they could no longer reproduce the softlock; ares maintainers stressed the sequence's timing sensitivity. This supports investigating execution/thread timing instead of assuming the RSX is the primary fault.
- [ares #539](https://github.com/ares-emulator/ares/issues/539) reported a different DK64 freeze before the rap intro associated with DIV/MULT opcode timings, fixed by a subsequent core change. It is only a general CPU-timing lead, not evidence that the same timing issue causes this post-Crocodile-Isle hang.

## Relevant Salsa64 code and known constraints

- `src/frontend/main.c` reads the global `SALSA64_DATA_DIR/fbfx.cfg` value (0 = off, 1 = render-to-texture, 2 = framebuffer copies to RDRAM) and passes it to the RDP. The startup path currently has no DK64-specific `fbfx` override.
- `src/rdp/targets.c` already has special handling for DK64's interlaced opening logos: a 640×480 target can be presented through a 1280-wide VI with alternating field origins.
- `docs/framebuffer.md` records two other DK64-specific facts: the game reads its 320×240 depth image at `0x5d5600` from the CPU, and a later 320×459 transition framebuffer is read back by the CPU. The depth values written by rendered geometry are not currently copied back to RDRAM; the RDRAM currently contains the depth clear only. That could affect game behavior, but is not yet established as the cause of this opening freeze.

## Findings from the first console capture (2026-10-29)

The supplied excerpt contains a hang-detector snapshot at runtime 123.082 s, VI field 6084. This changes the working hypothesis from a possible presentation-only failure to a game-CPU wait/failure path:

- At about 120.543 s the renderer drops to 60 RSX-presented fields and 220 fields without an image in that interval; in the following interval all 299 fields have no image. The VI timer continues.
- The watchdog observes no controller reads or new graphics/RDP work for 6 seconds.
- CPU `cur_pc=0xffffffff8000094c`, `ra=0xffffffff8000094c`; the code words show `jalr $ra,$t9` at PC−8, with its delay-slot instruction at PC−4, followed by `beq $zero,$zero,-1` at the current PC and a `nop` delay slot. The branch target is the same address. The matching return address strongly suggests that a called routine returned into a deliberate terminal spin loop; we still need the saved registers and RDRAM image to identify the call target and why the caller chose this path.
- At the snapshot, the RSP has no active task, DPC START/END/CURRENT are zero, and there is no current DP interrupt scheduled. The lifetime counters have equal `DP programadas` and `DP levantadas` counts (2723 each); `DP retenida por el congelado` is a cumulative counter, while the live `DP retenida` flag is zero. The excerpt therefore does not by itself show a currently missing DP interrupt.
- Audio task processing and VI fields continue after the CPU enters the loop. This is consistent with the scheduler advancing independently while the game's normal input/graphics thread has stopped making progress.

The watchdog successfully saved two especially useful files on the console:
- `/dev_hdd0/salsa64/cuelgue-6084.txt`: all GPRs, including `$t9`, CPU state and RCP snapshot.
- `/dev_hdd0/salsa64/cuelgue-6084.rdram`: full RDRAM image. Run `python3 tools/libultra-threads.py cuelgue-6084.rdram --all` on the host copy to enumerate likely libultra threads and their PCs/message-queue waits.

Those files are needed to distinguish a fatal return from a missing-thread/message condition, and to identify whether the last blank presentation is a consequence rather than the initiating fault. Do not change DK64 framebuffer or depth-copy behavior until the saved evidence identifies a path to test.

## Investigation order

1. Read the supplied log and identify the ROM title/region/CRC, exact last successful scene, last RSP/RDP task activity, VI-origin/field changes, framebuffer-copy setting, and whether audio continues.
2. Map the last log events to the code paths above. Check whether emulation continues producing VI fields and graphics tasks after the black screen; do not assume a black presentation means the CPU has hung.
3. If existing logs cannot distinguish the cases, add narrowly scoped DK64 diagnostics (target dimensions/origin, presentation slot selection, depth/color readback requests, and task/VI progress) rather than changing framebuffer behavior blindly.
4. Make the smallest testable code change on this branch, keep it isolated from the Dr. Mario 64 work in PR #11, run host regressions and the PS3 package workflow, and leave hardware acceptance pending until the console is tested.

## Console log checklist

Include the log from a fresh launch through the freeze, without trimming the beginning. Especially useful:
- ROM title, region/revision, and CRC.
- The last scene reached (Rare logo, DK Rap, Crocodile Isle, or another point).
- Whether music/audio continues and whether any inputs are accepted.
- The startup lines for framebuffer effects and internal render scale.
- The last `gfx`, `RSP/RDP`, VI/framebuffer-target, fault, or watchdog messages before the screen goes black.

No game-specific behavior is changed by this initial investigation commit.
