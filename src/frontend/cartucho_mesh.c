/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Ver cartucho_mesh.h. */
#include "frontend/cartucho_mesh.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be16(const uint8_t *p)
{
	return (uint32_t)p[0] << 8 | p[1];
}

static float bef(const uint8_t *p)
{
	uint32_t u = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
	float f;
	memcpy(&f, &u, sizeof f);
	return f;
}

int cartucho_mesh_parse(struct cartucho_mesh *m, const uint8_t *d, size_t size)
{
	if (!m || !d || size < 12 || memcmp(d, "CMSH", 4) || be16(d + 4) != 1)
		return -1;
	uint32_t nv = be16(d + 6), np = be16(d + 8), nl = be16(d + 10);
	if (!nv || nv > CARTUCHO_MAX_VERTS || np + nl == 0 || np + nl > CARTUCHO_MAX_TRIS)
		return -2;
	if (size != 12 + (size_t)nv * 32 + (size_t)(np + nl) * 6)
		return -3;
	m->nv = nv;
	m->np = np;
	m->nl = nl;
	const uint8_t *p = d + 12;
	for (uint32_t i = 0; i < nv; i++, p += 32) {
		for (int k = 0; k < 8; k++) {
			m->v[i][k] = bef(p + 4 * k);
			if (!isfinite(m->v[i][k]))
				return -4;
		}
		float n = sqrtf(m->v[i][3] * m->v[i][3] + m->v[i][4] * m->v[i][4] + m->v[i][5] * m->v[i][5]);
		if (fabsf(n - 1.0f) > 0.01f)
			return -5;
	}
	for (uint32_t i = 0; i < (np + nl) * 3; i++, p += 2) {
		m->idx[i] = (uint16_t)be16(p);
		if (m->idx[i] >= nv)
			return -6;
	}
	return 0;
}

/* Luz: de arriba a la izquierda y de frente; el espectador mira por −z. */
#define LX (-0.45f)
#define LY 0.55f
#define LZ 0.70f

struct sorted {
	float z;
	uint16_t tri;
};

static int by_depth(const void *a, const void *b)
{
	float za = ((const struct sorted *)a)->z, zb = ((const struct sorted *)b)->z;
	return za < zb ? -1 : za > zb;
}

static struct {
	float p[CARTUCHO_MAX_VERTS][3]; /* posición girada */
	uint8_t rgba[CARTUCHO_MAX_VERTS][4];
	struct sorted s[CARTUCHO_MAX_TRIS];
} sc;

static uint32_t emit(const struct cartucho_mesh *m, uint32_t first, uint32_t count, float cx, float cy, float size,
		     float tw, float th, rsxkit_vertex *out)
{
	uint32_t ns = 0;
	for (uint32_t t = first; t < first + count; t++) {
		const uint16_t *i = m->idx + 3 * t;
		const float *a = sc.p[i[0]], *b = sc.p[i[1]], *c = sc.p[i[2]];
		/* Cara delantera: antihoraria vista desde +z con la y hacia arriba. */
		float cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
		if (cross <= 0.0f)
			continue;
		sc.s[ns].z = a[2] + b[2] + c[2];
		sc.s[ns].tri = (uint16_t)t;
		ns++;
	}
	qsort(sc.s, ns, sizeof sc.s[0], by_depth);
	uint32_t n = 0;
	for (uint32_t k = 0; k < ns; k++) {
		const uint16_t *i = m->idx + 3 * sc.s[k].tri;
		for (int j = 0; j < 3; j++, n++) {
			rsxkit_vertex *v = &out[n];
			const float *p = sc.p[i[j]];
			v->pos[0] = cx + p[0] * size;
			v->pos[1] = cy - p[1] * size;
			v->pos[2] = 0.5f;
			v->pos[3] = 1.0f;
			v->st[0] = m->v[i[j]][6] * tw;
			v->st[1] = m->v[i[j]][7] * th;
			memcpy(v->rgba, sc.rgba[i[j]], 4);
			v->pad = 0;
		}
	}
	return n;
}

void cartucho_mesh_draw(const struct cartucho_mesh *m, float yaw, float pitch, float cx, float cy, float size,
			float ptw, float pth, float ltw, float lth, struct cartucho_out *out)
{
	float cyw = cosf(yaw), syw = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	float ll = sqrtf(LX * LX + LY * LY + LZ * LZ), lx = LX / ll, ly = LY / ll, lz = LZ / ll;
	/* Vector a la media entre la luz y el ojo (+z). */
	float hx = lx, hy = ly, hz = lz + 1.0f, hl = sqrtf(hx * hx + hy * hy + hz * hz);
	hx /= hl, hy /= hl, hz /= hl;
	for (uint32_t i = 0; i < m->nv; i++) {
		const float *v = m->v[i];
		/* Primero Ry(yaw), después Rx(pitch). */
		float x = cyw * v[0] + syw * v[2], z = -syw * v[0] + cyw * v[2];
		float y = cp * v[1] - sp * z;
		z = sp * v[1] + cp * z;
		sc.p[i][0] = x, sc.p[i][1] = y, sc.p[i][2] = z;
		float nx = cyw * v[3] + syw * v[5], nz = -syw * v[3] + cyw * v[5];
		float ny = cp * v[4] - sp * nz;
		nz = sp * v[4] + cp * nz;
		float diff = nx * lx + ny * ly + nz * lz;
		float spec = nx * hx + ny * hy + nz * hz;
		if (diff < 0.0f)
			diff = 0.0f;
		float s2 = spec > 0.0f ? spec * spec : 0.0f;
		s2 *= s2, s2 *= s2, s2 *= s2;  /* spec^16 */
		float lum = 0.50f + 0.62f * diff;
		if (lum > 1.0f)
			lum = 1.0f;
		sc.rgba[i][0] = sc.rgba[i][1] = sc.rgba[i][2] = (uint8_t)(lum * 255.0f + 0.5f);
		sc.rgba[i][3] = (uint8_t)(fminf(1.0f, 0.30f * s2) * 255.0f + 0.5f);
	}
	out->nplastic = emit(m, 0, m->np, cx, cy, size, ptw, pth, out->plastic);
	out->nlabel = emit(m, m->np, m->nl, cx, cy, size, ltw, lth, out->label);
}

/* Muestra bilineal de la carátula en (fx, fy) píxeles (centro del píxel en +0,5). */
static void sample(const uint8_t *img, unsigned w, unsigned h, float fx, float fy, float acc[3])
{
	fx -= 0.5f, fy -= 0.5f;
	if (fx < 0.0f) fx = 0.0f;
	if (fy < 0.0f) fy = 0.0f;
	if (fx > (float)(w - 1)) fx = (float)(w - 1);
	if (fy > (float)(h - 1)) fy = (float)(h - 1);
	unsigned x0 = (unsigned)fx, y0 = (unsigned)fy, x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
	float tx = fx - (float)x0, ty = fy - (float)y0;
	for (int c = 0; c < 3; c++) {
		float a = img[((size_t)y0 * w + x0) * 4 + c] * (1.0f - tx) + img[((size_t)y0 * w + x1) * 4 + c] * tx;
		float b = img[((size_t)y1 * w + x0) * 4 + c] * (1.0f - tx) + img[((size_t)y1 * w + x1) * 4 + c] * tx;
		acc[c] += a * (1.0f - ty) + b * ty;
	}
}

void cartucho_label_compose(uint8_t *dst, uint32_t pitch, const uint8_t *base, const uint8_t *cover, unsigned w,
			    unsigned h)
{
	const int N = CARTUCHO_LABEL_TEX;
	const uint8_t g[3] = { (CARTUCHO_LABEL_GRAY >> 16) & 255u, (CARTUCHO_LABEL_GRAY >> 8) & 255u,
			       CARTUCHO_LABEL_GRAY & 255u };
	const float fx0 = CARTUCHO_STICKER_U0 * (float)N, fx1 = CARTUCHO_STICKER_U1 * (float)N;
	const float fy0 = CARTUCHO_STICKER_V0 * (float)N, fy1 = (float)N;
	const float rad = 0.05f * (fx1 - fx0);
	for (int y = 0; y < N; y++) {
		uint8_t *row = dst + (size_t)y * pitch;
		for (int x = 0; x < N; x++) {
			uint8_t *px = row + 4 * x;
			if (base) {
				memcpy(px, base + (size_t)y * pitch + 4 * x, 4);
				px[3] = 255;
			} else {
				px[0] = g[0], px[1] = g[1], px[2] = g[2], px[3] = 255;
			}
			if (!cover || !w || !h)
				continue;
			float cx = (float)x + 0.5f, cy = (float)y + 0.5f;
			if (cx < fx0 || cx >= fx1 || cy < fy0 || cy >= fy1)
				continue;
			/* Esquinas de arriba redondeadas (abajo el sticker sigue fuera de las caras). */
			float cov = 1.0f;
			float dx = fminf(cx - fx0, fx1 - cx), dy = cy - fy0;
			if (dx < rad && dy < rad) {
				float ex = rad - dx, ey = rad - dy;
				cov = fminf(1.0f, fmaxf(0.0f, rad - sqrtf(ex * ex + ey * ey) + 0.5f));
			}
			if (cov <= 0.0f)
				continue;
			float acc[3] = { 0, 0, 0 };
			/* 2 × 2 muestras por píxel: la carátula suele ser más grande que la textura. */
			for (int j = 0; j < 2; j++)
				for (int i = 0; i < 2; i++)
					sample(cover, w, h, ((float)x - fx0 + 0.25f + 0.5f * (float)i) / (fx1 - fx0) * (float)w,
					       ((float)y - fy0 + 0.25f + 0.5f * (float)j) / (fy1 - fy0) * (float)h, acc);
			for (int c = 0; c < 3; c++)
				px[c] = (uint8_t)(acc[c] * 0.25f * cov + (float)px[c] * (1.0f - cov) + 0.5f);
		}
	}
}
