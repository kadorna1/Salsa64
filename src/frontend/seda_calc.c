/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — fondo «Seda» del menú: la geometría y los colores de la cinta.
 * Ver seda_calc.h. Todo en `float` y sin libm más que sqrtf, para que el
 * mismo código corra en el PPE, en el SPE y en la Mac. El seno es propio
 * (reducción a [−π/2, π/2] y un polinomio de grado 9, error < 4e-6).
 */
#include "frontend/seda_calc.h"

#include <math.h>

#define PI_F  3.14159265359f
#define TAU_F 6.28318530718f
#define FOCAL 1.7f  /* distancia focal de la cámara (la y visible a z = 3 es ±1,76) */
/* Encuadre de la cinta: más lejos (ZOOM), estirada a lo largo (LARGO) para
 * que siga cruzando la pantalla, y ALTURA en unidades de la y de recorte. */
#define ZOOM   0.503f
#define LARGO  2.0f
#define ALTURA 0.032f

/* ---- Aritmética -------------------------------------------------------------------- */

static float sn(float x)
{
	float y = x * 0.15915494309f;
	int k = (int)(y + 8192.5f) - 8192;  /* ciclos enteros, al más cercano */
	x -= (float)k * TAU_F;
	if (x > 1.57079632679f)
		x = PI_F - x;
	else if (x < -1.57079632679f)
		x = -PI_F - x;
	float x2 = x * x;
	return x * (1.0f + x2 * (-0.16666667f + x2 * (0.0083333333f + x2 * (-1.98412698e-4f + x2 * 2.75573192e-6f))));
}

static float cs(float x)
{
	return sn(x + 1.57079632679f);
}

static float clamp01(float x)
{
	return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static float smooth01(float x)
{
	x = clamp01(x);
	return x * x * (3.0f - 2.0f * x);
}

static void cross3(const float *a, const float *b, float *o)
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Normaliza en el lugar; con longitud casi 0 deja `fallback`. */
static void norm3(float *v, float fx, float fy, float fz)
{
	float l2 = dot3(v, v);
	if (l2 < 1e-20f) {
		v[0] = fx, v[1] = fy, v[2] = fz;
		return;
	}
	float r = 1.0f / sqrtf(l2);
	v[0] *= r, v[1] *= r, v[2] *= r;
}

float seda_phase(double t)
{
	double p = t / SEDA_PERIOD;
	p -= (double)(int64_t)p;
	if (p < 0.0)
		p += 1.0;
	return (float)p;
}

/* Fase de una onda de `m` ciclos por periodo. */
static float ph(int m, float tf)
{
	return TAU_F * ((float)m * tf);
}

/* ---- Capas -------------------------------------------------------------------------- */

struct layer_params {
	float zc, yc;   /* profundidad y altura medias del eje */
	float lx;       /* mitad de la longitud en x por la mitad de la altura visible */
	float hw;       /* semiancho de la tela */
	float twk;      /* torsión total a lo largo (rad por unidad de s) */
	float ph0;      /* desfase propio */
	int dm;         /* ciclos extra por periodo (varía la velocidad) */
	float gain;     /* ganancia de color */
};

static const struct layer_params LAYERS[SEDA_LAYERS] = {
	{ 3.54f, -0.231f, 2.55f, 0.323f, 3.6f, 6.283f, 0, 1.162f },
	{ 3.55f, -0.38f, 3.00f, 0.472f, 4.4f, 2.1f, 9, 0.757f },
	{ 4.20f, 0.46f, 3.55f, 0.74f, 2.6f, 4.0f, -6, 0.482f },
};

float seda_layer_gain(int layer)
{
	return LAYERS[layer].gain;
}

/* El eje de la cinta en s. */
static void axis(const struct layer_params *L, float aspect, float s, float tf, float *c)
{
	c[0] = L->lx * aspect * s;
	c[1] = L->yc + 0.60f * sn(3.1f * s + ph(70 + L->dm, tf) + L->ph0) +
	       0.24f * sn(6.7f * s - ph(95 + L->dm, tf) + 1.3f * L->ph0);
	c[2] = L->zc + 0.55f * sn(2.5f * s + ph(55 + L->dm, tf) + 0.7f * L->ph0) +
	       0.20f * sn(5.6f * s - ph(80 + L->dm, tf));
}

/* Los pliegues: cuatro ondas que corren a lo largo (distintos signos y
 * velocidades) y se cruzan en diagonal por el ancho. */
static float pleats(const struct layer_params *L, float s, float v, float tf)
{
	float h = 0.075f * sn(5.0f * s + 2.0f * v + ph(85 + L->dm, tf) + L->ph0) +
		  0.050f * sn(9.0f * s - 3.5f * v - ph(60 + L->dm, tf) + 1.7f * L->ph0) +
		  0.034f * sn(14.0f * s + 5.0f * v + ph(110 + L->dm, tf) + 2.9f * L->ph0) +
		  0.020f * sn(21.0f * s - 7.0f * v - ph(140 + L->dm, tf) + 0.4f);
	return h * (0.45f + 0.55f * v * v);  /* la orilla ondula más que el centro */
}

void seda_positions(int layer, float aspect, float tf, float pos[SEDA_LAYER_VERTS][4])
{
	const struct layer_params *L = &LAYERS[layer];
	for (int i = 0; i < SEDA_NU; i++) {
		float s = -1.0f + 2.0f * (float)i / (float)(SEDA_NU - 1);
		float c[3], cp[3], cm[3], t[3];
		axis(L, aspect, s, tf, c);
		axis(L, aspect, s + 0.004f, tf, cp);
		axis(L, aspect, s - 0.004f, tf, cm);
		t[0] = cp[0] - cm[0], t[1] = cp[1] - cm[1], t[2] = cp[2] - cm[2];
		norm3(t, 1.0f, 0.0f, 0.0f);
		/* Marco: B = t × arriba, N2 = B × t; el ancho gira en φ entre ellos. */
		static const float up[3] = { 0.0f, 1.0f, 0.0f };
		float b[3], n2[3], d[3], m[3];
		cross3(t, up, b);
		norm3(b, 0.0f, 0.0f, 1.0f);
		cross3(b, t, n2);
		float phi = L->twk * s + 1.2f * sn(2.2f * s + ph(40 + L->dm, tf) + L->ph0) + ph(20 + L->dm, tf);
		float cp_ = cs(phi), sp_ = sn(phi);
		d[0] = cp_ * n2[0] + sp_ * b[0];
		d[1] = cp_ * n2[1] + sp_ * b[1];
		d[2] = cp_ * n2[2] + sp_ * b[2];
		cross3(t, d, m);
		float hw = L->hw * (1.0f + 0.28f * sn(2.7f * s + ph(35 + L->dm, tf) + L->ph0));
		for (int j = 0; j < SEDA_NV; j++) {
			float v = -1.0f + 2.0f * (float)j / (float)(SEDA_NV - 1);
			float h = pleats(L, s, v, tf), w = v * hw;
			float *p = pos[i * SEDA_NV + j];
			p[0] = c[0] + w * d[0] + h * m[0];
			p[1] = c[1] + w * d[1] + h * m[1];
			p[2] = c[2] + w * d[2] + h * m[2];
			p[3] = 0.0f;
		}
	}
}

void seda_row(int layer, float aspect, const float pos[SEDA_LAYER_VERTS][4], int i, seda_vertex *out)
{
	/* Luz fija (hacia arriba, a la izquierda y delante de la tela). */
	static const float light[3] = { -0.48f, 0.62f, -0.62f };
	int ip = i < SEDA_NU - 1 ? i + 1 : i, im = i > 0 ? i - 1 : i;
	float s = -1.0f + 2.0f * (float)i / (float)(SEDA_NU - 1);
	for (int j = 0; j < SEDA_NV; j++) {
		int jp = j < SEDA_NV - 1 ? j + 1 : j, jm = j > 0 ? j - 1 : j;
		const float *p = pos[i * SEDA_NV + j];
		const float *a = pos[ip * SEDA_NV + j], *b = pos[im * SEDA_NV + j];
		const float *c = pos[i * SEDA_NV + jp], *d = pos[i * SEDA_NV + jm];
		float du[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
		float dv[3] = { c[0] - d[0], c[1] - d[1], c[2] - d[2] };
		float n[3], view[3], hv[3];
		cross3(du, dv, n);
		norm3(n, 0.0f, 0.0f, -1.0f);
		view[0] = -p[0], view[1] = -p[1], view[2] = -p[2];
		norm3(view, 0.0f, 0.0f, -1.0f);
		float f = dot3(n, view);
		if (f < 0.0f) {  /* tela de dos caras: la normal hacia la cámara */
			n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
			f = -f;
		}
		hv[0] = light[0] + view[0], hv[1] = light[1] + view[1], hv[2] = light[2] + view[2];
		norm3(hv, 0.0f, 0.0f, -1.0f);
		float sp = dot3(n, hv);
		float v = -1.0f + 2.0f * (float)j / (float)(SEDA_NV - 1);
		float e2 = v * v;
		seda_vertex *o = &out[j];
		o->pos[0] = p[0] * (FOCAL * ZOOM * LARGO / aspect);
		o->pos[1] = p[1] * (FOCAL * ZOOM) + ALTURA * p[2];
		o->pos[2] = 0.0f;
		o->pos[3] = p[2];
		o->t0[0] = f;
		o->t0[1] = sp;
		o->t0[2] = 1.0f - 0.35f * e2 * e2;
		o->t0[3] = clamp01(0.5f + 0.5f * sn(1.9f * s + 1.1f * v + 2.0f * (float)layer));
	}
}

void seda_layer(int layer, float aspect, float tf, float scratch[SEDA_LAYER_VERTS][4], seda_vertex *out)
{
	seda_positions(layer, aspect, tf, scratch);
	for (int i = 0; i < SEDA_NU; i++)
		seda_row(layer, aspect, (const float(*)[4])scratch, i, out + i * SEDA_NV);
}

/* ---- Destellos ------------------------------------------------------------------------ */

static uint32_t hash32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static float rnd(uint32_t i, uint32_t k)
{
	return (float)(hash32(i * 8u + k + 0x9e3779b9u) >> 8) * (1.0f / 16777216.0f);
}

void seda_sparkles(float aspect, float tf, seda_vertex *out)
{
	static const float cx[4] = { -1.0f, 1.0f, 1.0f, -1.0f }, cy[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
	for (uint32_t i = 0; i < SEDA_SPARKS; i++) {
		float r0 = rnd(i, 0), r1 = rnd(i, 1), r2 = rnd(i, 2), r3 = rnd(i, 3), r4 = rnd(i, 4);
		int rise = 3 + (int)(r2 * 5.0f);          /* vueltas a la pantalla por periodo */
		int twinkle = 100 + (int)(r3 * 200.0f);   /* parpadeos por periodo */
		float y01 = r1 + (float)rise * tf;
		y01 -= (float)(int)y01;
		/* Hacia el centro: en x, la suma de dos azares (más en medio que en
		 * las orillas); en y, el brillo se apaga lejos de la mitad. */
		float x = (r0 + rnd(i, 5) - 1.0f) * 0.85f + 0.03f * sn(ph(7 + (int)(r4 * 9.0f), tf) + r3 * TAU_F);
		float y = 2.0f * y01 - 1.0f;
		float tw = 0.5f + 0.5f * sn(ph(twinkle, tf) + r4 * TAU_F);
		float sy = sn(PI_F * y01);
		float b = tw * tw * sy * sy;
		float sz = 0.006f + 0.014f * r4;
		for (int k = 0; k < 4; k++) {
			seda_vertex *o = &out[i * 4 + k];
			o->pos[0] = x + cx[k] * sz / aspect;
			o->pos[1] = y + cy[k] * sz;
			o->pos[2] = 0.0f;
			o->pos[3] = 1.0f;
			o->t0[0] = cx[k];
			o->t0[1] = cy[k];
			o->t0[2] = b;
			o->t0[3] = r2;
		}
	}
}

void seda_indices(uint16_t *idx)
{
	uint32_t n = 0;
	for (int i = 0; i + 1 < SEDA_NU; i++)
		for (int j = 0; j + 1 < SEDA_NV; j++) {
			uint16_t a = (uint16_t)(i * SEDA_NV + j), b = (uint16_t)(a + SEDA_NV);
			idx[n++] = a, idx[n++] = b, idx[n++] = (uint16_t)(a + 1);
			idx[n++] = (uint16_t)(a + 1), idx[n++] = b, idx[n++] = (uint16_t)(b + 1);
		}
}

void seda_spark_indices(uint16_t *idx)
{
	for (uint32_t i = 0; i < SEDA_SPARKS; i++) {
		uint16_t a = (uint16_t)(i * 4);
		idx[i * 6 + 0] = a, idx[i * 6 + 1] = (uint16_t)(a + 1), idx[i * 6 + 2] = (uint16_t)(a + 2);
		idx[i * 6 + 3] = a, idx[i * 6 + 4] = (uint16_t)(a + 2), idx[i * 6 + 5] = (uint16_t)(a + 3);
	}
}

/* ---- Colores -------------------------------------------------------------------------- */

struct key {
	float hour;
	float top[3], bottom[3], tint_a[3], tint_b[3], glow[3];
	float stars;
};

/* Paleta propia: noche índigo, amanecer rosa, día turquesa, tarde ámbar. */
static const struct key KEYS[] = {
	{ 0.0f, { 0.015f, 0.020f, 0.070f }, { 0.070f, 0.040f, 0.170f }, { 0.45f, 0.60f, 1.00f }, { 0.85f, 0.50f, 1.00f },
	  { 0.20f, 0.12f, 0.45f }, 1.00f },
	{ 5.5f, { 0.050f, 0.080f, 0.240f }, { 0.550f, 0.200f, 0.320f }, { 1.00f, 0.62f, 0.55f }, { 0.60f, 0.55f, 1.00f },
	  { 0.55f, 0.25f, 0.35f }, 0.50f },
	{ 8.0f, { 0.040f, 0.230f, 0.500f }, { 0.300f, 0.560f, 0.720f }, { 0.80f, 0.86f, 0.92f }, { 0.45f, 0.75f, 0.92f },
	  { 0.18f, 0.32f, 0.46f }, 0.10f },
	{ 13.0f, { 0.020f, 0.200f, 0.480f }, { 0.100f, 0.440f, 0.680f }, { 0.78f, 0.84f, 0.88f }, { 0.40f, 0.70f, 0.90f },
	  { 0.12f, 0.30f, 0.45f }, 0.05f },
	{ 17.5f, { 0.100f, 0.200f, 0.500f }, { 0.850f, 0.400f, 0.220f }, { 1.00f, 0.78f, 0.50f }, { 1.00f, 0.45f, 0.70f },
	  { 0.70f, 0.35f, 0.20f }, 0.30f },
	{ 20.0f, { 0.120f, 0.060f, 0.300f }, { 0.500f, 0.140f, 0.360f }, { 1.00f, 0.60f, 0.75f }, { 0.55f, 0.45f, 1.00f },
	  { 0.40f, 0.15f, 0.40f }, 0.70f },
	{ 22.5f, { 0.030f, 0.040f, 0.140f }, { 0.090f, 0.050f, 0.220f }, { 0.50f, 0.58f, 1.00f }, { 0.80f, 0.50f, 1.00f },
	  { 0.22f, 0.12f, 0.46f }, 0.95f },
	{ 24.0f, { 0.015f, 0.020f, 0.070f }, { 0.070f, 0.040f, 0.170f }, { 0.45f, 0.60f, 1.00f }, { 0.85f, 0.50f, 1.00f },
	  { 0.20f, 0.12f, 0.45f }, 1.00f },
};

static void mix3(float *o, const float *a, const float *b, float t)
{
	for (int i = 0; i < 3; i++)
		o[i] = a[i] + (b[i] - a[i]) * t;
}

void seda_colors(double day_seconds, struct seda_colors *c)
{
	double h = day_seconds / 3600.0;
	h -= 24.0 * (double)(int64_t)(h / 24.0);
	if (h < 0.0)
		h += 24.0;
	unsigned n = sizeof KEYS / sizeof KEYS[0], k = 0;
	while (k + 2 < n && h >= (double)KEYS[k + 1].hour)
		k++;
	const struct key *a = &KEYS[k], *b = &KEYS[k + 1];
	float t = smooth01((float)((h - (double)a->hour) / (double)(b->hour - a->hour)));
	mix3(c->top, a->top, b->top, t);
	mix3(c->bottom, a->bottom, b->bottom, t);
	mix3(c->tint_a, a->tint_a, b->tint_a, t);
	mix3(c->tint_b, a->tint_b, b->tint_b, t);
	mix3(c->glow, a->glow, b->glow, t);
	c->stars = a->stars + (b->stars - a->stars) * t;
}
