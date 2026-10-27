/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — lo que comparten el fondo «Shader» del menú (fondo.c, en el PPE)
 * y su referencia en la Mac (tools/fondo-ref.c): el hash del original, sus
 * estrellas «en bruto», el tramado y el degradado de día o de noche.
 *
 * Basado en el shader «XdGfRR» de Shadertoy, de int_45h
 * (https://www.shadertoy.com/view/XdGfRR): «just do whatever you want with
 * this shader», con un comentario de crédito. Ver docs/THIRD_PARTY.md.
 */
#ifndef SALSA64_FONDO_REF_H
#define SALSA64_FONDO_REF_H

#include <math.h>
#include <stdint.h>

#define FONDO_THRESHOLD 0.99f
#define FONDO_MIN_DIST  0.04f
#define FONDO_MAX_DIST  40.0f
#define FONDO_MAX_DRAWS 40

/* hash12 del original, en un punto entero de la red: uvec2(ivec2(p)). */
static inline float fondo_hash12i(int32_t x, int32_t y)
{
	uint32_t qx = (uint32_t)x * 1597334673u, qy = (uint32_t)y * 3812015801u;
	uint32_t n = (qx ^ qy) * 1597334673u;
	return (float)n * 2.328306437080797e-10f;
}

static inline float fondo_hash12(float x, float y)
{
	return fondo_hash12i((int32_t)x, (int32_t)y);
}

/* get_stars_rough del original. */
static inline float fondo_star_rough_h(float h)
{
	float t = (h - FONDO_THRESHOLD) / (1.0f - FONDO_THRESHOLD);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	float s = t * t * (3.0f - 2.0f * t);
	if (s >= FONDO_THRESHOLD)
		s = powf((s - FONDO_THRESHOLD) / (1.0f - FONDO_THRESHOLD), 10.0f);
	return s;
}

static inline float fondo_star_rough(float x, float y)
{
	return fondo_star_rough_h(fondo_hash12(x, y));
}

/* dither del original (U en píxeles de Shadertoy, con la y hacia arriba). */
static inline float fondo_dither(float x, float y)
{
	float d = x * 0.06711056f + y * 0.00583715f;
	d -= floorf(d);
	d *= 52.9829189f;
	return d - floorf(d);
}

/* Los cuatro colores del degradado a la hora `day_s` (segundos desde la
 * medianoche, el iDate.w de Shadertoy): l1 abajo a la izquierda, l2 abajo a
 * la derecha, l3 arriba a la izquierda y l4 arriba a la derecha (la y del
 * original va hacia arriba). A la medianoche, los grises; a mediodía, el
 * azul extrapolado (p = −1). */
static inline void fondo_corners(float day_s, float l[4][3])
{
	static const float day[4][3] = {
		{ 0.149f, 0.471f, 0.569f }, { 0.075f, 0.333f, 0.412f },
		{ 0.063f, 0.329f, 0.412f }, { 0.169f, 0.482f, 0.580f },
	};
	static const float night[4][3] = {
		{ 0.231f, 0.231f, 0.231f }, { 0.129f, 0.129f, 0.129f },
		{ 0.149f, 0.149f, 0.149f }, { 0.251f, 0.251f, 0.251f },
	};
	float q = day_s / 86400.0f;
	float p = sinf(2.0f * 3.14159265358979f * q + 0.5f * 3.14159265358979f);
	for (int i = 0; i < 4; i++)
		for (int k = 0; k < 3; k++)
			l[i][k] = day[i][k] + (night[i][k] - day[i][k]) * p;
}

static inline void fondo_gradient(float day_s, float ux, float uy, float c[3])
{
	float l[4][3];
	fondo_corners(day_s, l);
	for (int k = 0; k < 3; k++) {
		float a = l[0][k] + (l[1][k] - l[0][k]) * ux, b = l[2][k] + (l[3][k] - l[2][k]) * ux;
		c[k] = a + (b - a) * uy;
	}
}

#endif
