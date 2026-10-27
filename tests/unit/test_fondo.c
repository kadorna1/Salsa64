/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — pruebas de lo que el fondo «Shader» del menú calcula en la CPU
 * (src/frontend/fondo_calc.c): fp16, la primitiva de la ola, los anillos
 * del hash (rellenar de a poco da lo mismo que rellenar todo), la malla
 * sobre las superficies g = ±0,04, la tabla de h y las constantes afines de
 * la pasada final. La imagen se compara contra el shader original con
 * tools/fondo-ref.c (docs/fondo-shader.md).
 */
#include "test.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/fondo_calc.h"

static void fondo_half_roundtrip(void)
{
	static const float v[] = { 0.0f, 1.0f, -2.5f, 3.14159f, 39.99f, 65504.0f, 1e-5f, 0.1f, -1000.0f };
	for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
		float r = fondo_unhalf(fondo_half(v[i]));
		CHECK(fabsf(r - v[i]) <= fabsf(v[i]) * (1.0f / 2048.0f) + 6e-8f);
	}
	CHECK_EQ(fondo_half(1.0f), 0x3C00u);
	CHECK_EQ(fondo_half(-2.0f), 0xC000u);
}

static float smoothstep(float a, float b, float x)
{
	float t = (x - a) / (b - a);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return t * t * (3 - 2 * t);
}

static void fondo_prim_derivative(void)
{
	CHECK(fabsf(fondo_prim(0.0f) + FONDO_C0) < 1e-6f);
	float prev = fondo_prim(0.0f);
	for (float t = 0.5f; t <= 40.0f; t += 0.5f) {
		float p = fondo_prim(t);
		CHECK(p >= prev);
		prev = p;
	}
	/* Su derivada es lo que suma el original por unidad de t dentro de la
	 * cáscara: 0,015 por paso de 0,05 por smoothstep(0, 0,3, 0,009·t). */
	static const float ts[] = { 5.0f, 20.0f, 33.0f, 38.0f };
	for (unsigned i = 0; i < sizeof ts / sizeof ts[0]; i++) {
		float d = (fondo_prim(ts[i] + 0.01f) - fondo_prim(ts[i] - 0.01f)) / 0.02f;
		CHECK(fabsf(d - 0.3f * smoothstep(0.0f, 0.3f, ts[i] * 0.9f / 100.0f)) < 2e-3f);
	}
}

static void fondo_rings_incremental(void)
{
	struct fondo_view vw = { 1280, 720, 160, 90 };
	struct fondo_tex a, b;
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	a.star = calloc((size_t)FONDO_STAR_W * FONDO_STAR_H * 4, 1);
	a.bri = calloc((size_t)FONDO_BRI_W * FONDO_BRI_H * 4, 1);
	b.star = calloc((size_t)FONDO_STAR_W * FONDO_STAR_H * 4, 1);
	b.bri = calloc((size_t)FONDO_BRI_W * FONDO_BRI_H * 4, 1);
	CHECK(a.star && a.bri && b.star && b.bri);
	uint32_t first = 0, later = 0;
	for (int f = 0; f <= 90; f++) {
		fondo_tex_update(&a, &vw, 100.0 + f / 30.0);
		*(f ? &later : &first) += a.filled;
	}
	/* Después del primer cuadro solo entra lo nuevo: unas columnas y filas
	 * (en 3 s, la capa 3 corre 120 celdas y todas 30 filas). */
	CHECK(first > 1000000u && later < first / 4);
	fondo_tex_update(&b, &vw, 100.0 + 90 / 30.0);
	for (int k = 0; k < 2 * FONDO_LAYERS; k++) {
		const struct fondo_ring *r = &b.ring[k];
		const uint8_t *ta = k < FONDO_LAYERS ? a.star : a.bri, *tb = k < FONDO_LAYERS ? b.star : b.bri;
		uint32_t tw = k < FONDO_LAYERS ? FONDO_STAR_W : FONDO_BRI_W, th = k < FONDO_LAYERS ? FONDO_STAR_H : FONDO_BRI_H;
		CHECK(r->valid && r->x1 - r->x0 <= (int64_t)tw && r->y1 - r->y0 <= (int64_t)th);
		uint32_t diff = 0;
		for (int64_t y = r->y0; y < r->y1; y++)
			for (int64_t x = r->x0; x < r->x1; x++) {
				size_t o = (((uint64_t)y & (th - 1)) * tw + ((uint64_t)x & (tw - 1))) * 4 + k % FONDO_LAYERS;
				diff += ta[o] != tb[o];
			}
		CHECK_EQ(diff, 0u);
	}
	free(a.star), free(a.bri), free(b.star), free(b.bri);
}

static void fondo_mesh_on_surface(void)
{
	struct fondo_view vw = { 1920, 1080, 240, 135 };
	fondo_vertex *v = malloc(sizeof *v * FONDO_VERTS);
	CHECK(v != NULL);
	double T = 47.3;
	fondo_mesh(v, &vw, T);
	static const uint32_t pick[][2] = { { 0, 0 }, { 32, 100 }, { 64, 256 }, { 7, 200 } };
	for (unsigned s = 0; s < 2; s++)
		for (unsigned i = 0; i < sizeof pick / sizeof pick[0]; i++) {
			const fondo_vertex *p =
				&v[(s ? FONDO_VERT_SURF_N : FONDO_VERT_SURF_P) + pick[i][1] * (FONDO_NU + 1) + pick[i][0]];
			/* De la posición en la pantalla al rayo (con el AA del
			 * original: el píxel es el rayo de Ue = U − 0,25). */
			double z = p->pos[3], X = p->pos[0] / z, Y = p->pos[1] / z;
			double xw = (X + 1.0) * 0.5 * vw.w, yw = (1.0 - Y) * 0.5 * vw.h;
			double dx = (xw - 0.25 - 0.5 * vw.w) / vw.h, dy = (0.5 * vw.h - yw - 0.25) / vw.h;
			double g = FONDO_A * dy * z + fondo_h(dx * z, z, T);
			CHECK(fabs(g - (s ? -FONDO_SHELL : FONDO_SHELL)) < 1e-3);
			CHECK(fabsf(p->t0[0] - (float)z) < 1e-4f);
		}
	/* Las tapas son las filas de los extremos. */
	CHECK(!memcmp(&v[FONDO_VERT_CAP_FAR], &v[FONDO_VERT_SURF_P + FONDO_NZ * (FONDO_NU + 1)], sizeof *v));
	CHECK(!memcmp(&v[FONDO_VERT_CAP_NEAR + 1], &v[FONDO_VERT_SURF_N], sizeof *v));
	uint16_t *idx = malloc(sizeof *idx * FONDO_INDICES);
	CHECK(idx != NULL);
	fondo_indices(idx);
	uint16_t mx = 0;
	for (uint32_t i = 0; i < FONDO_SURF_INDICES; i++)
		mx = idx[i] > mx ? idx[i] : mx;
	CHECK_EQ(mx, (uint16_t)(FONDO_SURF_VERTS - 1));
	for (uint32_t i = 0; i < FONDO_CAP_INDICES; i++)
		CHECK(idx[FONDO_SURF_INDICES + i] < FONDO_CAP_VERTS);
	free(idx);
	free(v);
}

static void fondo_htable_values(void)
{
	struct fondo_view vw = { 1280, 720, 160, 90 };
	uint32_t pitch = FONDO_NT * 8;
	uint16_t *tab = malloc((size_t)(vw.lw / 4) * pitch);
	CHECK(tab != NULL);
	double T = 12.5;
	fondo_htable(tab, pitch, &vw, T);
	static const uint32_t cols[] = { 0, 37, 80, 159 }, js[] = { 0, 13, 256, 511 };
	for (unsigned c = 0; c < 4; c++)
		for (unsigned k = 0; k < 4; k++) {
			uint32_t i = cols[c], j = js[k];
			double t = FONDO_MAX_DIST * j / (FONDO_NT - 1), dx = fondo_low_dx(&vw, i);
			double want = fondo_h(dx * t, t, T) - 2.5 - FONDO_B * t;
			const uint16_t *row = (const uint16_t *)((const uint8_t *)tab + (size_t)(i / 4) * pitch);
			CHECK(fabs(fondo_unhalf(row[j * 4 + i % 4]) - want) < 0.005);
		}
	free(tab);
}

static void fondo_affine_consts(void)
{
	struct fondo_view vw = { 1920, 1080, 240, 135 };
	struct fondo_frame fr;
	fondo_frame_consts(&fr, &vw, 33.0, 6 * 3600.0);
	static const int px[] = { 0, 960, 1919 }, py[] = { 0, 333, 1079 };
	for (int i = 0; i < 3; i++) {
		float v[5][4];
		for (int j = 0; j < 5; j++)
			for (int c = 0; c < 4; c++)
				v[j][c] = fr.k[j][c] + fr.kx[j][c] * (px[i] + 0.5f) + fr.ky[j][c] * (py[i] + 0.5f);
		/* El tramado: dot(U, k) en el píxel de Shadertoy (la y hacia arriba). */
		double ux = px[i] + 0.5, uy = vw.h - py[i] - 0.5;
		CHECK(fabs(v[3][3] - (ux * 0.06711056 + uy * 0.00583715)) < 1e-3);
		/* La y del píxel con el AA del original, y el degradado completo. */
		CHECK(fabs(v[4][3] - (uy - 0.25)) < 1e-3);
		float c[3];
		fondo_gradient(6 * 3600.0f, (float)((ux - 0.25) / vw.w), (float)((uy - 0.25) / vw.h), c);
		for (int k = 0; k < 3; k++)
			CHECK(fabsf(v[4][k] * v[4][3] + v[3][k] - c[k]) < 1e-4f);
		/* Capa 1: la red avanza 0,1·2000/H celdas por píxel. */
		float p0 = fr.k[0][0] + fr.kx[0][0] * 0.5f, p1 = fr.k[0][0] + fr.kx[0][0] * 1.5f;
		CHECK(fabsf(p1 - p0 - 200.0f / vw.h) < 1e-4f);
	}
	CHECK(fr.band_y0 <= fr.band_y1 && fr.band_y1 <= vw.h);
	CHECK_EQ((uint32_t)fr.unpack_k[0], vw.lw / 4);
}

void t_suite_fondo(void)
{
	RUN(fondo_half_roundtrip);
	RUN(fondo_prim_derivative);
	RUN(fondo_rings_incremental);
	RUN(fondo_mesh_on_surface);
	RUN(fondo_htable_values);
	RUN(fondo_affine_consts);
}
