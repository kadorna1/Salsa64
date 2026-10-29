/*
 * Salsa64 — capas de 8 bits dibujadas en la RDRAM sin pasar por el RSX
 * (layer8.h). Portable: el PPE y las pruebas del host.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "rdp/layer8.h"

#include <math.h>
#include <string.h>

/* Rojo y alfa del combiner que se admiten: sin color de vértice
 * (k[j][1] y k[j][2] en cero) ni segunda textura; el rojo es el texel
 * (T0 por 1, nada más) o una constante (ONE, sin textura), y el alfa, si
 * hay prueba, el alfa del texel o una constante. */
static bool combiner_ok(const struct gfx_draw *d, bool *tex_red, bool *tex_alpha)
{
	if (d->nterms || d->fog || d->ntex > 1)
		return false;
	for (int j = 0; j < GFX_K_TERMS; j++)
		for (int l = 0; l < 4; l++)
			if (d->k[j][1][l] != 0.0f || d->k[j][2][l] != 0.0f)
				return false;
	if (d->k[GFX_K_T1][0][0] != 0.0f || d->k[GFX_K_T1A][0][0] != 0.0f || d->k[GFX_K_T0A][0][0] != 0.0f)
		return false;
	if (d->ntex == 1 && d->k[GFX_K_T0][0][0] == 1.0f && d->k[GFX_K_ONE][0][0] == 0.0f)
		*tex_red = true;
	else if (d->k[GFX_K_T0][0][0] == 0.0f)
		*tex_red = false;
	else
		return false;
	*tex_alpha = false;
	if (d->atest != GFX_ATEST_NONE) {
		if (d->k[GFX_K_T1][0][3] != 0.0f || d->k[GFX_K_T1A][0][3] != 0.0f || d->k[GFX_K_T0][0][3] != 0.0f)
			return false;
		if (d->ntex == 1 && d->k[GFX_K_T0A][0][3] == 1.0f && d->k[GFX_K_ONE][0][3] == 0.0f)
			*tex_alpha = true;
		else if (d->k[GFX_K_T0A][0][3] != 0.0f)
			return false;
	}
	return true;
}

bool layer8_draw_ok(const struct gfx_draw *d, const struct gfx_vertex *v)
{
	bool tr, ta;
	if (d->h.b < 3 || d->h.b % 3 != 0 || d->blend != GFX_BLEND_OFF || d->depth != 0 || d->viscvg ||
	    d->cycle == 1 /* G_CYC_2CYCLE */)
		return false;
	if (!combiner_ok(d, &tr, &ta))
		return false;
	if (d->ntex == 1) {
		const struct gfx_tex *t = &d->tex[0];
		if (t->where == GFX_TEX_TARGET || (t->flags & GFX_TEX_BILINEAR) || !t->width || !t->height ||
		    GFX_TEX_WRAP_S(t->flags) == GFX_WRAP_MIRROR_ONCE || GFX_TEX_WRAP_T(t->flags) == GFX_WRAP_MIRROR_ONCE)
			return false;
	}
	for (uint32_t k = 0; k < d->h.b; k++)
		if (v[k].w != 1.0f)
			return false;
	return true;
}

static inline int wrap(int i, int n, unsigned mode)
{
	if (mode == GFX_WRAP_CLAMP)
		return i < 0 ? 0 : i >= n ? n - 1 : i;
	int m = i % n;
	if (m < 0)
		m += n;
	if (mode == GFX_WRAP_MIRROR && ((i < 0 ? -1 - i : i) / n) & 1)
		m = n - 1 - m;
	return m;
}

static inline float edge(float ax, float ay, float bx, float by, float px, float py)
{
	return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

uint32_t layer8_draw(uint8_t *rdram, uint32_t rdram_size, uint32_t addr, uint32_t width, const struct gfx_draw *d,
		     const struct gfx_vertex *v, const uint8_t *arena, const uint8_t *cache)
{
	bool tex_red = false, tex_alpha = false;
	if (!combiner_ok(d, &tex_red, &tex_alpha))
		return 0;
	const struct gfx_tex *t = d->ntex ? &d->tex[0] : NULL;
	const uint8_t *tp = NULL;
	if (t) {
		const uint8_t *base = t->where == GFX_TEX_CACHE ? cache : arena;
		if (!base)
			return 0;
		tp = base + t->offset;
	}
	/* Como channel() del host: el rojo constante y la prueba de alfa. */
	uint8_t red_const = (uint8_t)(d->k[GFX_K_ONE][0][0] * 255.0f + 0.5f);
	bool alpha_const_fails = d->atest != GFX_ATEST_NONE && !tex_alpha &&
				 d->k[GFX_K_ONE][0][3] * 255.0f + 0.5f < (float)d->aref;
	if (alpha_const_fails)
		return 0;
	float x0 = d->scissor[0] / 4.0f, y0 = d->scissor[1] / 4.0f;
	float x1 = d->scissor[2] / 4.0f, y1 = d->scissor[3] / 4.0f;
	if (x1 > (float)width)
		x1 = (float)width;
	uint32_t written = 0;
	for (uint32_t k = 0; k + 3 <= d->h.b; k += 3) {
		const struct gfx_vertex *a = &v[k];
		float sx[3] = { a[0].x, a[1].x, a[2].x }, sy[3] = { a[0].y, a[1].y, a[2].y };
		float area = edge(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
		if (area == 0.0f)
			continue;
		if ((d->cull == GFX_CULL_BACK && area > 0.0f) || (d->cull == GFX_CULL_FRONT && area < 0.0f))
			continue;
		float minx = fminf(fminf(sx[0], sx[1]), sx[2]), maxx = fmaxf(fmaxf(sx[0], sx[1]), sx[2]);
		float miny = fminf(fminf(sy[0], sy[1]), sy[2]), maxy = fmaxf(fmaxf(sy[0], sy[1]), sy[2]);
		int px0 = (int)floorf(fmaxf(minx, x0)), px1 = (int)ceilf(fminf(maxx, x1));
		int py0 = (int)floorf(fmaxf(miny, y0)), py1 = (int)ceilf(fminf(maxy, y1));
		if (px0 < 0)
			px0 = 0;
		if (py0 < 0)
			py0 = 0;
		if (px1 > (int)width)
			px1 = (int)width;
		for (int y = py0; y < py1; y++) {
			uint32_t row = addr + (uint32_t)y * width;
			if (row + width > rdram_size)
				break;
			for (int x = px0; x < px1; x++) {
				float cx = x + 0.5f, cy = y + 0.5f;
				if (cx < x0 || cx >= x1 || cy < y0 || cy >= y1)
					continue;
				float b0 = edge(sx[1], sy[1], sx[2], sy[2], cx, cy) / area;
				float b1 = edge(sx[2], sy[2], sx[0], sy[0], cx, cy) / area;
				float b2 = 1.0f - b0 - b1;
				if (b0 < 0.0f || b1 < 0.0f || b2 < 0.0f)
					continue;
				uint8_t red = red_const;
				if (t) {
					/* Como shade() del host con w = 1: la interpolación
					 * con perspectiva se reduce a las baricéntricas. */
					float pw = b0 + b1 + b2, p0 = b0 / pw, p1 = b1 / pw, p2 = b2 / pw;
					float s = p0 * a[0].s + p1 * a[1].s + p2 * a[2].s;
					float tt = p0 * a[0].t + p1 * a[1].t + p2 * a[2].t;
					float u = s * t->xf[0] + t->xf[2], vv = tt * t->xf[1] + t->xf[3];
					int tx = wrap((int)floorf(u * t->width), t->width, GFX_TEX_WRAP_S(t->flags));
					int ty = wrap((int)floorf(vv * t->height), t->height, GFX_TEX_WRAP_T(t->flags));
					const uint8_t *p = tp + (uint32_t)ty * t->pitch + 4u * (uint32_t)tx;
					if (tex_alpha && p[3] < d->aref)
						continue;
					if (tex_red)
						red = p[0];
				}
				rdram[row + (uint32_t)x] = red;
				written++;
			}
		}
	}
	return written;
}

void layer8_run(uint8_t *rdram, uint32_t rdram_size, const uint8_t *cmd, uint32_t cmd_bytes,
		const struct gfx_vertex *vtx, const uint8_t *arena, const uint8_t *cache, uint32_t min_width,
		struct layer8_result *out)
{
	memset(out, 0, sizeof *out);
	/* Primera pasada: por destino, todos sus dibujos de la tarea caben o
	 * ninguno (un tramo en la RDRAM y otro en el RSX dejaría la capa a
	 * medias en los dos lados). */
	uint32_t addr[LAYER8_MAX], width[LAYER8_MAX], n = 0;
	bool ok[LAYER8_MAX], any[LAYER8_MAX];
	uint32_t off = 0;
	int cur = -1;
	while (off + sizeof(struct gfx_cmd) <= cmd_bytes) {
		const struct gfx_cmd *c = (const struct gfx_cmd *)(cmd + off);
		if (c->op == GFX_OP_END || c->size < sizeof *c)
			break;
		off += c->size;
		if (c->op == GFX_OP_TARGET) {
			cur = -1;
			if ((c->c & 0xFF) != 1u /* G_IM_SIZ_8b */ || c->b <= min_width || c->b > 640 || !c->a)
				continue;
			uint32_t i = 0;
			while (i < n && !(addr[i] == c->a && width[i] == c->b))
				i++;
			if (i == n) {
				if (n == LAYER8_MAX) {
					out->refused++;
					continue;
				}
				addr[n] = c->a;
				width[n] = c->b;
				ok[n] = true;
				any[n] = false;
				n++;
			}
			cur = (int)i;
		} else if (c->op == GFX_OP_DRAW && cur >= 0) {
			any[cur] = true;
			if (c->size < sizeof(struct gfx_draw) ||
			    !layer8_draw_ok((const struct gfx_draw *)c, vtx + ((const struct gfx_draw *)c)->h.a))
				ok[cur] = false;
		}
	}
	for (uint32_t i = 0; i < n; i++) {
		if (!any[i])
			ok[i] = false;
		else if (!ok[i])
			out->refused++;
	}
	/* Segunda: dibujar, en el orden de la tarea. */
	off = 0;
	cur = -1;
	while (off + sizeof(struct gfx_cmd) <= cmd_bytes) {
		const struct gfx_cmd *c = (const struct gfx_cmd *)(cmd + off);
		if (c->op == GFX_OP_END || c->size < sizeof *c)
			break;
		off += c->size;
		if (c->op == GFX_OP_TARGET) {
			cur = -1;
			for (uint32_t i = 0; i < n; i++)
				if (ok[i] && addr[i] == c->a && width[i] == c->b)
					cur = (int)i;
		} else if (c->op == GFX_OP_DRAW && cur >= 0) {
			const struct gfx_draw *d = (const struct gfx_draw *)c;
			out->pixels += layer8_draw(rdram, rdram_size, addr[cur], width[cur], d, vtx + d->h.a, arena, cache);
			out->draws++;
		}
	}
	for (uint32_t i = 0; i < n; i++)
		if (ok[i]) {
			out->addr[out->n] = addr[i];
			out->width[out->n] = width[i];
			out->n++;
		}
}
