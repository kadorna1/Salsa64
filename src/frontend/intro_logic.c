/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Salsa64 — línea de tiempo, fecha y nieve de la secuencia de arranque (intro_logic.h). */
#include <stdio.h>

#include "intro_logic.h"

void intro_clock_init(struct intro_clock *c)
{
	c->phase = INTRO_POWER;
	c->start = 0.0f;
	c->load_at = -1.0f;
	c->skipped = false;
}

static void go(struct intro_clock *c, enum intro_phase p, float t)
{
	c->phase = p;
	c->start = t;
}

enum intro_phase intro_clock_step(struct intro_clock *c, float t, bool loaded, bool skip)
{
	if (skip && c->phase != INTRO_DONE) {
		c->skipped = true;
		go(c, INTRO_DONE, t);
		return c->phase;
	}
	if (loaded && c->load_at < 0.0f)
		c->load_at = t;
	/* Cada fase puede pasar a la siguiente en el mismo cuadro. */
	for (;;) {
		float in = t - c->start;
		switch (c->phase) {
		case INTRO_POWER:
			if (in < INTRO_POWER_S)
				return c->phase;
			go(c, INTRO_STATIC, c->start + INTRO_POWER_S);
			break;
		case INTRO_STATIC:
			if (in < INTRO_STATIC_MIN_S || c->load_at < 0.0f)
				return c->phase;
			/* El corte empieza cuando se cumplen el mínimo y la carga. */
			go(c, INTRO_CUT, c->start + INTRO_STATIC_MIN_S > c->load_at ? c->start + INTRO_STATIC_MIN_S : c->load_at);
			break;
		case INTRO_CUT:
			if (in < INTRO_CUT_S)
				return c->phase;
			go(c, INTRO_LOGO, c->start + INTRO_CUT_S);
			break;
		case INTRO_LOGO:
			if (in < INTRO_LOGO_S)
				return c->phase;
			go(c, INTRO_FADE, c->start + INTRO_LOGO_S);
			break;
		case INTRO_FADE:
			if (in < INTRO_FADE_S)
				return c->phase;
			go(c, INTRO_DONE, c->start + INTRO_FADE_S);
			return c->phase;
		case INTRO_DONE:
			return c->phase;
		}
	}
}

void intro_vcr_text(uint64_t utc_sec, int tz_minutes, int dst, char date[16], char time[16])
{
	static const char *const months[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
						"JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
	int64_t local = (int64_t)utc_sec + (int64_t)(tz_minutes + (dst ? 60 : 0)) * 60;
	int64_t days = local / 86400;
	int64_t rem = local % 86400;
	if (rem < 0) {
		rem += 86400;
		days--;
	}
	/* Días desde 1970 a fecha civil (algoritmo de Howard Hinnant). */
	int64_t z = days + 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	int64_t doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t y = yoe + era * 400;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;
	int d = (int)(doy - (153 * mp + 2) / 5 + 1);
	int m = (int)(mp < 10 ? mp + 3 : mp - 9);
	if (m <= 2)
		y++;
	snprintf(date, 16, "%s.%02d %04d", months[m - 1], d, (int)y);
	snprintf(time, 16, "%02d:%02d:%02d", (int)(rem / 3600), (int)(rem / 60 % 60), (int)(rem % 60));
}

void intro_noise_fill(uint32_t *seed, uint8_t *rgba, unsigned w, unsigned h)
{
	uint32_t s = *seed ? *seed : 0x9E3779B9u;
	unsigned n = w * h;
	for (unsigned i = 0; i < n; i += 4) {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		for (unsigned k = 0; k < 4 && i + k < n; k++) {
			/* Gris de 16 a 215 (media ~115): nieve densa de grano fino, que deja leer el OSD. */
			unsigned v = 16 + ((s >> (8 * k)) & 0xFF) * 199 / 255;
			uint8_t *p = rgba + (size_t)(i + k) * 4;
			p[0] = p[1] = p[2] = (uint8_t)v;
			p[3] = 255;
		}
	}
	*seed = s;
}
