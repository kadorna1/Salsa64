/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — Transfer Pak bridge and Game Boy cartridge bus.
 *
 * The accessory is a 32-byte Joybus device. Its C000-FFFF aperture selects a
 * 16 KiB slice of the Game Boy address space; the Game Boy cartridge's own
 * mapper then selects ROM/RAM banks. This is not a Game Boy CPU emulator.
 */
#ifndef SALSA64_CORE_TRANSFERPAK_H
#define SALSA64_CORE_TRANSFERPAK_H

#include <stdbool.h>
#include <stdint.h>

#define SALSA64_TRANSFERPAK_RAM_MAX (128u * 1024u)
#define SALSA64_TRANSFERPAK_ROM_MAX (8u * 1024u * 1024u)
#define SALSA64_TRANSFERPAK_BLOCK 32u

enum salsa64_gb_mapper {
	SALSA64_GB_MAPPER_ROM,
	SALSA64_GB_MAPPER_MBC1,
	SALSA64_GB_MAPPER_MBC2,
	SALSA64_GB_MAPPER_MBC3,
	SALSA64_GB_MAPPER_MBC5,
};

struct salsa64_transferpak_cart {
	const uint8_t *rom; /* owned by frontend/transferpakfile.c */
	uint32_t rom_size;
	uint8_t *ram;       /* battery-backed cartridge RAM; owned by frontend */
	uint32_t ram_size;
	uint8_t type;       /* GB cartridge type byte at 0x147 */
	uint8_t mapper;     /* enum salsa64_gb_mapper */
	uint8_t ram_banks;
	uint8_t has_rtc;
	uint8_t dirty;
	uint32_t writes;
};

struct salsa64_transferpak_state {
	/* Transfer Pak registers. */
	uint8_t power;
	uint8_t bank;       /* 16 KiB Game Boy address-space slice (0..3) */
	uint8_t cart_enabled;
	uint8_t status_phase; /* transición del registro de estado (0..3) */
	/* Game Boy MBC registers. */
	uint8_t ram_enabled;
	uint8_t mbc1_rom_low, mbc1_bank_high, mbc1_mode;
	uint8_t mbc2_rom_bank;
	uint8_t mbc3_rom_bank, mbc3_ram_bank, mbc3_latch_prev;
	uint8_t mbc5_rom_low, mbc5_rom_high, mbc5_ram_bank;
	/* Deterministic RTC registers (MBC3). No wall-clock advancement yet. */
	uint8_t rtc[5], rtc_latched[5];
	uint8_t rtc_latched_valid;
};

struct salsa64_bus;

/* Return the RAM capacity implied by a validated-enough GB header, or 0. */
uint32_t salsa64_transferpak_ram_size(const uint8_t *rom, uint32_t rom_size);
/* Attach caller-owned ROM and RAM buffers. Returns false for unsupported or
 * malformed cartridges. RAM is not allocated and remains caller-owned. */
bool salsa64_transferpak_cart_init(struct salsa64_transferpak_cart *cart, const uint8_t *rom,
				  uint32_t rom_size, uint8_t *ram, uint32_t ram_size);
/* Reset controller-side registers without disconnecting the attached cart. */
void salsa64_transferpak_reset(struct salsa64_transferpak_state *state);
/* Execute one 32-byte Transfer Pak read/write. address must already have its
 * five checksum bits cleared/validated by the Joybus layer. */
void salsa64_transferpak_read(struct salsa64_transferpak_state *state, const struct salsa64_transferpak_cart *cart,
			      uint16_t address, uint8_t data[SALSA64_TRANSFERPAK_BLOCK]);
void salsa64_transferpak_write(struct salsa64_transferpak_state *state, struct salsa64_transferpak_cart *cart,
			       uint16_t address, const uint8_t data[SALSA64_TRANSFERPAK_BLOCK]);

#endif
