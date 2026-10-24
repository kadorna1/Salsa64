# DuckStation PS3 port

This branch is an isolated porting workspace. It does not alter Salsa64's main branch.

## What is reused from the PS3 work in this repository

The existing PS3 infrastructure remains available in this branch at the repository root:

- `mk/ps3.mk`: shared PPU/SPU build and PKG packaging rules, for later integration.
- `lib/rsxkit`: RSX/GCM framebuffer, shader, UI, and presentation helpers.
- `lib/spukit`: SPU program loading, DMA/job queues, and worker support.
- `src/platform/ps3/log.c` and `paths.h`: established logging, paths, and diagnostic patterns.
- `src/frontend/audio_out.c`, pad mapping and pacing code: PS3 audio-output/input examples for a later frontend.

The first build intentionally does not link those libraries yet. It packages a very small PPE/JIT feasibility probe so we can validate the PS3 toolchain and executable-code-cache behavior before attempting the full DuckStation core.

## Phase 0 package

The probe constructs PPC64 instructions in memory (`addi r3,r3,imm; blr`), flushes the PPU instruction cache with `dcbst/sync/icbi/sync/isync`, and calls the generated function through an ELFv1 function descriptor. On a PS3 it writes the result to:

`/dev_hdd0/duckstation-ps3/phase0-jit-probe.log`

The package is **not a PS1 emulator** and is not expected to boot a game. It is a hardware validation step only.

## Build

The GitHub Actions workflow on this branch obtains the latest available `ps3dev/ps3dev` nightly bundle, runs host tests, compiles the PPU ELF, and creates raw/finalized PKG files. Output is uploaded as the workflow artifact `duckstation-ps3-phase0-packages`.

For later phases, the intended direction is to integrate the DuckStation core in interpreter mode first, retain the current PPC64 instruction emitter as a low-level utility, then implement a complete backend against DuckStation's architecture-neutral CPU recompiler interface. RSXKit/SPUKit integration is subsequent work, not claimed by this checkpoint.


## Package icon

`assets/ICON0.PNG` is the PS3 XMB package icon (320×176 PNG). The build
uses this icon via the shared Salsa64 `mk/ps3.mk` rules, so both the raw PKG
and finalized GNPDRM PKG carry the DuckStation PS3 artwork instead of Salsa64's
default icon.
