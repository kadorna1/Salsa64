/*
 * Salsa64 — pruebas de los arreglos por juego (core/gamefix.c): solo con el
 * encabezado exacto y las palabras originales en la RDRAM, al empezar un
 * campo, una vez, y nunca en otro juego.
 */
#include "test.h"

#include <string.h>

#include "core/endian.h"
#include "core/gamefix.h"

#define DK64_PA 0x00600914u

static const uint32_t dk64_orig[11] = {
	0x3C038076u, 0x3C048077u, 0x2484AF10u, 0x24637A40u, 0x8C6E0284u, 0x8C8F0000u,
	0x3C018074u, 0x01CFC023u, 0xAC384478u, 0x8C790284u, 0xAC990000u,
};

static uint8_t rom[4096];

static void set_header(const char *code, uint32_t crc1, uint32_t crc2)
{
	memset(rom, 0, sizeof rom);
	salsa64_wr32(rom + 0x10, crc1);
	salsa64_wr32(rom + 0x14, crc2);
	memcpy(rom + 0x3B, code, 4);
	salsa64_bus_set_rom(&t_n64.bus, rom, sizeof rom);
}

static void load_orig(void)
{
	for (unsigned k = 0; k < 11; k++)
		t_wr32(DK64_PA + 4 * k, dk64_orig[k]);
}

static void spin_field(void)
{
	T_LOAD(BEQ(0, 0, 0xFFFF), NOP);
	salsa64_n64_run_field(&t_n64);
}

static void dk64_patched_once(void)
{
	set_header("NDOE", 0xEC58EABFu, 0xAD7C7169u);
	CHECK(salsa64_gamefix_name(&t_n64.bus) != NULL);
	load_orig();
	spin_field();
	CHECK_EQ(t_n64.bus.gamefix_applied, 1);
	CHECK_EQ(t_rd32(DK64_PA + 4 * 2), 0x8C6E7CC4u);   /* lw t6, 0x7CC4(v1) */
	CHECK_EQ(t_rd32(DK64_PA + 4 * 6), 0x2F190001u);   /* sltiu t9, t8, 1 */
	CHECK_EQ(t_rd32(DK64_PA + 4 * 7), 0x0319C021u);   /* addu t8, t8, t9 */
	CHECK_EQ(t_rd32(DK64_PA + 4 * 10), 0x00000000u);
	spin_field();  /* ya arreglado: no se vuelve a aplicar */
	CHECK_EQ(t_n64.bus.gamefix_applied, 1);
	load_orig();   /* el juego vuelve a cargar el código: otra vez */
	spin_field();
	CHECK_EQ(t_n64.bus.gamefix_applied, 2);
	salsa64_bus_set_rom(&t_n64.bus, NULL, 0);
}

static void dk64_other_code_untouched(void)
{
	set_header("NDOE", 0xEC58EABFu, 0xAD7C7169u);
	load_orig();
	t_wr32(DK64_PA + 4 * 9, 0x00000000u);  /* una palabra distinta */
	spin_field();
	CHECK_EQ(t_n64.bus.gamefix_applied, 0);
	CHECK_EQ(t_rd32(DK64_PA + 4 * 2), dk64_orig[2]);
	salsa64_bus_set_rom(&t_n64.bus, NULL, 0);
}

static void other_games_untouched(void)
{
	set_header("NDOP", 0xEC58EABFu, 0xAD7C7169u);  /* otra región */
	CHECK(salsa64_gamefix_name(&t_n64.bus) == NULL);
	set_header("NDOE", 0x0DD4ABABu, 0xB5A2A91Eu);  /* otro CRC (la demo de quiosco) */
	CHECK(salsa64_gamefix_name(&t_n64.bus) == NULL);
	load_orig();
	spin_field();
	CHECK_EQ(t_n64.bus.gamefix_applied, 0);
	CHECK_EQ(t_rd32(DK64_PA + 4 * 2), dk64_orig[2]);
	salsa64_bus_set_rom(&t_n64.bus, NULL, 0);
	CHECK(salsa64_gamefix_name(&t_n64.bus) == NULL);
}

void t_suite_gamefix(void)
{
	RUN(dk64_patched_once);
	RUN(dk64_other_code_untouched);
	RUN(other_games_untouched);
}
