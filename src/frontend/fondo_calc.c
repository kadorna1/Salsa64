/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — fondo «Shader» del menú, la parte de la CPU. Ver fondo_calc.h.
 *
 * Basado en el shader «XdGfRR» de Shadertoy, de int_45h
 * (https://www.shadertoy.com/view/XdGfRR). Ver docs/THIRD_PARTY.md.
 */
#include "frontend/fondo_calc.h"

#include <math.h>
#include <string.h>

/* Capas de estrellas del original: escala de la red y velocidad. */
static const float layer_scale[FONDO_LAYERS] = { 0.1f, 0.2f, 0.32f };
static const float layer_vx[FONDO_LAYERS] = { 20.0f, 30.0f, 40.0f };
#define LAYER_VY (-10.1)

static double fmod_pos(double a, double m)
{
	double r = fmod(a, m);
	return r < 0 ? r + m : r;
}

/* ---- fp16 ------------------------------------------------------------------ */

uint16_t fondo_half(float f)
{
	union {
		float f;
		uint32_t u;
	} v = { f };
	uint32_t s = (v.u >> 16) & 0x8000u, e = (v.u >> 23) & 0xFFu, m = v.u & 0x7FFFFFu;
	if (e == 0xFF)
		return (uint16_t)(s | 0x7C00u | (m ? 0x200u : 0));
	int ex = (int)e - 127 + 15;
	if (ex >= 31)
		return (uint16_t)(s | 0x7C00u);
	if (ex <= 0) {
		if (ex < -10)
			return (uint16_t)s;
		m |= 0x800000u;
		uint32_t shift = (uint32_t)(14 - ex);
		uint32_t hm = m >> shift, rem = m & ((1u << shift) - 1), half = 1u << (shift - 1);
		if (rem > half || (rem == half && (hm & 1)))
			hm++;
		return (uint16_t)(s | hm);
	}
	uint32_t hm = m >> 13, rem = m & 0x1FFFu;
	uint32_t h = s | ((uint32_t)ex << 10) | hm;
	if (rem > 0x1000u || (rem == 0x1000u && (hm & 1)))
		h++;  /* el acarreo pasa al exponente, como debe */
	return (uint16_t)h;
}

float fondo_unhalf(uint16_t h)
{
	uint32_t s = (uint32_t)(h & 0x8000u) << 16, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
	union {
		uint32_t u;
		float f;
	} v;
	if (e == 0) {
		if (!m) {
			v.u = s;
			return v.f;
		}
		float r = (float)m / 16777216.0f;  /* m · 2^-24 */
		return s ? -r : r;
	}
	if (e == 31)
		v.u = s | 0x7F800000u | (m << 13);
	else
		v.u = s | ((e - 15 + 127) << 23) | (m << 13);
	return v.f;
}

/* ---- La superficie -------------------------------------------------------------- */

/* value2d del original en doble precisión. */
static double value2d(double px, double py)
{
	double gx = floor(px), gy = floor(py), cx = px - gx, cy = py - gy;
	cx *= cx * (3.0 - 2.0 * cx);
	cy *= cy * (3.0 - 2.0 * cy);
	int32_t ix = (int32_t)gx, iy = (int32_t)gy;
	double a = fondo_hash12i(ix, iy), b = fondo_hash12i(ix + 1, iy);
	double c = fondo_hash12i(ix, iy + 1), d = fondo_hash12i(ix + 1, iy + 1);
	double m0 = a + (b - a) * cx, m1 = c + (d - c) * cx;
	return m0 + (m1 - m0) * cy;
}

double fondo_h(double x, double z, double T)
{
	double o = 8.2 * sin(0.1 * x + 0.25 * T) +
		   0.16 * z * sin(0.22 * x + T) * sin(0.4 * z + T) * value2d(0.06 * x + 0.5 * T, 0.8 * z);
	return FONDO_B * z + 2.5 + 0.5 * o;
}

/* h en n puntos de una recta (x0 + k·sx, z0 + k·sz): los senos con giros
 * en lugar de llamar a sin en cada punto. */
static void h_line(double *out, int n, double x0, double sx, double z0, double sz, double T)
{
	double a1 = 0.1 * x0 + 0.25 * T, d1 = 0.1 * sx;
	double a2 = 0.22 * x0 + T, d2 = 0.22 * sx;
	double a3 = 0.4 * z0 + T, d3 = 0.4 * sz;
	double s1 = sin(a1), c1 = cos(a1), sd1 = sin(d1), cd1 = cos(d1);
	double s2 = sin(a2), c2 = cos(a2), sd2 = sin(d2), cd2 = cos(d2);
	double s3 = sin(a3), c3 = cos(a3), sd3 = sin(d3), cd3 = cos(d3);
	/* Las 4 esquinas del value2d se repiten mientras no cambie la celda. */
	int32_t cx = INT32_MIN, cy = INT32_MIN;
	double ha = 0, hb = 0, hc = 0, hd = 0;
	for (int k = 0; k < n; k++) {
		double x = x0 + k * sx, z = z0 + k * sz;
		double vx = 0.06 * x + 0.5 * T, vy = 0.8 * z, gx = floor(vx), gy = floor(vy);
		int32_t ix = (int32_t)gx, iy = (int32_t)gy;
		if (ix != cx || iy != cy) {
			cx = ix, cy = iy;
			ha = fondo_hash12i(ix, iy), hb = fondo_hash12i(ix + 1, iy);
			hc = fondo_hash12i(ix, iy + 1), hd = fondo_hash12i(ix + 1, iy + 1);
		}
		double fx = vx - gx, fy = vy - gy;
		fx *= fx * (3.0 - 2.0 * fx);
		fy *= fy * (3.0 - 2.0 * fy);
		double m0 = ha + (hb - ha) * fx, m1 = hc + (hd - hc) * fx;
		double o = 8.2 * s1 + 0.16 * z * s2 * s3 * (m0 + (m1 - m0) * fy);
		out[k] = FONDO_B * z + 2.5 + 0.5 * o;
		double t;
		t = s1 * cd1 + c1 * sd1, c1 = c1 * cd1 - s1 * sd1, s1 = t;
		t = s2 * cd2 + c2 * sd2, c2 = c2 * cd2 - s2 * sd2, s2 = t;
		t = s3 * cd3 + c3 * sd3, c3 = c3 * cd3 - s3 * sd3, s3 = t;
	}
}

float fondo_prim(float t)
{
	float x = 0.03f * t;
	x = x < 0 ? 0 : x > 1 ? 1 : x;
	float x3 = x * x * x;
	float F = (x3 - 0.5f * x3 * x) * (1.0f / 0.03f) + (t > 1.0f / 0.03f ? t - 1.0f / 0.03f : 0.0f);
	return 0.3f * F - FONDO_C0;
}

/* ---- Malla ---------------------------------------------------------------------- */

void fondo_indices(uint16_t *idx)
{
	const uint32_t row = FONDO_NU + 1;
	for (uint32_t j = 0; j < FONDO_NZ; j++)
		for (uint32_t i = 0; i < FONDO_NU; i++) {
			uint16_t a = (uint16_t)(j * row + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + row),
				 d = (uint16_t)(c + 1);
			*idx++ = a, *idx++ = b, *idx++ = c;
			*idx++ = b, *idx++ = d, *idx++ = c;
		}
	/* Tapa: pares (+0,04, −0,04) por columna. */
	for (uint32_t i = 0; i < FONDO_NU; i++) {
		uint16_t a = (uint16_t)(2 * i);
		*idx++ = a, *idx++ = (uint16_t)(a + 2), *idx++ = (uint16_t)(a + 1);
		*idx++ = (uint16_t)(a + 2), *idx++ = (uint16_t)(a + 3), *idx++ = (uint16_t)(a + 1);
	}
}

static double mesh_z(uint32_t j)
{
	return FONDO_Z0 + (FONDO_MAX_DIST - FONDO_Z0) * (double)j / FONDO_NZ;
}

/* Vértice de la superficie g = c en la columna u (= dx) y la profundidad z.
 * El píxel (px, py) del RSX es el rayo de Ue = (px + 0,25, H − py − 0,75)
 * de Shadertoy (el centro de las 4 muestras del AA del original). */
static void put_vertex(fondo_vertex *v, const struct fondo_view *vw, double u, double z, double h, double c,
		       float t)
{
	double dy = (c - h) / (FONDO_A * z) ;
	double W = vw->w, H = vw->h;
	double xw = u * H + 0.5 * W + 0.25, yw = 0.5 * H - dy * H - 0.25;
	double X = 2.0 * xw / W - 1.0, Y = 1.0 - 2.0 * yw / H;
	v->pos[0] = (float)(X * z);
	v->pos[1] = (float)(Y * z);
	v->pos[2] = 0.0f;
	v->pos[3] = (float)z;
	v->t0[0] = t;
	v->t0[1] = v->t0[2] = v->t0[3] = 0.0f;
}

void fondo_mesh(fondo_vertex *v, const struct fondo_view *vw, double T)
{
	const uint32_t row = FONDO_NU + 1;
	/* La malla pasa unos píxeles más allá de los bordes: la orilla de la
	 * red queda fuera de la pantalla. */
	double umax = (0.5 * vw->w + 4.0) / vw->h, du = 2.0 * umax / FONDO_NU;
	double h[FONDO_NU + 1];
	for (uint32_t j = 0; j <= FONDO_NZ; j++) {
		double z = mesh_z(j);
		h_line(h, (int)row, -umax * z, du * z, z, 0.0, T);
		for (uint32_t i = 0; i < row; i++) {
			double u = -umax + du * i;
			fondo_vertex *p = &v[FONDO_VERT_SURF_P + j * row + i], *n = &v[FONDO_VERT_SURF_N + j * row + i];
			put_vertex(p, vw, u, z, h[i], FONDO_SHELL, (float)z);
			put_vertex(n, vw, u, z, h[i], -FONDO_SHELL, (float)z);
			if (j == FONDO_NZ || j == 0) {
				fondo_vertex *cap = &v[j ? FONDO_VERT_CAP_FAR : FONDO_VERT_CAP_NEAR];
				cap[2 * i] = *p;
				cap[2 * i + 1] = *n;
			}
		}
	}
}

/* ---- Tabla de h ------------------------------------------------------------------ */

static double polvo_t(int k)
{
	return 10.0 + 30.0 * k / (FONDO_TS - 1);
}

double fondo_low_dx(const struct fondo_view *vw, uint32_t i)
{
	double ue = (i + 0.5) * vw->w / vw->lw - 0.25;
	return (ue - 0.5 * vw->w) / vw->h;
}

double fondo_low_dy(const struct fondo_view *vw, uint32_t j)
{
	double ue = vw->h - 0.25 - (j + 0.5) * vw->h / vw->lh;
	return (ue - 0.5 * vw->h) / vw->h;
}

void fondo_htable(uint16_t *tab, uint32_t pitch, const struct fondo_view *vw, double T)
{
	double h[FONDO_NT];
	const double dt = FONDO_MAX_DIST / (FONDO_NT - 1);
	for (uint32_t i = 0; i < vw->lw; i++) {
		double dx = fondo_low_dx(vw, i);
		h_line(h, FONDO_NT, 0.0, dx * dt, 0.0, dt, T);
		uint16_t *row = (uint16_t *)((uint8_t *)tab + (size_t)(i / 4) * pitch) + i % 4;
		for (int j = 0; j < FONDO_NT; j++)
			row[j * 4] = fondo_half((float)(h[j] - 2.5 - FONDO_B * dt * j));
	}
}

/* ---- Anillos del hash ------------------------------------------------------------- */

/* Valor de un texel de la capa `layer` en el punto (i, j) de su red. */
typedef uint8_t (*texel_fn)(int64_t i, int64_t j);

static uint8_t to_byte(float v)
{
	v = v < 0 ? 0 : v > 1 ? 1 : v;
	return (uint8_t)(v * 255.0f + 0.5f);
}

static uint8_t star_texel(int64_t i, int64_t j)
{
	return to_byte(fondo_star_rough_h(fondo_hash12i((int32_t)i, (int32_t)j)));
}

static uint8_t bri_texel(int64_t i, int64_t j)
{
	double v = value2d((double)i / FONDO_BRI_SS, (double)j / FONDO_BRI_SS);
	return to_byte((float)pow(v * 0.5 + 0.5, 8.3));
}

static uint32_t fill(uint8_t *tex, uint32_t tw, uint32_t th, int ch, int64_t x0, int64_t x1, int64_t y0, int64_t y1,
		     texel_fn fn)
{
	uint32_t n = 0;
	for (int64_t j = y0; j < y1; j++) {
		uint8_t *row = tex + (size_t)((uint64_t)j & (th - 1)) * tw * 4 + ch;
		for (int64_t i = x0; i < x1; i++, n++)
			row[((uint64_t)i & (tw - 1)) * 4] = fn(i, j);
	}
	return n;
}

static uint32_t ring_update(struct fondo_ring *r, uint8_t *tex, uint32_t tw, uint32_t th, int ch, int64_t x0,
			    int64_t x1, int64_t y0, int64_t y1, texel_fn fn)
{
	uint32_t n = 0;
	if (r->valid && x0 >= r->x0 && x1 <= r->x1 && y0 >= r->y0 && y1 <= r->y1)
		return 0;
	/* Lo que sigue valiendo es la intersección; con un salto grande (o al
	 * empezar), nada. */
	int64_t ix0 = x0 > r->x0 ? x0 : r->x0, ix1 = x1 < r->x1 ? x1 : r->x1;
	int64_t iy0 = y0 > r->y0 ? y0 : r->y0, iy1 = y1 < r->y1 ? y1 : r->y1;
	if (!r->valid || ix0 >= ix1 || iy0 >= iy1) {
		n = fill(tex, tw, th, ch, x0, x1, y0, y1, fn);
	} else {
		/* Columnas nuevas en todas las filas; filas nuevas en el resto. */
		n += fill(tex, tw, th, ch, x0, ix0, y0, y1, fn);
		n += fill(tex, tw, th, ch, ix1, x1, y0, y1, fn);
		n += fill(tex, tw, th, ch, ix0, ix1, y0, iy0, fn);
		n += fill(tex, tw, th, ch, ix0, ix1, iy1, y1, fn);
	}
	r->valid = true;
	r->x0 = x0, r->x1 = x1, r->y0 = y0, r->y1 = y1;
	return n;
}

void fondo_tex_reset(struct fondo_tex *t)
{
	memset(t->ring, 0, sizeof t->ring);
}

void fondo_tex_update(struct fondo_tex *t, const struct fondo_view *vw, double T)
{
	double W = vw->w, H = vw->h;
	t->filled = 0;
	for (int k = 0; k < FONDO_LAYERS; k++) {
		double S = layer_scale[k] * 2000.0 / H, ox = T * layer_vx[k], oy = T * LAYER_VY;
		/* Ue en [−1, W + 1] × [−1, H + 1], con margen para el AA y la
		 * interpolación. */
		int64_t x0 = (int64_t)floor(-S + ox) - 1, x1 = (int64_t)floor((W + 1) * S + ox) + 3;
		int64_t y0 = (int64_t)floor(-S + oy) - 1, y1 = (int64_t)floor((H + 1) * S + oy) + 3;
		t->filled += ring_update(&t->ring[k], t->star, FONDO_STAR_W, FONDO_STAR_H, k, x0, x1, y0, y1, star_texel);
		/* El brillo: value2d(p·0,1 + iTime) en FONDO_BRI_SS muestras por celda. */
		double n = FONDO_BRI_SS, s = 0.1 * S, bx = 0.1 * ox + T, by = 0.1 * oy + T;
		x0 = (int64_t)floor((-s + bx) * n) - 1, x1 = (int64_t)floor(((W + 1) * s + bx) * n) + 3;
		y0 = (int64_t)floor((-s + by) * n) - 1, y1 = (int64_t)floor(((H + 1) * s + by) * n) + 3;
		t->filled += ring_update(&t->ring[FONDO_LAYERS + k], t->bri, FONDO_BRI_W, FONDO_BRI_H, k, x0, x1, y0,
					 y1, bri_texel);
	}
}

/* ---- Constantes ------------------------------------------------------------------- */

/* Un valor afín en Ue, f = f0 + fx·Ue.x + fy·Ue.y, como constante afín en
 * el píxel: Ue = (px + 0,25, H − py − 0,75) = (X − 0,25, H − Y − 0,25) con
 * (X, Y) el centro del píxel. */
static void affine(struct fondo_frame *fr, int j, int c, double H, double f0, double fx, double fy)
{
	fr->k[j][c] = (float)(f0 - 0.25 * fx + fy * (H - 0.25));
	fr->kx[j][c] = (float)fx;
	fr->ky[j][c] = (float)-fy;
}

void fondo_frame_consts(struct fondo_frame *fr, const struct fondo_view *vw, double T, double day_s)
{
	double W = vw->w, H = vw->h;
	memset(fr, 0, sizeof *fr);
	/* Capas: red p = Ue·S + O (O reducido a su anillo); la capa 3 ya como
	 * coordenada de textura ((p + 0,5)/tamaño) y el brillo como coordenada
	 * de su textura ((q·SS + 0,5)/tamaño, q = p·0,1 + iTime). */
	for (int k = 0; k < FONDO_LAYERS; k++) {
		double S = layer_scale[k] * 2000.0 / H, ox = T * layer_vx[k], oy = T * LAYER_VY;
		double px = fmod_pos(ox, FONDO_STAR_W), py = fmod_pos(oy, FONDO_STAR_H);
		double n = FONDO_BRI_SS, b = 0.1 * S * n / FONDO_BRI_W;
		double bx = (fmod_pos((0.1 * ox + T) * n, FONDO_BRI_W) + 0.5) / FONDO_BRI_W;
		double by = (fmod_pos((0.1 * oy + T) * n, FONDO_BRI_H) + 0.5) / FONDO_BRI_H;
		if (k < 2) {
			affine(fr, 0, 2 * k, H, px, S, 0);
			affine(fr, 0, 2 * k + 1, H, py, 0, S);
			affine(fr, 2, 2 * k, H, bx, b, 0);
			affine(fr, 2, 2 * k + 1, H, by, 0, b);
		} else {
			affine(fr, 1, 0, H, (px + 0.5) / FONDO_STAR_W, S / FONDO_STAR_W, 0);
			affine(fr, 1, 1, H, (py + 0.5) / FONDO_STAR_H, 0, S / FONDO_STAR_H);
			affine(fr, 1, 2, H, bx, b, 0);
			affine(fr, 1, 3, H, by, 0, b);
		}
	}
	/* Degradado: c = K0 + K1·x + K2·y + K3·x·y en Ue (x = Ue.x/W, y =
	 * Ue.y/H); TEXCOORD4 lleva lo lineal, TEXCOORD5 el K3·x y la y. El
	 * tramado: dot(U, (0,0671…, 0,0058…)) con U = Ue + 0,25. */
	float l[4][3];
	fondo_corners((float)day_s, l);
	for (int c = 0; c < 3; c++) {
		affine(fr, 3, c, H, l[0][c], (l[1][c] - l[0][c]) / W, (l[2][c] - l[0][c]) / H);
		affine(fr, 4, c, H, 0, (l[0][c] - l[1][c] - l[2][c] + l[3][c]) / (W * H), 0);
	}
	affine(fr, 3, 3, H, 0.25 * (0.06711056 + 0.00583715), 0.06711056, 0.00583715);
	affine(fr, 4, 3, H, 0, 0, 1);
	/* Marcha y polvo: a = A·dy + B en la fila j de la resolución reducida,
	 * dy = 1/2 − 0,25/H − (j + 0,5)/lh. */
	fr->low_k[0] = (float)(FONDO_A * (0.5 - 0.25 / H) + FONDO_B);
	fr->low_ky[0] = (float)(-FONDO_A / vw->lh);
	fr->unpack_k[0] = (float)(vw->lw / 4);
	fr->unpack_k[1] = (float)(-0.25 / W);

	/* Filas con polvo: g_min < 3 en alguna columna. Para cada t_k, las dy
	 * con |A·dy·t_k + h_k| < 3, y entre dos t_k seguidas con distinto
	 * signo, el tramo entre sus raíces. */
	double lo = 1e9, hi = -1e9, h[FONDO_TS];
	const double dt = 30.0 / (FONDO_TS - 1);
	for (uint32_t i = 0; i < vw->lw; i += 2) {
		double dx = fondo_low_dx(vw, i);
		h_line(h, FONDO_TS, dx * 10.0, dx * dt, 10.0, dt, T);
		double prev = 0;
		for (int k = 0; k < FONDO_TS; k++) {
			double at = FONDO_A * polvo_t(k), r = -h[k] / at;
			double a = (-3.0 - h[k]) / at, b = (3.0 - h[k]) / at;
			lo = a < lo ? a : lo;
			hi = b > hi ? b : hi;
			if (k) {
				double m = r < prev ? r : prev, M = r > prev ? r : prev;
				lo = m < lo ? m : lo;
				hi = M > hi ? M : hi;
			}
			prev = r;
		}
	}
	/* py + 0,5 = H/2 − dy·H − 0,25, con dos píxeles del polvo de margen
	 * (la pasada final lo lee con filtro bilineal) y las columnas salteadas. */
	double margin = 2.0 * H / vw->lh + 2.0;
	double y0 = 0.5 * H - hi * H - 0.75 - margin, y1 = 0.5 * H - lo * H - 0.75 + margin + 1.0;
	y0 = y0 < 0 ? 0 : y0 > H ? H : y0;
	y1 = y1 < 0 ? 0 : y1 > H ? H : y1;
	fr->band_y0 = (uint32_t)y0;
	fr->band_y1 = (uint32_t)ceil(y1);
	if (fr->band_y1 < fr->band_y0)
		fr->band_y1 = fr->band_y0;
}
