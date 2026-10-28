/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — pruebas de la malla del fondo «Seda» del menú
 * (src/frontend/seda_calc.c): las posiciones contra una evaluación
 * independiente en doble precisión con el seno de libm, que el camino por
 * filas del SPE da lo mismo que la capa entera del PPE, la normal y la
 * proyección, que el periodo de las fases cierra sin saltos, los índices,
 * los destellos y la paleta. El aspecto se revisa con tools/seda-vista.sh.
 */
#include "test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "frontend/seda_calc.h"

static float pos_a[SEDA_LAYER_VERTS][4], pos_b[SEDA_LAYER_VERTS][4];
static seda_vertex va[SEDA_LAYER_VERTS], vb[SEDA_LAYER_VERTS];

/* ---- La misma fórmula en doble precisión (con sin de libm) --------------------------- */

struct ref_layer {
	double zc, yc, lx, hw, twk, ph0;
	int dm;
};

static const struct ref_layer REF[SEDA_LAYERS] = {
	{ 3.54, -0.231, 2.55, 0.323, 3.6, 6.283, 0 },
	{ 3.55, -0.38, 3.00, 0.472, 4.4, 2.1, 9 },
	{ 4.20, 0.46, 3.55, 0.74, 2.6, 4.0, -6 },
};

static double rph(int m, double tf)
{
	return 2.0 * M_PI * m * tf;
}

static void ref_axis(const struct ref_layer *L, double aspect, double s, double tf, double *c)
{
	c[0] = L->lx * aspect * s;
	c[1] = L->yc + 0.60 * sin(3.1 * s + rph(70 + L->dm, tf) + L->ph0) +
	       0.24 * sin(6.7 * s - rph(95 + L->dm, tf) + 1.3 * L->ph0);
	c[2] = L->zc + 0.55 * sin(2.5 * s + rph(55 + L->dm, tf) + 0.7 * L->ph0) + 0.20 * sin(5.6 * s - rph(80 + L->dm, tf));
}

static void v3_cross(const double *a, const double *b, double *o)
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}

static void v3_norm(double *v)
{
	double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	v[0] /= l, v[1] /= l, v[2] /= l;
}

static void ref_pos(int layer, double aspect, double tf, int i, int j, double *p)
{
	const struct ref_layer *L = &REF[layer];
	double s = -1.0 + 2.0 * i / (SEDA_NU - 1), v = -1.0 + 2.0 * j / (SEDA_NV - 1);
	double c[3], cp[3], cm[3], t[3], b[3], n2[3], d[3], m[3];
	ref_axis(L, aspect, s, tf, c);
	ref_axis(L, aspect, s + 0.004, tf, cp);
	ref_axis(L, aspect, s - 0.004, tf, cm);
	for (int k = 0; k < 3; k++)
		t[k] = cp[k] - cm[k];
	v3_norm(t);
	static const double up[3] = { 0, 1, 0 };
	v3_cross(t, up, b);
	v3_norm(b);
	v3_cross(b, t, n2);
	double phi = L->twk * s + 1.2 * sin(2.2 * s + rph(40 + L->dm, tf) + L->ph0) + rph(20 + L->dm, tf);
	for (int k = 0; k < 3; k++)
		d[k] = cos(phi) * n2[k] + sin(phi) * b[k];
	v3_cross(t, d, m);
	double hw = L->hw * (1.0 + 0.28 * sin(2.7 * s + rph(35 + L->dm, tf) + L->ph0));
	double h = 0.075 * sin(5.0 * s + 2.0 * v + rph(85 + L->dm, tf) + L->ph0) +
		   0.050 * sin(9.0 * s - 3.5 * v - rph(60 + L->dm, tf) + 1.7 * L->ph0) +
		   0.034 * sin(14.0 * s + 5.0 * v + rph(110 + L->dm, tf) + 2.9 * L->ph0) +
		   0.020 * sin(21.0 * s - 7.0 * v - rph(140 + L->dm, tf) + 0.4);
	h *= 0.45 + 0.55 * v * v;
	for (int k = 0; k < 3; k++)
		p[k] = c[k] + v * hw * d[k] + h * m[k];
}

static void seda_positions_match_reference(void)
{
	static const float tfs[] = { 0.0f, 0.1234f, 0.5f, 0.9876f };
	static const float aspects[] = { 4.0f / 3.0f, 16.0f / 9.0f };
	double worst = 0.0;
	for (int l = 0; l < SEDA_LAYERS; l++)
		for (unsigned a = 0; a < 2; a++)
			for (unsigned k = 0; k < 4; k++) {
				seda_positions(l, aspects[a], tfs[k], pos_a);
				for (int i = 0; i < SEDA_NU; i += 3)
					for (int j = 0; j < SEDA_NV; j++) {
						double r[3];
						ref_pos(l, aspects[a], tfs[k], i, j, r);
						for (int c = 0; c < 3; c++) {
							double e = fabs(r[c] - pos_a[i * SEDA_NV + j][c]);
							if (e > worst)
								worst = e;
						}
					}
			}
	/* El seno de la malla es un polinomio en float: unos 1e-4 de error en
	 * una posición que mide unidades (más la torsión, que lo amplifica). */
	if (worst > 2e-3)
		printf("   error máximo de posición %.2e\n", worst);
	CHECK(worst < 2e-3);
}

static void seda_rows_equal_layer(void)
{
	/* El SPE calcula las posiciones y después fila por fila; el PPE, la capa
	 * entera: los mismos vértices, bit a bit. */
	for (int l = 0; l < SEDA_LAYERS; l++) {
		seda_layer(l, 16.0f / 9.0f, 0.3f, pos_a, va);
		seda_positions(l, 16.0f / 9.0f, 0.3f, pos_b);
		for (int i = SEDA_NU - 1; i >= 0; i--)  /* en otro orden, a ver que no dependa */
			seda_row(l, 16.0f / 9.0f, (const float(*)[4])pos_b, i, vb + i * SEDA_NV);
		CHECK(!memcmp(va, vb, sizeof va));
	}
	/* Determinista. */
	seda_layer(1, 1.5f, 0.7f, pos_a, va);
	seda_layer(1, 1.5f, 0.7f, pos_b, vb);
	CHECK(!memcmp(va, vb, sizeof va));
	/* Las capas son distintas. */
	seda_layer(0, 1.5f, 0.7f, pos_a, va);
	CHECK(memcmp(va, vb, sizeof va) != 0);
}

static void seda_normals_and_projection(void)
{
	const float aspect = 16.0f / 9.0f;
	for (int l = 0; l < SEDA_LAYERS; l++)
		for (float tf = 0.0f; tf < 1.0f; tf += 0.173f) {
			seda_layer(l, aspect, tf, pos_a, va);
			int inside = 0;
			for (int i = 0; i < SEDA_NU; i++)
				for (int j = 0; j < SEDA_NV; j++) {
					const seda_vertex *v = &va[i * SEDA_NV + j];
					const float *p = pos_a[i * SEDA_NV + j];
					/* La normal de la red, en doble precisión, y el brillo de Fresnel. */
					int ip = i < SEDA_NU - 1 ? i + 1 : i, im = i > 0 ? i - 1 : i;
					int jp = j < SEDA_NV - 1 ? j + 1 : j, jm = j > 0 ? j - 1 : j;
					double du[3], dv[3], n[3], vw[3];
					for (int c = 0; c < 3; c++) {
						du[c] = (double)pos_a[ip * SEDA_NV + j][c] - pos_a[im * SEDA_NV + j][c];
						dv[c] = (double)pos_a[i * SEDA_NV + jp][c] - pos_a[i * SEDA_NV + jm][c];
						vw[c] = -(double)p[c];
					}
					v3_cross(du, dv, n);
					v3_norm(n);
					v3_norm(vw);
					double f = fabs(n[0] * vw[0] + n[1] * vw[1] + n[2] * vw[2]);
					CHECK(fabs(f - v->t0[0]) < 2e-4);
					CHECK(v->t0[0] >= 0.0f && v->t0[0] <= 1.0001f);
					CHECK(v->t0[1] >= -1.0001f && v->t0[1] <= 1.0001f);
					CHECK(v->t0[2] > 0.6f && v->t0[2] <= 1.0f);
					CHECK(v->t0[3] >= 0.0f && v->t0[3] <= 1.0f);
					/* Proyección: w = profundidad, delante de la cámara. */
					CHECK(v->pos[3] > 1.5f && v->pos[3] == p[2]);
					CHECK(fabsf(v->pos[0] / v->pos[3] - p[0] * (1.7f * 0.503f * 2.0f / aspect) / p[2]) < 1e-5f);
					CHECK(fabsf(v->pos[1] / v->pos[3] - (p[1] * 1.7f * 0.503f / p[2] + 0.032f)) < 1e-5f);
					if (fabsf(v->pos[0] / v->pos[3]) < 1.0f && fabsf(v->pos[1] / v->pos[3]) < 1.0f)
						inside++;
				}
			/* Cada capa pasa por la pantalla. */
			CHECK(inside > SEDA_LAYER_VERTS / 4);
		}
}

static void seda_ribbon_is_continuous(void)
{
	/* Vértices vecinos, cerca: la tela no se rompe ni da saltos. */
	for (int l = 0; l < SEDA_LAYERS; l++)
		for (float tf = 0.0f; tf < 1.0f; tf += 0.31f) {
			seda_positions(l, 16.0f / 9.0f, tf, pos_a);
			for (int i = 0; i + 1 < SEDA_NU; i++)
				for (int j = 0; j < SEDA_NV; j++) {
					const float *p = pos_a[i * SEDA_NV + j], *q = pos_a[(i + 1) * SEDA_NV + j];
					double d = hypot(hypot(p[0] - q[0], p[1] - q[1]), p[2] - q[2]);
					CHECK(d < 0.2);
				}
			for (int i = 0; i < SEDA_NU; i++)
				for (int j = 0; j + 1 < SEDA_NV; j++) {
					const float *p = pos_a[i * SEDA_NV + j], *q = pos_a[i * SEDA_NV + j + 1];
					double d = hypot(hypot(p[0] - q[0], p[1] - q[1]), p[2] - q[2]);
					CHECK(d < 0.2);
				}
		}
}

static void seda_period_wraps_seamlessly(void)
{
	/* A los 600 s el cuadro es el mismo que a los 0: ninguna onda salta. */
	CHECK_EQ(seda_phase(0.0), 0.0f);
	CHECK(seda_phase(SEDA_PERIOD) == 0.0f);
	CHECK(fabsf(seda_phase(150.0) - 0.25f) < 1e-6f);
	CHECK(fabsf(seda_phase(-150.0) - 0.75f) < 1e-6f);
	CHECK(fabsf(seda_phase(123456.0) - (float)fmod(123456.0 / SEDA_PERIOD, 1.0)) < 1e-6f);
	/* Justo antes y justo después del reinicio, la malla casi no cambia. */
	seda_positions(0, 1.7f, seda_phase(599.95), pos_a);
	seda_positions(0, 1.7f, seda_phase(600.05), pos_b);
	double worst = 0.0;
	for (int k = 0; k < SEDA_LAYER_VERTS; k++)
		for (int c = 0; c < 3; c++)
			worst = fmax(worst, fabs(pos_a[k][c] - pos_b[k][c]));
	CHECK(worst < 0.25);  /* 0,1 s de movimiento; sin reinicio sería de unidades */
	/* Y es lo mismo en los dos lados del reinicio: dentro del error de float. */
	seda_positions(2, 1.7f, 0.0f, pos_a);
	seda_positions(2, 1.7f, seda_phase(SEDA_PERIOD * 7.0), pos_b);
	for (int k = 0; k < SEDA_LAYER_VERTS; k++)
		for (int c = 0; c < 3; c++)
			CHECK(fabsf(pos_a[k][c] - pos_b[k][c]) < 1e-5f);
}

static void seda_moves(void)
{
	/* La tela se mueve: en 1 s cambia, pero no se desarma. */
	seda_positions(0, 1.7f, seda_phase(10.0), pos_a);
	seda_positions(0, 1.7f, seda_phase(11.0), pos_b);
	double sum = 0.0, worst = 0.0;
	for (int k = 0; k < SEDA_LAYER_VERTS; k++) {
		double d = hypot(hypot(pos_a[k][0] - pos_b[k][0], pos_a[k][1] - pos_b[k][1]), pos_a[k][2] - pos_b[k][2]);
		sum += d;
		worst = fmax(worst, d);
	}
	CHECK(sum / SEDA_LAYER_VERTS > 0.02);
	CHECK(worst < 2.0);
}

static void seda_index_buffers(void)
{
	static uint16_t idx[SEDA_INDICES], sidx[SEDA_SPARK_INDICES];
	static unsigned seen[SEDA_LAYER_VERTS], sseen[SEDA_SPARK_VERTS];
	seda_indices(idx);
	for (unsigned i = 0; i < SEDA_INDICES; i++) {
		CHECK(idx[i] < SEDA_LAYER_VERTS);
		seen[idx[i]]++;
	}
	for (unsigned i = 0; i < SEDA_LAYER_VERTS; i++)
		CHECK(seen[i] > 0);
	/* Triángulos no degenerados. */
	for (unsigned i = 0; i < SEDA_INDICES; i += 3)
		CHECK(idx[i] != idx[i + 1] && idx[i + 1] != idx[i + 2] && idx[i] != idx[i + 2]);
	CHECK(SEDA_VERTS * 32u < 65536u * 32u);
	seda_spark_indices(sidx);
	for (unsigned i = 0; i < SEDA_SPARK_INDICES; i++) {
		CHECK(sidx[i] < SEDA_SPARK_VERTS);
		sseen[sidx[i]]++;
	}
	for (unsigned i = 0; i < SEDA_SPARK_VERTS; i++)
		CHECK(sseen[i] > 0);
}

static void seda_sparkles_ok(void)
{
	static seda_vertex s[SEDA_SPARK_VERTS], s2[SEDA_SPARK_VERTS];
	int lit = 0;
	for (float tf = 0.0f; tf < 1.0f; tf += 0.093f) {
		seda_sparkles(16.0f / 9.0f, tf, s);
		for (unsigned i = 0; i < SEDA_SPARKS; i++) {
			const seda_vertex *q = &s[i * 4];
			/* El cuadrito es un rectángulo con las esquinas en t0 = ±1 y centrado. */
			CHECK(q[0].t0[0] == -1.0f && q[1].t0[0] == 1.0f && q[2].t0[1] == 1.0f && q[3].t0[0] == -1.0f);
			CHECK(q[0].t0[2] >= 0.0f && q[0].t0[2] <= 1.0001f);
			CHECK(q[0].t0[2] == q[2].t0[2] && q[1].t0[3] == q[3].t0[3]);
			CHECK(q[0].pos[3] == 1.0f);
			CHECK(fabsf(q[0].pos[1]) < 1.5f && fabsf(q[0].pos[0]) < 1.5f);
			CHECK(q[1].pos[0] > q[0].pos[0] && q[2].pos[1] > q[0].pos[1]);
			if (q[0].t0[2] > 0.3f)
				lit++;
		}
	}
	CHECK(lit > 20);
	seda_sparkles(16.0f / 9.0f, 0.4f, s);
	seda_sparkles(16.0f / 9.0f, 0.4f, s2);
	CHECK(!memcmp(s, s2, sizeof s));
}

static void seda_palette(void)
{
	struct seda_colors c, d;
	for (int h = 0; h <= 48; h++) {
		seda_colors(h * 1800.0, &c);
		for (int k = 0; k < 3; k++) {
			CHECK(c.top[k] >= 0.0f && c.top[k] <= 1.0f && c.bottom[k] >= 0.0f && c.bottom[k] <= 1.0f);
			CHECK(c.tint_a[k] >= 0.0f && c.tint_a[k] <= 1.0f && c.tint_b[k] >= 0.0f && c.tint_b[k] <= 1.0f);
			CHECK(c.glow[k] >= 0.0f && c.glow[k] <= 1.0f);
		}
		CHECK(c.stars >= 0.0f && c.stars <= 1.0f);
	}
	/* Medianoche, igual a los dos lados del día; el día y la noche difieren. */
	seda_colors(0.0, &c);
	seda_colors(86399.999, &d);
	for (int k = 0; k < 3; k++)
		CHECK(fabsf(c.top[k] - d.top[k]) < 1e-3f && fabsf(c.bottom[k] - d.bottom[k]) < 1e-3f);
	seda_colors(-3600.0, &d);  /* las 23:00 */
	seda_colors(23 * 3600.0, &c);
	CHECK(fabsf(c.top[2] - d.top[2]) < 1e-6f);
	seda_colors(13 * 3600.0, &c);
	seda_colors(1 * 3600.0, &d);
	CHECK(c.top[2] > d.top[2] + 0.2f);   /* el cielo de mediodía es más azul y claro */
	CHECK(d.stars > c.stars + 0.5f);     /* y los destellos, de noche */
	/* Sin saltos entre un minuto y el siguiente. */
	for (int s = 0; s < 86400; s += 60) {
		seda_colors(s, &c);
		seda_colors(s + 60, &d);
		for (int k = 0; k < 3; k++)
			CHECK(fabsf(c.bottom[k] - d.bottom[k]) < 0.01f);
	}
	for (int l = 0; l < SEDA_LAYERS; l++)
		CHECK(seda_layer_gain(l) > 0.0f && seda_layer_gain(l) <= 1.5f);
	CHECK(seda_layer_gain(0) > seda_layer_gain(1) && seda_layer_gain(1) > seda_layer_gain(2));
}

void t_suite_seda(void)
{
	RUN(seda_positions_match_reference);
	RUN(seda_rows_equal_layer);
	RUN(seda_normals_and_projection);
	RUN(seda_ribbon_is_continuous);
	RUN(seda_period_wraps_seamlessly);
	RUN(seda_moves);
	RUN(seda_index_buffers);
	RUN(seda_sparkles_ok);
	RUN(seda_palette);
}
