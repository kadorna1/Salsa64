/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — Transfer Pak bus and Game Boy cartridge mapper tests.
 */
#include "test.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/transferpak.h"
#include "frontend/transferpakfile.h"

#define GB_BANK 0x4000u

static uint8_t test_rom[4 * GB_BANK];
static uint8_t test_ram[SALSA64_TRANSFERPAK_RAM_MAX];

static void make_rom(uint8_t type, uint8_t ram_code)
{
	for (unsigned bank = 0; bank < 4; bank++)
		memset(test_rom + bank * GB_BANK, (int)(bank * 0x11u), GB_BANK);
	test_rom[0x147] = type;
	test_rom[0x148] = 0x01; /* 64 KiB */
	test_rom[0x149] = ram_code;
	uint8_t sum = 0;
	for (unsigned i = 0x134; i <= 0x14C; i++)
		sum = (uint8_t)(sum - test_rom[i] - 1u);
	test_rom[0x14D] = sum;
}

static void quiet_log(const char *fmt, ...)
{
	(void)fmt;
}

static void write_reg(struct salsa64_transferpak_state *s, struct salsa64_transferpak_cart *c,
		      uint16_t addr, uint8_t value)
{
	uint8_t data[SALSA64_TRANSFERPAK_BLOCK];
	memset(data, value, sizeof data);
	salsa64_transferpak_write(s, c, addr, data);
}

static uint8_t read_first(struct salsa64_transferpak_state *s, const struct salsa64_transferpak_cart *c,
			  uint16_t addr)
{
	uint8_t data[SALSA64_TRANSFERPAK_BLOCK];
	memset(data, 0xCC, sizeof data);
	salsa64_transferpak_read(s, c, addr, data);
	for (unsigned i = 1; i < sizeof data; i++)
		CHECK_EQ(data[i], data[0]);
	return data[0];
}

static void transferpak_power_status_and_banking(void)
{
	struct salsa64_transferpak_cart cart;
	struct salsa64_transferpak_state state;
	make_rom(0x03, 0x02); /* MBC1 + battery-backed 8 KiB RAM */
	memset(test_ram, 0xFF, sizeof test_ram);
	CHECK_EQ(salsa64_transferpak_ram_size(test_rom, sizeof test_rom), 8u * 1024u);
	CHECK(salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram, 8u * 1024u));
	salsa64_transferpak_reset(&state);

	CHECK_EQ(read_first(&state, &cart, 0x8000), 0x00); /* power off */
	write_reg(&state, &cart, 0x8000, 0x84);
	CHECK(state.power);
	CHECK_EQ(state.bank, 3);
	CHECK_EQ(read_first(&state, &cart, 0x8000), 0x84);
	CHECK_EQ(read_first(&state, &cart, 0xA000), 3);

	write_reg(&state, &cart, 0xA000, 1);
	CHECK_EQ(read_first(&state, &cart, 0xA000), 1);
	write_reg(&state, &cart, 0xB000, 1);
	CHECK_EQ(read_first(&state, &cart, 0xB000), 0x8D); /* powered, cart enabled, reset=3 */
	CHECK_EQ(read_first(&state, &cart, 0xB000), 0x89); /* reset status advances */
	CHECK_EQ(read_first(&state, &cart, 0xB000), 0x89); /* steady state: access enabled */

	CHECK_EQ(read_first(&state, &cart, 0xC000), 0x11); /* Transfer Pak bank 1 -> GB ROM bank 1 */
	write_reg(&state, &cart, 0xA000, 2);
	CHECK_EQ(read_first(&state, &cart, 0xC000), 0xFF); /* Transfer Pak bank 2 maps GB VRAM */
	write_reg(&state, &cart, 0xA000, 3);
	CHECK_EQ(read_first(&state, &cart, 0xC000), 0xFF); /* Transfer Pak bank 3 maps GB work RAM */

	write_reg(&state, &cart, 0xB000, 0);
	CHECK_EQ(read_first(&state, &cart, 0xB000), 0x88);
	CHECK_EQ(read_first(&state, &cart, 0xB000), 0x84);
	CHECK_EQ(read_first(&state, &cart, 0xB000), 0x80);
	uint8_t data[SALSA64_TRANSFERPAK_BLOCK];
	memset(data, 0xCC, sizeof data);
	salsa64_transferpak_read(&state, &cart, 0xC000, data);
	CHECK_EQ(data[0], 0);
	CHECK_EQ(data[31], 0);

	write_reg(&state, &cart, 0x8000, 0xFE);
	CHECK(!state.power);
	CHECK(!state.cart_enabled);
	CHECK_EQ(read_first(&state, &cart, 0x8000), 0);
}

static void transferpak_disabled_blocks_cartridge_writes(void)
{
	struct salsa64_transferpak_cart cart;
	struct salsa64_transferpak_state state;
	make_rom(0x03, 0x02);
	memset(test_ram, 0xFF, 8u * 1024u);
	CHECK(salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram, 8u * 1024u));
	salsa64_transferpak_reset(&state);
	write_reg(&state, &cart, 0x8000, 0x84);

	/* Banco 0 lleva C000 a GB 0000: ni RAMG ni bancos cambian sin acceso. */
	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xC000, 0x0A);
	write_reg(&state, &cart, 0xE000, 2);
	CHECK(!state.ram_enabled);
	CHECK_EQ(state.mbc1_rom_low, 1);

	write_reg(&state, &cart, 0xB000, 1);
	write_reg(&state, &cart, 0xC000, 0x0A);
	CHECK(state.ram_enabled);
	write_reg(&state, &cart, 0xA000, 2);
	write_reg(&state, &cart, 0xE000, 0x5A);
	CHECK_EQ(test_ram[0], 0x5A);
	write_reg(&state, &cart, 0xB000, 0);
	write_reg(&state, &cart, 0xE000, 0x33);
	CHECK_EQ(test_ram[0], 0x5A);
}

static void transferpak_mbc1_banking_and_sram(void)
{
	struct salsa64_transferpak_cart cart;
	struct salsa64_transferpak_state state;
	uint8_t data[SALSA64_TRANSFERPAK_BLOCK];
	make_rom(0x03, 0x02);
	memset(test_ram, 0xFF, 8u * 1024u);
	CHECK(salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram, 8u * 1024u));
	salsa64_transferpak_reset(&state);
	write_reg(&state, &cart, 0x8000, 0x84);
	write_reg(&state, &cart, 0xB000, 1);

	/* GB address 0x2000 is reached at E000 with Transfer Pak bank 0. */
	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xE000, 2); /* all 32 bytes select MBC1 ROM bank 2 */
	write_reg(&state, &cart, 0xA000, 1);
	CHECK_EQ(read_first(&state, &cart, 0xC000), 0x22);

	/* Enable MBC1 external RAM at GB 0000, then use Transfer Pak bank 2
	 * (GB 8000–BFFF) + E000 (GB A000) to read/write cartridge SRAM. */
	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xC000, 0x0A);
	CHECK(state.ram_enabled);
	write_reg(&state, &cart, 0xA000, 2);
	write_reg(&state, &cart, 0xE000, 0x5A);
	memset(data, 0, sizeof data);
	salsa64_transferpak_read(&state, &cart, 0xE000, data);
	for (unsigned i = 0; i < sizeof data; i++)
		CHECK_EQ(data[i], 0x5A);
	CHECK(cart.dirty);
	CHECK(cart.writes >= SALSA64_TRANSFERPAK_BLOCK);
	CHECK_EQ(test_ram[0], 0x5A);
	CHECK_EQ(test_ram[31], 0x5A);
}

static void transferpak_mbc2_nibble_ram(void)
{
	struct salsa64_transferpak_cart cart;
	struct salsa64_transferpak_state state;
	uint8_t data[SALSA64_TRANSFERPAK_BLOCK];
	make_rom(0x06, 0); /* MBC2 + battery; internal 512 x 4-bit RAM */
	memset(test_ram, 0xFF, 512);
	CHECK_EQ(salsa64_transferpak_ram_size(test_rom, sizeof test_rom), 512);
	CHECK(salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram, 512));
	salsa64_transferpak_reset(&state);
	write_reg(&state, &cart, 0x8000, 0x84);
	write_reg(&state, &cart, 0xB000, 1);
	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xE100, 2); /* GB address 2100: select MBC2 ROM bank 2 */
	write_reg(&state, &cart, 0xA000, 1);
	CHECK_EQ(read_first(&state, &cart, 0xC000), 0x22);

	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xC000, 0x0A); /* RAM enable */
	write_reg(&state, &cart, 0xA000, 2);
	write_reg(&state, &cart, 0xE000, 0x0B); /* first bytes of 512 x 4-bit RAM */
	memset(data, 0, sizeof data);
	salsa64_transferpak_read(&state, &cart, 0xE000, data);
	for (unsigned i = 0; i < sizeof data; i++)
		CHECK_EQ(data[i], 0xFB);
	CHECK_EQ(test_ram[0], 0x0B);
}

static void transferpak_file_save_round_trip(void)
{
	if (!t_tmp_dir) {
		CHECK(false);
		return;
	}
	char source[256], gb_path[260], sav_path[260];
	snprintf(source, sizeof source, "%s/transferpak-test.z64", t_tmp_dir);
	snprintf(gb_path, sizeof gb_path, "%s.gb", source);
	snprintf(sav_path, sizeof sav_path, "%s.sav", source);
	FILE *n64 = fopen(source, "wb");
	CHECK(n64 != NULL);
	if (!n64)
		return;
	CHECK_EQ(fclose(n64), 0);

	make_rom(0x03, 0x02);
	FILE *gb = fopen(gb_path, "wb");
	CHECK(gb != NULL);
	if (!gb) {
		remove(source);
		return;
	}
	CHECK_EQ(fwrite(test_rom, 1, sizeof test_rom, gb), sizeof test_rom);
	CHECK_EQ(fclose(gb), 0);

	struct salsa64_pif pif;
	struct transferpakfile file;
	memset(&pif, 0, sizeof pif);
	transferpakfile_open(&file, &pif, 0, source, quiet_log);
	CHECK(file.cart.rom != NULL);
	CHECK_EQ(file.cart.ram_size, 8u * 1024u);
	CHECK_EQ(file.cart.ram[0], 0xFF);
	CHECK(pif.transferpak[0] == &file.cart);

	/* Exercise deferred staging and the .sav sidecar, then reload it. */
	file.cart.ram[0] = 0x42;
	file.cart.dirty = 1;
	file.cart.writes = 1;
	file.seen_writes = 1;
	file.last_write_us = 100;
	transferpakfile_stage(&file, 2000100);
	CHECK(file.pending);
	CHECK(!file.cart.dirty);
	transferpakfile_write_staged(&file, 2000100);
	CHECK(!file.pending);
	CHECK(access(sav_path, F_OK) == 0);
	transferpakfile_close(&file);

	transferpakfile_open(&file, &pif, 0, source, quiet_log);
	CHECK(file.cart.rom != NULL);
	CHECK_EQ(file.cart.ram[0], 0x42);
	transferpakfile_close(&file);
	remove(sav_path);
	remove(gb_path);
	remove(source);
}

static void transferpak_mbc3_mbc5_banking(void)
{
	struct salsa64_transferpak_cart cart;
	struct salsa64_transferpak_state state;
	uint8_t data[SALSA64_TRANSFERPAK_BLOCK];

	/* MBC3: switch ROM bank, enable SRAM, select external RAM bank 2. */
	make_rom(0x13, 0x03);
	memset(test_ram, 0xFF, 32u * 1024u);
	CHECK(salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram, 32u * 1024u));
	salsa64_transferpak_reset(&state);
	write_reg(&state, &cart, 0x8000, 0x84);
	write_reg(&state, &cart, 0xB000, 1);
	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xE000, 2); /* GB 0x2000: MBC3 ROM bank 2 */
	write_reg(&state, &cart, 0xA000, 1);
	CHECK_EQ(read_first(&state, &cart, 0xC000), 0x22);

	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xC000, 0x0A); /* GB 0x0000: SRAM enable */
	write_reg(&state, &cart, 0xA000, 1);
	write_reg(&state, &cart, 0xC000, 2); /* GB 0x4000: SRAM bank 2 */
	write_reg(&state, &cart, 0xA000, 2);
	write_reg(&state, &cart, 0xE000, 0x5C); /* GB 0xA000: external SRAM */
	memset(data, 0, sizeof data);
	salsa64_transferpak_read(&state, &cart, 0xE000, data);
	for (unsigned i = 0; i < sizeof data; i++)
		CHECK_EQ(data[i], 0x5C);
	CHECK_EQ(test_ram[2 * 0x2000], 0x5C);

	/* MBC5: 8-bit ROM-bank register and a separately banked SRAM window. */
	make_rom(0x1B, 0x03);
	memset(test_ram, 0xFF, 32u * 1024u);
	CHECK(salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram, 32u * 1024u));
	salsa64_transferpak_reset(&state);
	write_reg(&state, &cart, 0x8000, 0x84);
	write_reg(&state, &cart, 0xB000, 1);
	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xE000, 3); /* GB 0x2000: MBC5 ROM bank 3 */
	write_reg(&state, &cart, 0xA000, 1);
	CHECK_EQ(read_first(&state, &cart, 0xC000), 0x33);

	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xC000, 0x0A); /* GB 0x0000: SRAM enable */
	write_reg(&state, &cart, 0xA000, 1);
	write_reg(&state, &cart, 0xC000, 1); /* GB 0x4000: SRAM bank 1 */
	write_reg(&state, &cart, 0xA000, 2);
	write_reg(&state, &cart, 0xE000, 0xA6); /* GB 0xA000: external SRAM */
	memset(data, 0, sizeof data);
	salsa64_transferpak_read(&state, &cart, 0xE000, data);
	for (unsigned i = 0; i < sizeof data; i++)
		CHECK_EQ(data[i], 0xA6);
	CHECK_EQ(test_ram[0x2000], 0xA6);
}

static void transferpak_mbc3_rtc_and_ram_wrap(void)
{
	struct salsa64_transferpak_cart cart;
	struct salsa64_transferpak_state state;
	make_rom(0x10, 0x02); /* RTC + 8 KiB: todos los bancos SRAM se espejan. */
	memset(test_ram, 0xFF, 8u * 1024u);
	CHECK(salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram, 8u * 1024u));
	salsa64_transferpak_reset(&state);
	write_reg(&state, &cart, 0x8000, 0x84);
	write_reg(&state, &cart, 0xB000, 1);
	write_reg(&state, &cart, 0xA000, 1);
	write_reg(&state, &cart, 0xC000, 0x08); /* registro de segundos */
	write_reg(&state, &cart, 0xA000, 2);
	write_reg(&state, &cart, 0xE000, 17);
	CHECK_EQ(state.rtc[0], 0); /* RTC también exige RAMG */

	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xC000, 0x0A);
	write_reg(&state, &cart, 0xA000, 1);
	write_reg(&state, &cart, 0xC000, 0x08);
	write_reg(&state, &cart, 0xA000, 2);
	write_reg(&state, &cart, 0xE000, 17);
	CHECK_EQ(state.rtc[0], 17);

	write_reg(&state, &cart, 0xA000, 1);
	write_reg(&state, &cart, 0xC000, 3); /* banco 3 sobre RAM de un banco */
	write_reg(&state, &cart, 0xA000, 2);
	write_reg(&state, &cart, 0xE000, 0x6A);
	CHECK_EQ(test_ram[0], 0x6A);
}

static void transferpak_mbc5_bank_100(void)
{
	uint32_t size = 8u * 1024u * 1024u;
	uint8_t *rom = calloc(1, size);
	CHECK(rom != NULL);
	if (!rom)
		return;
	rom[0x147] = 0x19;
	rom[0x148] = 0x08;
	rom[0x149] = 0;
	uint8_t sum = 0;
	for (unsigned i = 0x134; i <= 0x14C; i++)
		sum = (uint8_t)(sum - rom[i] - 1u);
	rom[0x14D] = sum;
	memset(rom + 0x100u * GB_BANK, 0xA5, SALSA64_TRANSFERPAK_BLOCK);
	struct salsa64_transferpak_cart cart;
	struct salsa64_transferpak_state state;
	CHECK(salsa64_transferpak_cart_init(&cart, rom, size, NULL, 0));
	salsa64_transferpak_reset(&state);
	write_reg(&state, &cart, 0x8000, 0x84);
	write_reg(&state, &cart, 0xB000, 1);
	write_reg(&state, &cart, 0xA000, 0);
	write_reg(&state, &cart, 0xE000, 0); /* bits 0–7 */
	write_reg(&state, &cart, 0xF000, 1); /* bit 8, GB 3000 */
	write_reg(&state, &cart, 0xA000, 1);
	CHECK_EQ(read_first(&state, &cart, 0xC000), 0xA5);
	free(rom);
}

static void transferpak_status_without_cartridge(void)
{
	struct salsa64_transferpak_state state;
	salsa64_transferpak_reset(&state);
	CHECK_EQ(read_first(&state, NULL, 0x8000), 0x00);
	CHECK_EQ(read_first(&state, NULL, 0xB000), 0x00);
	write_reg(&state, NULL, 0x8000, 0x84);
	CHECK_EQ(read_first(&state, NULL, 0x8000), 0x84);
	CHECK_EQ(read_first(&state, NULL, 0xB000), 0x40);
	write_reg(&state, NULL, 0xB000, 1);
	CHECK_EQ(read_first(&state, NULL, 0xB000), 0x40);
	write_reg(&state, NULL, 0x8000, 0xFE);
	CHECK_EQ(read_first(&state, NULL, 0xB000), 0x00);
}

static void transferpak_rejects_bad_cartridges(void)
{
	struct salsa64_transferpak_cart cart;
	make_rom(0x00, 0);
	CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom - 1, NULL, 0));
	uint8_t checksum = test_rom[0x14D];
	test_rom[0x14D] ^= 1;
	CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, NULL, 0));
	test_rom[0x14D] = checksum;
	make_rom(0xFC, 0); /* camera / nonstandard mapper not implemented */
	CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, NULL, 0));
	make_rom(0x03, 0x02);
	CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, NULL, 0));
	CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram, 1024));
	for (uint8_t type = 0x1C; type <= 0x1E; type++) {
		make_rom(type, type == 0x1C ? 0 : 0x02);
		CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram,
						   type == 0x1C ? 0 : 8u * 1024u));
	}
}

void t_suite_transferpak(void)
{
	RUN(transferpak_power_status_and_banking);
	RUN(transferpak_disabled_blocks_cartridge_writes);
	RUN(transferpak_mbc1_banking_and_sram);
	RUN(transferpak_mbc2_nibble_ram);
	RUN(transferpak_file_save_round_trip);
	RUN(transferpak_mbc3_mbc5_banking);
	RUN(transferpak_mbc3_rtc_and_ram_wrap);
	RUN(transferpak_mbc5_bank_100);
	RUN(transferpak_status_without_cartridge);
	RUN(transferpak_rejects_bad_cartridges);
}
