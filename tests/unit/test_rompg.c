/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — pruebas de la ROM por fragmentos (src/core/rompg.c y su uso en el bus, el DMA del PI y el arranque).
 *
 * La ROM grande es sintética: una función de la posición (sin datos de juegos) que un lector falso entrega como si fuera
 * el archivo, en cualquiera de los tres órdenes de bytes. Pasa de 64 MB para tocar zonas lejanas, los bordes de página y
 * el final.
 */
#include "test.h"

#include <stdlib.h>
#include <string.h>

#include "core/boot.h"
#include "core/endian.h"
#include "core/rompg.h"

#define HEAD 4096u
#define BIG  ((80u << 20) + 36u)   /* 80 MB y un resto que no es múltiplo de la página ni de 8 */

/* El byte `off` de la ROM en orden z64. */
static uint8_t z64_byte(uint32_t off)
{
	uint32_t h = off * 2654435761u;
	return (uint8_t)((h >> 13) ^ (off >> 9) ^ (off >> 20) * 37u);
}

static int fake_fmt;      /* orden de bytes del «archivo» */
static uint32_t fake_size;
static uint64_t fake_reads;
static uint64_t fake_bytes;

/* El «archivo»: el byte i del archivo es el byte (i ^ x) de la ROM z64 en el orden de la consola. */
static int fake_rd(void *user, uint64_t off, uint8_t *dst, size_t len)
{
	(void)user;
	if (off >= fake_size)
		return 0;
	if (len > fake_size - off)
		len = (size_t)(fake_size - off);
	fake_reads++;
	fake_bytes += len;
	const unsigned x = fake_fmt == SALSA64_ROM_V64 ? 1u : fake_fmt == SALSA64_ROM_N64 ? 3u : 0u;
	for (size_t i = 0; i < len; i++) {
		uint32_t pos = (uint32_t)(off + i) ^ x;
		/* fuera del último grupo completo, el intercambio no cambia nada */
		if (pos >= fake_size)
			pos = (uint32_t)(off + i);
		dst[i] = z64_byte(pos);
	}
	return (int)len;
}

static struct salsa64_rompg pg;
static uint8_t head[HEAD];
static uint8_t *cache;

static bool open_big(int fmt, uint32_t size, unsigned shift, size_t cache_bytes)
{
	fake_fmt = fmt;
	fake_size = size;
	fake_reads = fake_bytes = 0;
	free(cache);
	cache = malloc(cache_bytes);
	if (!cache || salsa64_rompg_open(&pg, fake_rd, NULL, size, (enum salsa64_rom_format)fmt, shift, cache,
					 cache_bytes) != 0)
		return false;
	salsa64_rompg_read(&pg, 0, head, HEAD);   /* la cabecera residente, ya en orden z64 */
	salsa64_bus_set_rom_paged(&t_n64.bus, head, HEAD, size, &pg);
	return true;
}

static void close_big(void)
{
	salsa64_bus_set_rom(&t_n64.bus, NULL, 0);
	salsa64_rompg_close(&pg);
	free(cache);
	cache = NULL;
}

static uint32_t expect32(uint32_t off, uint32_t size)
{
	uint32_t v = 0;
	for (unsigned k = 0; k < 4; k++)
		v = v << 8 | (off + k < size ? z64_byte(off + k) : 0);
	return v;
}

static uint32_t lcg(uint32_t *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s >> 8;
}

/* Lecturas de palabra del CPU en el dominio del cartucho: zonas lejanas, bordes de página, cabecera y final. */
static void cpu_reads_far(void)
{
	static const int fmts[] = { SALSA64_ROM_Z64, SALSA64_ROM_V64, SALSA64_ROM_N64 };
	for (unsigned f = 0; f < 3; f++) {
		const uint32_t size = f == 0 ? BIG : BIG - 36u;  /* los intercambiados, con tamaño múltiplo de 4 */
		CHECK(open_big(fmts[f], size, 18, 4u << 20));
		const uint32_t page = 1u << 18;
		uint32_t offs[64], n = 0, seed = 7;
		offs[n++] = 0;
		offs[n++] = 0x3C;
		offs[n++] = HEAD - 4;   /* última palabra residente */
		offs[n++] = HEAD - 2;   /* a caballo de la cabecera */
		offs[n++] = HEAD;
		offs[n++] = page - 4;
		offs[n++] = page - 2;   /* a caballo entre dos páginas */
		offs[n++] = page;
		offs[n++] = 64u * page - 4;
		offs[n++] = 64u * page - 2;
		offs[n++] = (64u << 20) - 4;
		offs[n++] = (64u << 20);    /* pasado el viejo límite de 64 MB */
		offs[n++] = (64u << 20) + 2;
		offs[n++] = (80u << 20) - 4;
		offs[n++] = size - 4;
		offs[n++] = size - 2;       /* el final: dos bytes de ROM y dos de nada */
		offs[n++] = size - 1 - 1;
		while (n < 64)
			offs[n++] = (lcg(&seed) % (size - 2)) & ~1u;
		for (unsigned i = 0; i < n; i++)
			CHECK_EQ(salsa64_bus_read32(&t_n64.bus, SALSA64_CART_ROM_ADDR + (offs[i] & ~1u)),
				 expect32(offs[i] & ~1u, size));
		/* más allá del final: 0 */
		CHECK_EQ(salsa64_bus_read32(&t_n64.bus, SALSA64_CART_ROM_ADDR + size + 8), 0);
		CHECK(pg.errs == 0);
		close_big();
	}
}

static void pi_dma(uint32_t dram, uint32_t cart, uint32_t len)
{
	salsa64_pi_write(&t_n64.bus, SALSA64_PI_DRAM_ADDR * 4, dram);
	salsa64_pi_write(&t_n64.bus, SALSA64_PI_CART_ADDR * 4, SALSA64_CART_ROM_ADDR + cart);
	salsa64_pi_write(&t_n64.bus, SALSA64_PI_WR_LEN * 4, len - 1);
}

/* DMA del PI cartucho → RDRAM, byte a byte contra la función generadora. */
static void pi_dma_far(void)
{
	static const int fmts[] = { SALSA64_ROM_Z64, SALSA64_ROM_V64, SALSA64_ROM_N64 };
	for (unsigned f = 0; f < 3; f++) {
		const uint32_t size = f == 0 ? BIG : BIG - 36u;
		CHECK(open_big(fmts[f], size, 18, 2u << 20));
		const uint32_t page = 1u << 18;
		const struct { uint32_t cart, len; } cases[] = {
			{ 0, 0x1000 },                      /* solo la cabecera residente */
			{ 0x800, 0x2000 },                  /* de la cabecera a la primera página */
			{ page - 8, 16 },                   /* cruza un borde de página */
			{ 3 * page - 1000, 5000 },
			{ 40u << 20, 3u << 20 },            /* 3 MB seguidos: 12 páginas, más que la caché de 8 */
			{ (64u << 20) - 100, 300 },         /* a caballo del límite viejo */
			{ (70u << 20) + 6, 0x20000 },
			{ size - 6000, 6000 },              /* hasta el último byte */
			{ size - 100, 400 },                /* pasa del final: el resto en 0 */
			{ size + 64, 128 },                 /* todo fuera de la ROM */
		};
		for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
			const uint32_t dram = 0x1000 + (c & 1u) * 2;  /* DRAM par; el borde, en bloques de 8 */
			uint32_t len = cases[c].len;
			if (dram + len > t_n64.bus.rdram_size)
				len = t_n64.bus.rdram_size - dram;
			memset(t_n64.bus.rdram + dram, 0xA5, len);
			pi_dma(dram, cases[c].cart, len);
			unsigned bad = 0;
			for (uint32_t i = 0; i < len; i++) {
				uint32_t o = cases[c].cart + i;
				uint8_t want = o < size ? z64_byte(o) : 0;
				bad += t_n64.bus.rdram[dram + i] != want;
			}
			CHECK_EQ(bad, 0);
		}
		CHECK(pg.errs == 0);
		/* el DMA grande leyó cada página una vez: lecturas del disco en múltiplos de 256 KB */
		CHECK(pg.miss + pg.ra > 0);
		close_big();
	}
}

/* Una caché diminuta (4 páginas de 4 KB) con accesos al azar: el LRU expulsa todo el tiempo y los datos no cambian. */
static void eviction_stress(void)
{
	const uint32_t size = 3u << 20;
	CHECK(open_big(SALSA64_ROM_V64, size, 12, 4u << 12));
	CHECK_EQ(pg.nslots, 4);
	uint32_t seed = 99;
	unsigned bad = 0;
	for (unsigned i = 0; i < 20000; i++) {
		uint32_t off = (lcg(&seed) % (size - 4)) & ~1u;
		bad += salsa64_bus_read32(&t_n64.bus, SALSA64_CART_ROM_ADDR + off) != expect32(off, size);
		if ((i & 255) == 0) {
			uint32_t len = 1 + lcg(&seed) % 0x6000;
			if (off + len > size)
				len = size - off;
			uint8_t buf[0x6000];
			salsa64_bus_rom_read(&t_n64.bus, off, buf, len);
			for (uint32_t k = 0; k < len; k++)
				bad += buf[k] != z64_byte(off + k);
		}
	}
	CHECK_EQ(bad, 0);
	CHECK(pg.miss > 1000);
	/* una repetición dentro de la misma página no vuelve al disco */
	uint64_t reads = fake_reads;
	for (unsigned i = 0; i < 100; i++)
		salsa64_bus_read32(&t_n64.bus, SALSA64_CART_ROM_ADDR + 0x100000 + (i & 7) * 4);
	CHECK(fake_reads <= reads + 2);   /* a lo más la página y su lectura por adelantado */
	close_big();
}

/* Misma ROM entera y por fragmentos: mismas palabras, mismo DMA, mismo arranque. */
static uint8_t whole[0x4000];

static void build_flat(void)
{
	memset(whole, 0, sizeof whole);
	salsa64_wr32(whole, 0x80371240);
	salsa64_wr32(whole + 8, 0x80000400);
	whole[0x3E] = 'E';
	for (uint32_t i = 0x1000; i < sizeof whole; i++)
		whole[i] = z64_byte(i + 12345);
}

static void build_elf_in_whole(uint32_t elf)
{
	uint8_t *e = whole + elf, *ph = e + 0x34;
	salsa64_wr32(e, 0x7F454C46);
	e[4] = 1;
	e[5] = 2;
	e[6] = 1;
	salsa64_wr32(e + 0x18, 0x80001000);
	salsa64_wr32(e + 0x1C, 0x34);
	salsa64_wr16(e + 0x2A, 0x20);
	salsa64_wr16(e + 0x2C, 1);
	salsa64_wr32(ph + 0x00, 1);
	salsa64_wr32(ph + 0x04, 0x100);
	salsa64_wr32(ph + 0x08, 0x80001000);
	salsa64_wr32(ph + 0x0C, 0x80001000);
	salsa64_wr32(ph + 0x10, 0x900);   /* 2304 bytes: cruza la página de 4 KB */
	salsa64_wr32(ph + 0x14, 0xA00);
	salsa64_wr32(ph + 0x18, 5);
}

static int mem_rd(void *user, uint64_t off, uint8_t *dst, size_t len)
{
	(void)user;
	if (off >= sizeof whole)
		return 0;
	if (len > sizeof whole - off)
		len = sizeof whole - (size_t)off;
	memcpy(dst, whole + off, len);
	return (int)len;
}

static void boot_whole_vs_paged(void)
{
	static uint8_t pcache[8u << 12];
	static uint8_t ram_a[0x4000];
	struct salsa64_boot_info ia, ib;

	for (unsigned mode = 0; mode < 2; mode++) {   /* 0: carga plana; 1: ELF libdragon que cruza páginas */
		build_flat();
		if (mode)
			build_elf_in_whole(0x1900);
		salsa64_bus_set_rom(&t_n64.bus, whole, sizeof whole);
		CHECK(salsa64_boot_hle(&t_n64, SALSA64_BOOT_TV_AUTO, &ia));
		memcpy(ram_a, t_n64.bus.rdram + 0x1000, sizeof ram_a);
		uint64_t pc_a = CPU->pc;

		CHECK(salsa64_rompg_open(&pg, mem_rd, NULL, sizeof whole, SALSA64_ROM_Z64, 12, pcache, sizeof pcache) == 0);
		uint8_t h[HEAD];
		memcpy(h, whole, HEAD);
		salsa64_bus_set_rom_paged(&t_n64.bus, h, HEAD, sizeof whole, &pg);
		CHECK(salsa64_boot_hle(&t_n64, SALSA64_BOOT_TV_AUTO, &ib));
		CHECK_EQ(ib.kind, ia.kind);
		CHECK_EQ(ib.entry, ia.entry);
		CHECK_EQ(ib.loaded, ia.loaded);
		CHECK_EQ(ib.elf_offset, ia.elf_offset);
		CHECK_EQ(CPU->pc, pc_a);
		CHECK(memcmp(ram_a, t_n64.bus.rdram + 0x1000, sizeof ram_a) == 0);
		CHECK_EQ(ib.kind, mode ? SALSA64_BOOT_ELF : SALSA64_BOOT_FLAT);
		if (mode)
			CHECK(ib.loaded == 0x900);
		salsa64_bus_set_rom(&t_n64.bus, NULL, 0);
		salsa64_rompg_close(&pg);
	}
}

static void open_rejects(void)
{
	static uint8_t mem[1u << 20];
	struct salsa64_rompg g;

	CHECK(salsa64_rompg_open(&g, fake_rd, NULL, 1u << 20, SALSA64_ROM_Z64, 18, mem, 1u << 18) != 0);   /* una ranura */
	CHECK(salsa64_rompg_open(&g, fake_rd, NULL, 0x800, SALSA64_ROM_Z64, 12, mem, sizeof mem) != 0);    /* menos de 4 KB */
	CHECK(salsa64_rompg_open(&g, fake_rd, NULL, 1u << 20, SALSA64_ROM_Z64, 8, mem, sizeof mem) != 0);  /* página < 4 KB */
}

void t_suite_rompg(void)
{
	t_run("rompg: lecturas del CPU en zonas lejanas y bordes", cpu_reads_far);
	t_run("rompg: DMA del PI de una ROM de 80 MB", pi_dma_far);
	t_run("rompg: expulsión con una caché diminuta", eviction_stress);
	t_run("rompg: arranque entero y por fragmentos", boot_whole_vs_paged);
	t_run("rompg: rechaza aperturas imposibles", open_rejects);
}
