/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Ver thumbs_calc.h. */
#include "frontend/thumbs_calc.h"

#include <math.h>
#include <string.h>

void cover_fit_crop(unsigned w, unsigned h, unsigned tw, unsigned th, unsigned *x, unsigned *y, unsigned *cw, unsigned *ch)
{
	*x = *y = 0;
	*cw = w;
	*ch = h;
	if ((uint64_t)w * th > (uint64_t)h * tw) {  /* más ancha que la caja: se recortan los lados */
		*cw = (unsigned)((uint64_t)h * tw / th);
		if (*cw < 1)
			*cw = 1;
		*x = (w - *cw) / 2;
	} else {                                      /* más alta: arriba y abajo */
		*ch = (unsigned)((uint64_t)w * th / tw);
		if (*ch < 1)
			*ch = 1;
		*y = (h - *ch) / 2;
	}
}

void thumb_scale(const uint8_t *rgba, unsigned w, unsigned h, uint8_t *dst)
{
	const float R = THUMB_RADIUS * THUMB_W;
	unsigned fx, fy, fw, fh;
	cover_fit_crop(w, h, THUMB_W, THUMB_H, &fx, &fy, &fw, &fh);
	for (unsigned y = 0; y < THUMB_H; y++) {
		unsigned sy0 = fy + (unsigned)((uint64_t)y * fh / THUMB_H), sy1 = fy + (unsigned)((uint64_t)(y + 1) * fh / THUMB_H);
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (unsigned x = 0; x < THUMB_W; x++) {
			unsigned sx0 = fx + (unsigned)((uint64_t)x * fw / THUMB_W), sx1 = fx + (unsigned)((uint64_t)(x + 1) * fw / THUMB_W);
			if (sx1 <= sx0)
				sx1 = sx0 + 1;
			uint32_t acc[3] = { 0, 0, 0 };
			for (unsigned j = sy0; j < sy1; j++)
				for (unsigned i = sx0; i < sx1; i++)
					for (int c = 0; c < 3; c++)
						acc[c] += rgba[((size_t)j * w + i) * 4 + c];
			uint32_t n = (sy1 - sy0) * (sx1 - sx0);
			uint8_t *p = dst + ((size_t)y * THUMB_W + x) * 4;
			for (int c = 0; c < 3; c++)
				p[c] = (uint8_t)((acc[c] + n / 2) / n);
			/* Esquinas redondeadas: cobertura de un cuarto de círculo. */
			float cx = (float)x + 0.5f, cy = (float)y + 0.5f;
			float dx = fminf(cx, (float)THUMB_W - cx), dy = fminf(cy, (float)THUMB_H - cy);
			float cov = 1.0f;
			if (dx < R && dy < R) {
				float ex = R - dx, ey = R - dy;
				cov = fminf(1.0f, fmaxf(0.0f, R - sqrtf(ex * ex + ey * ey) + 0.5f));
			}
			p[3] = (uint8_t)(cov * 255.0f + 0.5f);
		}
	}
}

uint32_t thumb_key(const char *name)
{
	uint32_t h = 2166136261u;
	for (const char *p = name; *p; p++)
		h = (h ^ (uint8_t)*p) * 16777619u;
	return h ? h : 1u;
}

void thumb_cache_init(struct thumb_cache *c)
{
	memset(c, 0, sizeof *c);
}

int thumb_cache_find(struct thumb_cache *c, uint32_t key, uint32_t frame)
{
	for (int i = 0; i < THUMB_CELLS; i++)
		if (c->key[i] == key) {
			c->stamp[i] = frame;
			c->hits++;
			return i;
		}
	c->misses++;
	return -1;
}

int thumb_cache_assign(struct thumb_cache *c, uint32_t key, uint32_t frame)
{
	int best = -1;
	for (int i = 0; i < THUMB_CELLS; i++) {
		if (c->key[i] == key)
			return i;
		if (!c->key[i]) {
			best = i;
			break;
		}
		/* El menos usado; los de este cuadro, solo si no hay otros. */
		if (best < 0) {
			best = i;
		} else {
			bool cur_i = c->stamp[i] == frame, cur_b = c->stamp[best] == frame;
			if (cur_i != cur_b ? !cur_i : c->stamp[i] < c->stamp[best])
				best = i;
		}
	}
	c->key[best] = key;
	c->stamp[best] = frame;
	return best;
}

void thumb_cell_origin(int cell, uint32_t *x, uint32_t *y)
{
	*x = (uint32_t)(cell % THUMB_COLS) * THUMB_STEP_X;
	*y = (uint32_t)(cell / THUMB_COLS) * THUMB_STEP_Y;
}

bool thumb_cache_is_missing(const struct thumb_cache *c, uint32_t key)
{
	for (unsigned i = 0; i < c->nmissing; i++)
		if (c->missing[i] == key)
			return true;
	return false;
}

void thumb_cache_mark_missing(struct thumb_cache *c, uint32_t key)
{
	if (thumb_cache_is_missing(c, key))
		return;
	if (c->nmissing < THUMB_MISSING) {
		c->missing[c->nmissing++] = key;
	} else {
		c->missing[c->missing_next] = key;
		c->missing_next = (c->missing_next + 1) % THUMB_MISSING;
	}
}

void thumb_cache_forget_missing(struct thumb_cache *c)
{
	c->nmissing = 0;
	c->missing_next = 0;
}

static void put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static uint32_t get16(const uint8_t *p)
{
	return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

size_t thumb_file_pack(uint8_t *out, const struct thumb_file *f, const uint8_t *px)
{
	size_t len = strlen(f->path);
	if (len > THUMB_PATH_MAX || (f->kind != THUMB_FILE_OK && f->kind != THUMB_FILE_NONE))
		return 0;
	memset(out, 0, THUMB_FILE_HEADER);
	memcpy(out, "SMIN", 4);
	put16(out + 4, 1);
	out[6] = (uint8_t)f->kind;
	put16(out + 8, THUMB_W);
	put16(out + 10, THUMB_H);
	put32(out + 12, f->png_size);
	put32(out + 16, f->png_time);
	put16(out + 20, (uint32_t)len);
	memcpy(out + 22, f->path, len);
	if (f->kind == THUMB_FILE_NONE)
		return THUMB_FILE_HEADER;
	memcpy(out + THUMB_FILE_HEADER, px, THUMB_BYTES);
	return THUMB_FILE_HEADER + THUMB_BYTES;
}

int thumb_file_unpack(const uint8_t *in, size_t size, struct thumb_file *f, const uint8_t **px)
{
	if (size < THUMB_FILE_HEADER || memcmp(in, "SMIN", 4) || get16(in + 4) != 1)
		return -1;
	if (in[6] != THUMB_FILE_OK && in[6] != THUMB_FILE_NONE)
		return -2;
	if (get16(in + 8) != THUMB_W || get16(in + 10) != THUMB_H)
		return -3;
	uint32_t len = get16(in + 20);
	if (len > THUMB_PATH_MAX || 22 + len > THUMB_FILE_HEADER)
		return -4;
	if (in[6] == THUMB_FILE_OK && size != THUMB_FILE_HEADER + THUMB_BYTES)
		return -5;
	f->kind = in[6];
	f->png_size = get32(in + 12);
	f->png_time = get32(in + 16);
	memcpy(f->path, in + 22, len);
	f->path[len] = 0;
	*px = f->kind == THUMB_FILE_OK ? in + THUMB_FILE_HEADER : NULL;
	return 0;
}
