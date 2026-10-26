# Transfer Pak (experimental)

Salsa64 exposes a Transfer Pak on any of the four N64 controller ports. The N64
game talks to the Game Boy cartridge through the Transfer Pak's Joybus register
window; Salsa64 does **not** run a Game Boy CPU or render Game Boy gameplay.

## Companion files

Place a dumped Game Boy ROM next to the N64 ROM:

- Port 1: `<rom-n64>.gb` or `<rom-n64>.gbc`
- Ports 2–4: `<n64-rom-path>.p2.gb`, `.p3.gb`, or `.p4.gb` (the same suffixes
  work with `.gbc`)
- Battery-backed cartridge RAM is saved beside the N64 ROM as
  `<n64-rom-path>.sav` for port 1, or `.p2.sav`, `.p3.sav`, `.p4.sav`
  for ports 2–4.

The suffix is appended to the complete N64 ROM filename: for example,
`/dev_hdd0/salsa64/Pokemon Stadium.z64` uses
`/dev_hdd0/salsa64/Pokemon Stadium.z64.gb` and
`/dev_hdd0/salsa64/Pokemon Stadium.z64.sav` for port 1. Keep the original
N64 ROM filename unchanged; the sidecar suffix is appended to its full path.

Set **Opciones → Accesorio del control → Transfer Pak**, or override the
accessory in the game's settings. The in-game **Accesorios** page can select a
different accessory per controller while paused. Transfer Pak register access
works without a GB ROM, but the status register will report no cartridge and
the data window returns open-bus bytes.

## Supported cartridge hardware

The implementation covers ROM-only cartridges, MBC1, MBC2, MBC3, and MBC5
ROM/RAM banking. It checks the ROM size declared by the header, the header
checksum, mapper limits and RAM size before attaching a cartridge. MBC5 types
1C–1E, which include cartridge-side rumble, are rejected. Battery-backed RAM
is persisted after a quiet period and on game exit. MBC3 RTC registers can be
read, written and latched, but deliberately do not advance with wall-clock
time yet. Exotic mapper types (including camera) are not supported.

Save states include a versioned `TPAK` section with the GB ROM size and CRC-32,
registers, cartridge RAM and N64 Rumble Pak latch for every port. Loading a
state made without this section resets Transfer Pak and turns rumble off;
loading one made with another GB ROM is rejected.

Transfer Pak mode is experimental: host protocol tests and PS3 compilation
check its implementation, but compatibility with individual N64 games and
real-PS3 hardware still needs testing. Please include the N64 game name, Game
Boy cartridge header/type, PS3 model/firmware and `salsa64.log` when reporting
problems. Use your own legally obtained ROM dumps; do not share ROM files.
