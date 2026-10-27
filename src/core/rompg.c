/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — ROM del cartucho por fragmentos (ver rompg.h).
 *
 * Adaptado de src/core/cart.c de SalsaDS (mismo autor, GPL-2.0-or-later).
 */
#include "core/rompg.h"

#include <stdlib.h>
#include <string.h>

#include "core/endian.h"

#define NO_PAGE 0xFFFFFFFFu

/* Pone en orden z64 los bytes de una página recién leída (las páginas miden un múltiplo de 4 y empiezan alineadas). */
static void page_normalize(uint8_t *p, size_t n, unsigned fmt)
{
	if (fmt == SALSA64_ROM_V64) {
		for (size_t i = 0; i + 1 < n; i += 2) {
			uint8_t t = p[i];
			p[i] = p[i + 1];
			p[i + 1] = t;
		}
	} else if (fmt == SALSA64_ROM_N64) {
		for (size_t i = 0; i + 3 < n; i += 4) {
			uint8_t a = p[i], b = p[i + 1];
			p[i] = p[i + 3];
			p[i + 1] = p[i + 2];
			p[i + 2] = b;
			p[i + 3] = a;
		}
	}
}

static void pg_load(struct salsa64_rompg *g, uint32_t page, uint32_t *slot_out, int ra)
{
	uint32_t best = 0;
	uint64_t oldest = ~0ull;
	const size_t psz = (size_t)1 << g->shift;
	const uint64_t off = (uint64_t)page << g->shift;
	const size_t want = off + psz > g->size ? (size_t)(g->size - off) : psz;
	uint8_t *dst;
	uint64_t t0 = g->clock_us ? g->clock_us() : 0;
	int got;

	for (uint32_t i = 0; i < g->nslots; i++) {
		if (g->slot_page[i] == NO_PAGE) {
			best = i;
			break;
		}
		if (g->slot_stamp[i] < oldest) {
			oldest = g->slot_stamp[i];
			best = i;
		}
	}
	if (g->slot_page[best] != NO_PAGE)
		g->map[g->slot_page[best]] = -1;
	g->last_page = NO_PAGE;  /* la ranura que se pisa pudo ser la última servida */
	dst = g->mem + ((size_t)best << g->shift);
	got = g->rd(g->user, off, dst, want);
	if (got < 0) {
		g->errs++;
		got = 0;
	}
	if ((size_t)got < psz)
		memset(dst + got, 0, psz - (size_t)got);
	page_normalize(dst, (size_t)got, g->fmt);
	g->read_bytes += (uint64_t)got;
	if (g->clock_us) {
		uint64_t dt = g->clock_us() - t0;
		g->read_us += dt;
		if (dt > g->read_max_us)
			g->read_max_us = dt;
	}
	g->slot_page[best] = page;
	g->map[page] = (int32_t)best;
	g->slot_stamp[best] = ++g->stamp;
	if (ra)
		g->ra++;
	else
		g->miss++;
	*slot_out = best;
}

/* Puntero a la página en la caché (la trae si falta). */
static const uint8_t *page_ptr(struct salsa64_rompg *g, uint32_t page)
{
	uint32_t slot;

	if (g->map[page] < 0) {
		pg_load(g, page, &slot, 0);
		/* acceso secuencial (la página anterior está en la caché): se lee la siguiente por adelantado */
		if (page > 0 && g->map[page - 1] >= 0 && page + 1 < g->npages && g->map[page + 1] < 0)
			pg_load(g, page + 1, &slot, 1);
	}
	slot = (uint32_t)g->map[page];
	g->slot_stamp[slot] = ++g->stamp;
	return g->mem + ((size_t)slot << g->shift);
}

int salsa64_rompg_open(struct salsa64_rompg *g, int (*rd)(void *, uint64_t, uint8_t *, size_t), void *user,
		       uint32_t size, enum salsa64_rom_format fmt, unsigned page_shift, uint8_t *mem,
		       size_t mem_bytes)
{
	if (!g || !rd || !mem || page_shift < 12 || page_shift > 24 || (mem_bytes >> page_shift) < 2 || size < 0x1000)
		return -1;
	memset(g, 0, sizeof *g);
	g->rd = rd;
	g->user = user;
	g->mem = mem;
	g->size = size;
	g->shift = page_shift;
	g->fmt = (uint8_t)fmt;
	g->npages = (uint32_t)(((uint64_t)size + ((uint64_t)1 << page_shift) - 1) >> page_shift);
	g->nslots = (uint32_t)(mem_bytes >> page_shift);
	if (g->nslots > g->npages)
		g->nslots = g->npages < 2 ? 2 : g->npages;
	g->last_page = NO_PAGE;
	g->map = malloc(sizeof(int32_t) * g->npages);
	g->slot_page = malloc(sizeof(uint32_t) * g->nslots);
	g->slot_stamp = calloc(g->nslots, sizeof(uint64_t));
	if (!g->map || !g->slot_page || !g->slot_stamp) {
		salsa64_rompg_close(g);
		return -1;
	}
	for (uint32_t i = 0; i < g->npages; i++)
		g->map[i] = -1;
	for (uint32_t i = 0; i < g->nslots; i++)
		g->slot_page[i] = NO_PAGE;
	return 0;
}

void salsa64_rompg_close(struct salsa64_rompg *g)
{
	free(g->map);
	free(g->slot_page);
	free(g->slot_stamp);
	memset(g, 0, sizeof *g);
}

void salsa64_rompg_read(struct salsa64_rompg *g, uint32_t off, uint8_t *dst, uint32_t len)
{
	const uint32_t pmask = (1u << g->shift) - 1u;

	g->acc++;
	while (len) {
		uint32_t page = off >> g->shift, in = off & pmask, n = (pmask + 1u) - in;
		const uint8_t *p = page_ptr(g, page);

		g->last_page = page;
		g->last_ptr = p;
		if (n > len)
			n = len;
		memcpy(dst, p + in, n);
		dst += n;
		off += n;
		len -= n;
	}
}

uint32_t salsa64_rompg_rd32(struct salsa64_rompg *g, uint32_t off)
{
	const uint32_t pmask = (1u << g->shift) - 1u;
	uint8_t b[4];

	if ((off >> g->shift) == g->last_page && (off & pmask) <= pmask - 3u)
		return salsa64_rd32(g->last_ptr + (off & pmask));
	salsa64_rompg_read(g, off, b, 4);
	return salsa64_rd32(b);
}

void salsa64_rompg_window(struct salsa64_rompg *g, uint64_t *acc, uint64_t *miss, uint64_t *ra, uint64_t *us)
{
	*acc = g->acc - g->w_acc;
	*miss = g->miss - g->w_miss;
	*ra = g->ra - g->w_ra;
	*us = g->read_us - g->w_us;
	g->w_acc = g->acc;
	g->w_miss = g->miss;
	g->w_ra = g->ra;
	g->w_us = g->read_us;
}
