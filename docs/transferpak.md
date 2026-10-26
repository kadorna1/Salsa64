# Transfer Pak (experimental)

Salsa64 exposes a Transfer Pak on any of the four N64 controller ports. The N64
game talks to the Game Boy cartridge through the Transfer Pak's Joybus register
window; Salsa64 does **not** run a Game Boy CPU or render Game Boy gameplay.

## Companion files

Place a dumped Game Boy ROM next to the N64 ROM:

- Port 1: `<n64-rom-path>.gb` or `<n64-rom-path>.gbc`
- Ports 2–4: `<n64-rom-path>.p2.gb`, `.p3.gb`, or `.p4.gb` (the same suffixes
  work with `.gbc`)
- Battery-backed cartridge RAM is saved beside the N64 ROM as
  `<n64-rom-path>.sav` for port 1, or `.p2.sav`, `.p3.sav`, `.p4.sav`
  for ports 2–4.

For example, `/dev_hdd0/salsa64/Pokemon Stadium.z64` uses
`/dev_hdd0/salsa64/Pokemon Stadium.z64.gb` and
`/dev_hdd0/salsa64/Pokemon Stadium.z64.sav` for port 1. Keep the original
N64 ROM filename unchanged; the sidecar suffix is appended to its full path.

Set **Opciones → Accesorio del control → Transfer Pak**, or override the
accessory in the game's settings. The in-game **Accesorios** page can select a
different accessory per controller while paused. Transfer Pak register access
works without a GB ROM, but the status register will report no cartridge and
the data window returns open-bus bytes.

## Supported cartridge hardware

The initial implementation covers ROM-only cartridges, MBC1, MBC2, MBC3, and
MBC5 ROM/RAM banking. Battery-backed cartridge RAM is persisted after a quiet
period and on game exit. MBC3 RTC registers are present but do not yet advance
with wall-clock time, and exotic mapper types (including camera and other
special hardware) are not supported. ROM images are accepted up to 8 MiB.

Transfer Pak mode is experimental: host protocol tests and PS3 compilation
check its implementation, but compatibility with individual N64 games and
real-PS3 hardware still needs testing. Please include the N64 game name, Game
Boy cartridge header/type, PS3 model/firmware and `salsa64.log` when reporting
problems. Use your own legally obtained ROM dumps; do not share ROM files.
