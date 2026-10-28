/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Pruebas de la secuencia de arranque (src/frontend/intro_logic.c): la línea
 * de tiempo (con la carga lista antes y después del mínimo, y el salto), la
 * fecha y la hora de videocasetera y la nieve.
 */
#include <string.h>

#include "frontend/intro_logic.h"
#include "test.h"

#define MIN_TOTAL (INTRO_POWER_S + INTRO_STATIC_MIN_S + INTRO_CUT_S + INTRO_LOGO_S + INTRO_FADE_S)

/* Cuándo termina la secuencia si la carga acaba en `load_s`, a pasos de 1/60 s. */
static float end_time(float load_s, enum intro_phase *seen)
{
	struct intro_clock c;
	intro_clock_init(&c);
	for (int i = 0; i < 6000; i++) {
		float t = (float)i / 60.0f;
		enum intro_phase p = intro_clock_step(&c, t, t >= load_s, false);
		if (seen)
			seen[p] = p;
		if (p == INTRO_DONE)
			return t;
	}
	return -1.0f;
}

static void fast_load(void)
{
	enum intro_phase seen[6] = { 0 };
	float e = end_time(0.2f, seen);
	CHECK(e >= MIN_TOTAL - 0.02f && e <= MIN_TOTAL + 0.04f);
	CHECK(e < 3.0f);  /* unos 3 s a lo más con la carga lista */
	for (int p = 0; p < 6; p++)
		CHECK_EQ((int)seen[p], p);  /* pasó por todas las fases */
}

static void slow_load(void)
{
	/* Si la carga tarda más, la estática sigue hasta que termine. */
	float e = end_time(3.0f, NULL);
	CHECK(e >= 3.0f + INTRO_CUT_S + INTRO_LOGO_S + INTRO_FADE_S - 0.02f);
	CHECK(e <= 3.0f + INTRO_CUT_S + INTRO_LOGO_S + INTRO_FADE_S + 0.04f);
	struct intro_clock c;
	intro_clock_init(&c);
	CHECK_EQ((int)intro_clock_step(&c, 2.9f, false, false), (int)INTRO_STATIC);
	CHECK_EQ((int)intro_clock_step(&c, 3.0f, true, false), (int)INTRO_CUT);
}

static void skip(void)
{
	struct intro_clock c;
	intro_clock_init(&c);
	CHECK_EQ((int)intro_clock_step(&c, 0.1f, false, false), (int)INTRO_POWER);
	CHECK_EQ((int)intro_clock_step(&c, 0.2f, false, true), (int)INTRO_DONE);
	CHECK(c.skipped);
	CHECK_EQ((int)intro_clock_step(&c, 0.3f, true, false), (int)INTRO_DONE);
}

static void vcr(void)
{
	char d[16], t[16];
	intro_vcr_text(1793133906ull, 0, 0, d, t);
	CHECK(!strcmp(d, "OCT.27 2026"));
	CHECK(!strcmp(t, "20:45:06"));
	/* Zona de México (−360 min) con y sin horario de verano. */
	intro_vcr_text(1793133906ull, -360, 0, d, t);
	CHECK(!strcmp(t, "14:45:06"));
	intro_vcr_text(1793133906ull, -360, 1, d, t);
	CHECK(!strcmp(t, "15:45:06"));
	/* El cambio de día y de mes por la zona. */
	intro_vcr_text(1793133906ull + 4 * 3600 + 15 * 60, 0, 0, d, t);
	CHECK(!strcmp(d, "OCT.28 2026"));
	CHECK(!strcmp(t, "01:00:06"));
	intro_vcr_text(0, 0, 0, d, t);
	CHECK(!strcmp(d, "JAN.01 1970"));
	CHECK(!strcmp(t, "00:00:00"));
	intro_vcr_text(0, -60, 0, d, t);  /* antes de 1970 */
	CHECK(!strcmp(d, "DEC.31 1969"));
	CHECK(!strcmp(t, "23:00:00"));
	intro_vcr_text(1709164800ull, 0, 0, d, t);  /* año bisiesto */
	CHECK(!strcmp(d, "FEB.29 2024"));
	intro_vcr_text(1709164800ull + 86400, 0, 0, d, t);
	CHECK(!strcmp(d, "MAR.01 2024"));
}

static void noise(void)
{
	static uint8_t a[64 * 32 * 4], b[64 * 32 * 4];
	uint32_t seed = 1;
	intro_noise_fill(&seed, a, 64, 32);
	intro_noise_fill(&seed, b, 64, 32);
	CHECK(memcmp(a, b, sizeof a));  /* cada cuadro es otro */
	unsigned sum = 0, lo = 255, hi = 0;
	for (unsigned i = 0; i < 64 * 32; i++) {
		unsigned v = a[i * 4];
		CHECK_EQ((int)a[i * 4 + 1], (int)v);
		CHECK_EQ((int)a[i * 4 + 2], (int)v);
		CHECK_EQ((int)a[i * 4 + 3], 255);
		sum += v;
		lo = v < lo ? v : lo;
		hi = v > hi ? v : hi;
	}
	CHECK(lo >= 16 && hi <= 215 && hi - lo > 150);  /* mucho contraste de grano */
	unsigned mean = sum / (64 * 32);
	CHECK(mean > 90 && mean < 150);  /* gris */
	seed = 0;
	intro_noise_fill(&seed, a, 5, 3);  /* tamaños que no son múltiplo de 4 */
	CHECK(seed != 0);
}

void t_suite_intro(void)
{
	RUN(fast_load);
	RUN(slow_load);
	RUN(skip);
	RUN(vcr);
	RUN(noise);
}
