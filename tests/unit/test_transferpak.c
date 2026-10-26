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
	test_rom[0x149] = ram_code;
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

	CHECK_EQ(read_first(&state, &cart, 0xC000), 0x11); /* Transfer Pak bank 1 -> GB ROM bank 1 */
	write_reg(&state, &cart, 0xA000, 2);
	CHECK_EQ(read_first(&state, &cart, 0xC000), 0xFF); /* Transfer Pak bank 2 maps GB VRAM */
	write_reg(&state, &cart, 0xA000, 3);
	CHECK_EQ(read_first(&state, &cart, 0xC000), 0xFF); /* Transfer Pak bank 3 maps GB work RAM */

	write_reg(&state, &cart, 0xB000, 0);
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

static void transferpak_rejects_bad_cartridges(void)
{
	struct salsa64_transferpak_cart cart;
	make_rom(0x00, 0);
	CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom - 1, NULL, 0));
	make_rom(0xFC, 0); /* camera / nonstandard mapper not implemented */
	CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, NULL, 0));
	make_rom(0x03, 0x02);
	CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, NULL, 0));
	CHECK(!salsa64_transferpak_cart_init(&cart, test_rom, sizeof test_rom, test_ram, 1024));
}

void t_suite_transferpak(void)
{
	RUN(transferpak_power_status_and_banking);
	RUN(transferpak_mbc1_banking_and_sram);
	RUN(transferpak_mbc2_nibble_ram);
	RUN(transferpak_file_save_round_trip);
	RUN(transferpak_rejects_bad_cartridges);
}
