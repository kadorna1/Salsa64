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

## Relevant Salsa64 code and known constraints

- `src/frontend/main.c` reads the global `SALSA64_DATA_DIR/fbfx.cfg` value (0 = off, 1 = render-to-texture, 2 = framebuffer copies to RDRAM) and passes it to the RDP. The startup path currently has no DK64-specific `fbfx` override.
- `src/rdp/targets.c` already has special handling for DK64's interlaced opening logos: a 640×480 target can be presented through a 1280-wide VI with alternating field origins.
- `docs/framebuffer.md` records two other DK64-specific facts: the game reads its 320×240 depth image at `0x5d5600` from the CPU, and a later 320×459 transition framebuffer is read back by the CPU. The depth values written by rendered geometry are not currently copied back to RDRAM; the RDRAM currently contains the depth clear only. That could affect game behavior, but is not yet established as the cause of this opening freeze.

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
