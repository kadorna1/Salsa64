/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Salsa64 — Joybus Transfer Pak and Game Boy cartridge memory window. */
#include "core/transferpak.h"

#include <string.h>

#define GB_ROM_BANK 0x4000u
#define GB_RAM_BANK 0x2000u

static uint32_t rom_banks(const struct salsa64_transferpak_cart *c)
{
	return c->rom_size / GB_ROM_BANK;
}

static uint32_t header_ram_size(const uint8_t *rom, uint32_t size)
{
	if (!rom || size < 0x150)
		return 0;
	uint8_t type = rom[0x147];
	if (type == 0x05 || type == 0x06)
		return 512; /* MBC2 has 512 × 4-bit internal RAM. */
	switch (rom[0x149]) {
	case 0x01: return 2u * 1024u;
	case 0x02: return 8u * 1024u;
	case 0x03: return 32u * 1024u;
	case 0x04: return 128u * 1024u;
	case 0x05: return 64u * 1024u;
	default: return 0;
	}
}

static uint32_t header_rom_size(uint8_t code)
{
	if (code <= 8)
		return 0x8000u << code;
	if (code == 0x52)
		return 72u * GB_ROM_BANK;
	if (code == 0x53)
		return 80u * GB_ROM_BANK;
	if (code == 0x54)
		return 96u * GB_ROM_BANK;
	return 0;
}

static bool header_checksum_ok(const uint8_t *rom)
{
	uint8_t sum = 0;
	for (unsigned i = 0x134; i <= 0x14C; i++)
		sum = (uint8_t)(sum - rom[i] - 1u);
	return sum == rom[0x14D];
}

uint32_t salsa64_transferpak_ram_size(const uint8_t *rom, uint32_t rom_size)
{
	if (!rom || rom_size < 0x150 || rom_size > SALSA64_TRANSFERPAK_ROM_MAX)
		return 0;
	return header_ram_size(rom, rom_size);
}

static int mapper_for(uint8_t type)
{
	if (type == 0x00 || type == 0x08 || type == 0x09)
		return SALSA64_GB_MAPPER_ROM;
	if (type >= 0x01 && type <= 0x03)
		return SALSA64_GB_MAPPER_MBC1;
	if (type == 0x05 || type == 0x06)
		return SALSA64_GB_MAPPER_MBC2;
	if (type >= 0x0F && type <= 0x13)
		return SALSA64_GB_MAPPER_MBC3;
	/* 1C–1E add a cartridge-side rumble motor, not emulated here. */
	if (type >= 0x19 && type <= 0x1B)
		return SALSA64_GB_MAPPER_MBC5;
	return -1;
}

static bool type_uses_ram(uint8_t type)
{
	return type == 0x02 || type == 0x03 || type == 0x05 || type == 0x06 || type == 0x08 || type == 0x09 ||
	       type == 0x10 || type == 0x12 || type == 0x13 || type == 0x1A || type == 0x1B;
}

static uint32_t mapper_rom_max(int mapper)
{
	switch (mapper) {
	case SALSA64_GB_MAPPER_ROM: return 2u * GB_ROM_BANK;
	case SALSA64_GB_MAPPER_MBC1: return 128u * GB_ROM_BANK;
	case SALSA64_GB_MAPPER_MBC2: return 16u * GB_ROM_BANK;
	case SALSA64_GB_MAPPER_MBC3: return 128u * GB_ROM_BANK;
	case SALSA64_GB_MAPPER_MBC5: return 512u * GB_ROM_BANK;
	default: return 0;
	}
}

bool salsa64_transferpak_cart_init(struct salsa64_transferpak_cart *cart, const uint8_t *rom,
				  uint32_t rom_size, uint8_t *ram, uint32_t ram_size)
{
	if (!cart || !rom || rom_size < 0x8000 || rom_size > SALSA64_TRANSFERPAK_ROM_MAX ||
	    (rom_size & (GB_ROM_BANK - 1)) != 0 || header_rom_size(rom[0x148]) != rom_size ||
	    !header_checksum_ok(rom))
		return false;
	int mapper = mapper_for(rom[0x147]);
	if (mapper < 0 || rom_size > mapper_rom_max(mapper))
		return false;
	uint32_t need = header_ram_size(rom, rom_size);
	if (type_uses_ram(rom[0x147]) != (need != 0) || need > SALSA64_TRANSFERPAK_RAM_MAX ||
	    (need && (!ram || ram_size != need)))
		return false;
	if (need == 0 && ram_size != 0)
		return false;

	memset(cart, 0, sizeof *cart);
	cart->rom = rom;
	cart->rom_size = rom_size;
	cart->ram = ram;
	cart->ram_size = need;
	cart->type = rom[0x147];
	cart->mapper = (uint8_t)mapper;
	cart->ram_banks = (uint8_t)((need + GB_RAM_BANK - 1) / GB_RAM_BANK);
	cart->has_rtc = (cart->type == 0x0F || cart->type == 0x10);
	return true;
}

void salsa64_transferpak_reset(struct salsa64_transferpak_state *s)
{
	if (!s)
		return;
	memset(s, 0, sizeof *s);
	s->mbc1_rom_low = 1;
	s->mbc2_rom_bank = 1;
	s->mbc3_rom_bank = 1;
	s->mbc5_rom_low = 1;
}

static uint8_t gb_rom_read(const struct salsa64_transferpak_state *s,
			   const struct salsa64_transferpak_cart *c, uint16_t addr)
{
	uint32_t bank = 0;
	if (addr < 0x4000) {
		if (c->mapper == SALSA64_GB_MAPPER_MBC1 && s->mbc1_mode)
			bank = (uint32_t)(s->mbc1_bank_high & 3u) << 5;
	} else {
		switch (c->mapper) {
		case SALSA64_GB_MAPPER_MBC1:
			bank = ((uint32_t)(s->mbc1_bank_high & 3u) << 5) | (s->mbc1_rom_low & 0x1Fu);
			if (!(bank & 0x1Fu))
				bank++;
			break;
		case SALSA64_GB_MAPPER_MBC2:
			bank = s->mbc2_rom_bank & 0x0Fu;
			if (!bank)
				bank = 1;
			break;
		case SALSA64_GB_MAPPER_MBC3:
			bank = s->mbc3_rom_bank & 0x7Fu;
			if (!bank)
				bank = 1;
			break;
		case SALSA64_GB_MAPPER_MBC5:
			bank = (uint32_t)(s->mbc5_rom_high & 1u) << 8 | s->mbc5_rom_low;
			break;
		default:
			bank = 1;
			break;
		}
	}
	uint32_t count = rom_banks(c);
	if (!count)
		return 0xFF;
	bank %= count;
	uint32_t offset = bank * GB_ROM_BANK + (addr & 0x3FFFu);
	return offset < c->rom_size ? c->rom[offset] : 0xFF;
}

static uint8_t gb_ram_bank(const struct salsa64_transferpak_state *s,
			   const struct salsa64_transferpak_cart *c)
{
	switch (c->mapper) {
	case SALSA64_GB_MAPPER_MBC1: return s->mbc1_mode ? (s->mbc1_bank_high & 3u) : 0;
	case SALSA64_GB_MAPPER_MBC3: return s->mbc3_ram_bank & 0x0Fu;
	case SALSA64_GB_MAPPER_MBC5: return s->mbc5_ram_bank & 0x0Fu;
	default: return 0;
	}
}

static uint8_t gb_read(struct salsa64_transferpak_state *s, const struct salsa64_transferpak_cart *c,
		       uint16_t addr)
{
	if (!c || !c->rom)
		return 0xFF;
	if (addr < 0x8000)
		return gb_rom_read(s, c, addr);
	if (addr < 0xA000)
		return 0xFF; /* VRAM is on the Game Boy, not the cartridge. */
	if (addr < 0xC000) {
		if (c->mapper != SALSA64_GB_MAPPER_ROM && !s->ram_enabled)
			return 0xFF;
		if (c->mapper == SALSA64_GB_MAPPER_MBC3 && c->has_rtc &&
		    s->mbc3_ram_bank >= 0x08 && s->mbc3_ram_bank <= 0x0C) {
			unsigned reg = s->mbc3_ram_bank - 0x08;
			return s->rtc_latched_valid ? s->rtc_latched[reg] : s->rtc[reg];
		}
		if (!c->ram || !c->ram_size)
			return 0xFF;
		uint32_t offset;
		if (c->mapper == SALSA64_GB_MAPPER_MBC2) {
			offset = addr & 0x01FFu;
			return (uint8_t)(0xF0u | (c->ram[offset] & 0x0Fu));
		}
		uint32_t bank = gb_ram_bank(s, c);
		offset = (bank * GB_RAM_BANK + (addr - 0xA000u)) % c->ram_size;
		return c->ram[offset];
	}
	/* These addresses belong to GB work RAM/I/O/OAM, not the cartridge. */
	return 0xFF;
}

static void gb_mapper_write(struct salsa64_transferpak_state *s,
			    struct salsa64_transferpak_cart *c, uint16_t addr, uint8_t value)
{
	if (!c || !c->rom)
		return;
	if (addr < 0x8000) {
		switch (c->mapper) {
		case SALSA64_GB_MAPPER_MBC1:
			if (addr < 0x2000)
				s->ram_enabled = (value & 0x0Fu) == 0x0Au;
			else if (addr < 0x4000) {
				s->mbc1_rom_low = value & 0x1Fu;
				if (!s->mbc1_rom_low)
					s->mbc1_rom_low = 1;
			} else if (addr < 0x6000)
				s->mbc1_bank_high = value & 3u;
			else
				s->mbc1_mode = value & 1u;
			break;
		case SALSA64_GB_MAPPER_MBC2:
			if (addr < 0x4000) {
				if (addr & 0x0100)
					s->mbc2_rom_bank = (value & 0x0Fu) ? (value & 0x0Fu) : 1;
				else
					s->ram_enabled = (value & 0x0Fu) == 0x0Au;
			}
			break;
		case SALSA64_GB_MAPPER_MBC3:
			if (addr < 0x2000)
				s->ram_enabled = (value & 0x0Fu) == 0x0Au;
			else if (addr < 0x4000) {
				s->mbc3_rom_bank = value & 0x7Fu;
				if (!s->mbc3_rom_bank)
					s->mbc3_rom_bank = 1;
			} else if (addr < 0x6000)
				s->mbc3_ram_bank = value;
			else {
				if (!s->mbc3_latch_prev && value == 1) {
					memcpy(s->rtc_latched, s->rtc, sizeof s->rtc);
					s->rtc_latched_valid = 1;
				}
				s->mbc3_latch_prev = value;
			}
			break;
		case SALSA64_GB_MAPPER_MBC5:
			if (addr < 0x2000)
				s->ram_enabled = (value & 0x0Fu) == 0x0Au;
			else if (addr < 0x3000)
				s->mbc5_rom_low = value;
			else if (addr < 0x4000)
				s->mbc5_rom_high = value & 1u;
			else if (addr < 0x6000)
				s->mbc5_ram_bank = value & 0x0Fu;
			break;
		default:
			break;
		}
		return;
	}
	if (addr < 0xA000 || addr >= 0xC000)
		return;
	if (c->mapper != SALSA64_GB_MAPPER_ROM && !s->ram_enabled)
		return;
	if (c->mapper == SALSA64_GB_MAPPER_MBC3 && c->has_rtc &&
	    s->mbc3_ram_bank >= 0x08 && s->mbc3_ram_bank <= 0x0C) {
		s->rtc[s->mbc3_ram_bank - 0x08] = value;
		return;
	}
	if (!c->ram || !c->ram_size)
		return;
	uint32_t offset;
	if (c->mapper == SALSA64_GB_MAPPER_MBC2) {
		offset = addr & 0x01FFu;
		value &= 0x0Fu;
	} else {
		offset = ((uint32_t)gb_ram_bank(s, c) * GB_RAM_BANK + (addr - 0xA000u)) % c->ram_size;
	}
	/* Most Transfer Pak software writes whole blocks of repeated control
	 * bytes; preserving the entire block is essential for SRAM data. */
	if (c->ram[offset] != value) {
		c->ram[offset] = value;
		c->dirty = 1;
		c->writes++;
	}
}

static uint8_t status_step(struct salsa64_transferpak_state *s, const struct salsa64_transferpak_cart *c)
{
	if (!s->power)
		return 0;
	if (!c || !c->rom)
		return 0x40;
	if (s->cart_enabled) {
		if (s->status_phase == 3) {
			s->status_phase = 2;
			return 0x8D;
		}
		return 0x89;
	}
	if (s->status_phase == 2) {
		s->status_phase = 1;
		return 0x88;
	}
	if (s->status_phase == 1) {
		s->status_phase = 0;
		return 0x84;
	}
	return 0x80;
}

void salsa64_transferpak_read(struct salsa64_transferpak_state *s, const struct salsa64_transferpak_cart *c,
			      uint16_t address, uint8_t data[SALSA64_TRANSFERPAK_BLOCK])
{
	if (!s || !data)
		return;
	uint8_t value;
	if (address < 0x2000 || (address >= 0x8000 && address < 0xA000)) {
		value = s->power ? 0x84 : 0x00;
		memset(data, value, SALSA64_TRANSFERPAK_BLOCK);
		return;
	}
	if (address >= 0x2000 && address < 0x3000) {
		memset(data, s->power ? s->bank : 0, SALSA64_TRANSFERPAK_BLOCK);
		return;
	}
	if (address >= 0x3000 && address < 0x4000) {
		value = status_step(s, c);
		memset(data, value, SALSA64_TRANSFERPAK_BLOCK);
		return;
	}
	if (address < 0x8000) {
		memset(data, 0, SALSA64_TRANSFERPAK_BLOCK);
		return;
	}
	if (address >= 0xA000 && address < 0xB000) {
		memset(data, s->power ? s->bank : 0, SALSA64_TRANSFERPAK_BLOCK);
		return;
	}
	if (address >= 0xB000 && address < 0xC000) {
		value = status_step(s, c);
		memset(data, value, SALSA64_TRANSFERPAK_BLOCK);
		return;
	}
	if (!s->power || !s->cart_enabled) {
		memset(data, 0, SALSA64_TRANSFERPAK_BLOCK);
		return;
	}
	uint16_t base = (uint16_t)(((s->bank & 3u) * GB_ROM_BANK) | (address & 0x3FFFu));
	for (unsigned i = 0; i < SALSA64_TRANSFERPAK_BLOCK; i++)
		data[i] = gb_read(s, c, (uint16_t)(base + i));
}

void salsa64_transferpak_write(struct salsa64_transferpak_state *s, struct salsa64_transferpak_cart *c,
			       uint16_t address, const uint8_t data[SALSA64_TRANSFERPAK_BLOCK])
{
	if (!s || !data)
		return;
	if (address < 0x2000 || (address >= 0x8000 && address < 0xA000)) {
		if (data[0] == 0x84 && !s->power) {
			s->power = 1;
			s->bank = 3;
			s->cart_enabled = 0;
			salsa64_transferpak_reset(s);
			s->power = 1;
			s->bank = 3;
		} else if (data[0] == 0xFE) {
			s->power = 0;
			s->bank = 0;
			s->cart_enabled = 0;
			s->status_phase = 0;
		}
		return;
	}
	if (!s->power)
		return;
	if (address >= 0x2000 && address < 0x3000) {
		s->bank = data[0] <= 3 ? data[0] : 0;
		return;
	}
	if (address >= 0x3000 && address < 0x4000) {
		uint8_t enabled = data[0] & 1u;
		if (!s->cart_enabled && enabled) {
			s->status_phase = 3;
			s->ram_enabled = 0;
			s->mbc1_rom_low = 1;
			s->mbc1_bank_high = s->mbc1_mode = 0;
			s->mbc2_rom_bank = s->mbc3_rom_bank = 1;
			s->mbc3_ram_bank = s->mbc3_latch_prev = 0;
			s->mbc5_rom_low = 1;
			s->mbc5_rom_high = s->mbc5_ram_bank = 0;
		} else if (s->cart_enabled && !enabled) {
			s->status_phase = 2;
		}
		s->cart_enabled = enabled;
		return;
	}
	if (address >= 0x4000 && address < 0x8000)
		return;
	if (address >= 0xA000 && address < 0xB000) {
		s->bank = data[0] <= 3 ? data[0] : 0;
		return;
	}
	if (address >= 0xB000 && address < 0xC000) {
		uint8_t enabled = data[0] & 1u;
		if (!s->cart_enabled && enabled) {
			s->status_phase = 3;
			s->ram_enabled = 0;
			s->mbc1_rom_low = 1;
			s->mbc1_bank_high = s->mbc1_mode = 0;
			s->mbc2_rom_bank = s->mbc3_rom_bank = 1;
			s->mbc3_ram_bank = s->mbc3_latch_prev = 0;
			s->mbc5_rom_low = 1;
			s->mbc5_rom_high = s->mbc5_ram_bank = 0;
		} else if (s->cart_enabled && !enabled) {
			s->status_phase = 2;
		}
		s->cart_enabled = enabled;
		return;
	}
	if (address >= 0xC000 && s->power && s->cart_enabled)
		for (unsigned i = 0; i < SALSA64_TRANSFERPAK_BLOCK; i++) {
			uint16_t gb_addr = (uint16_t)(((s->bank & 3u) * GB_ROM_BANK) | ((address + i) & 0x3FFFu));
			gb_mapper_write(s, c, gb_addr, data[i]);
		}
}
