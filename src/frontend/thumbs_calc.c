/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Ver thumbs_calc.h. */
#include "frontend/thumbs_calc.h"

#include <math.h>
#include <string.h>

void thumb_scale(const uint8_t *rgba, unsigned w, unsigned h, uint8_t *dst)
{
	const float R = THUMB_RADIUS * THUMB_W;
	for (unsigned y = 0; y < THUMB_H; y++) {
		unsigned sy0 = (unsigned)((uint64_t)y * h / THUMB_H), sy1 = (unsigned)((uint64_t)(y + 1) * h / THUMB_H);
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (unsigned x = 0; x < THUMB_W; x++) {
			unsigned sx0 = (unsigned)((uint64_t)x * w / THUMB_W), sx1 = (unsigned)((uint64_t)(x + 1) * w / THUMB_W);
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
