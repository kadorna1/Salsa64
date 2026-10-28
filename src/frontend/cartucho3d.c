/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — el cartucho 3D del menú. Ver cartucho3d.h.
 *
 * El PPE arma los vértices cada cuadro (cartucho_mesh_draw: 498 triángulos,
 * giro, caras traseras fuera, orden de pintor y luz por vértice) directamente
 * en memoria mapeada al RSX, y se dibujan con rsxkit_draw_triangles sobre la
 * pantalla del cuadro: primero el plástico y luego la etiqueta. La pantalla
 * del cuadro no tiene profundidad, de ahí el orden de pintor.
 *
 * color = textura × luz + brillo, con la luz en el rgb del vértice y el brillo
 * en su alfa (combinación lineal fija de rsxkit: X0 = C, X4 = C.aaaa).
 */
#include "frontend/cartucho3d.h"

#include <malloc.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <sys/systime.h>

#include <rsxkit/rsxkit.h>

#include "frontend/cartucho_mesh.h"
#include "platform/ps3/log.h"

#define TEX_N 256u
#define TEX_PITCH (TEX_N * 4u)
#define TEX_BYTES (TEX_PITCH * TEX_N)
#define VB_BYTES  (128u * 1024u)
#define OFF_VB(i) ((uint32_t)(i) * VB_BYTES)
#define OFF_PLASTIC (2u * VB_BYTES)
#define OFF_LABEL   (OFF_PLASTIC + TEX_BYTES)
#define OFF_BASE    (OFF_LABEL + TEX_BYTES) /* la etiqueta sin carátula, para rehacer la de arriba */
#define MEM_BYTES   (1u << 20)

_Static_assert(sizeof(struct cartucho_out) <= VB_BYTES, "los vértices caben en su mitad");
_Static_assert(OFF_BASE + TEX_BYTES <= MEM_BYTES, "el cartucho cabe en 1 MB");
_Static_assert(offsetof(struct cartucho_out, label) % 128 == 0, "los vértices de la etiqueta alineados");

#define SECONDS_PER_TURN 6.0

static struct {
	bool ok;
	uint8_t *mem;
	uint32_t off;
	struct cartucho_mesh mesh;
	unsigned pw, ph;
	uint32_t fence[2];
	int half;
	int sel;
	bool label_cover;  /* la textura de la etiqueta lleva carátula */
	unsigned label_w;  /* de qué ancho: la miniatura y la carátula completa */
	int label_sel;
	double t, t0;
	/* Medición. */
	uint64_t us;
	unsigned frames;
} c3 = { .sel = -2, .label_sel = -2 };

bool cartucho3d_ready(void)
{
	return c3.ok;
}

static void fence_all(void)
{
	for (int i = 0; i < 2; i++)
		if (c3.fence[i])
			rsxkit_fence_wait(c3.fence[i]);
}

bool cartucho3d_init(const uint8_t *mesh, size_t mesh_size, const uint8_t *plastic, unsigned w, unsigned h,
		     const uint8_t *label, unsigned lw, unsigned lh)
{
	if (c3.ok)
		return true;
	if (!plastic || !w || !h || w > TEX_N || h > TEX_N || !label || lw != TEX_N || lh != TEX_N) {
		salsa64_log("cartucho 3D: texturas inválidas (plástico %ux%u, etiqueta %ux%u)", w, h, lw, lh);
		return false;
	}
	int rc = cartucho_mesh_parse(&c3.mesh, mesh, mesh_size);
	if (rc) {
		salsa64_log("cartucho 3D: malla inválida (%d)", rc);
		return false;
	}
	c3.mem = memalign(1u << 20, MEM_BYTES);
	if (!c3.mem) {
		salsa64_log("cartucho 3D: sin %u KB de memoria principal", MEM_BYTES >> 10);
		return false;
	}
	if (rsxkit_map_main(c3.mem, MEM_BYTES, &c3.off) != RSXKIT_OK) {
		salsa64_log("cartucho 3D: no se pudo mapear la memoria para el RSX");
		free(c3.mem);
		c3.mem = NULL;
		return false;
	}
	for (unsigned y = 0; y < h; y++)
		memcpy(c3.mem + OFF_PLASTIC + (size_t)y * TEX_PITCH, plastic + (size_t)y * w * 4, (size_t)w * 4);
	memcpy(c3.mem + OFF_BASE, label, (size_t)TEX_BYTES);
	cartucho_label_compose(c3.mem + OFF_LABEL, TEX_PITCH, c3.mem + OFF_BASE, NULL, 0, 0);
	__sync_synchronize();
	c3.pw = w;
	c3.ph = h;
	c3.ok = true;
	salsa64_log("cartucho 3D: %u vértices, %u + %u triángulos, 1 MB de memoria principal", c3.mesh.nv, c3.mesh.np,
		    c3.mesh.nl);
	return true;
}

void cartucho3d_update(int sel, const uint8_t *cover, unsigned w, unsigned h, double t)
{
	if (!c3.ok)
		return;
	c3.t = t;
	if (sel != c3.sel) {
		c3.sel = sel;
		c3.t0 = t;  /* el giro arranca de frente */
	}
	bool has = cover && w && h;
	if (sel != c3.label_sel || has != c3.label_cover || (has && w != c3.label_w)) {
		fence_all();  /* el RSX puede estar leyendo la textura anterior */
		cartucho_label_compose(c3.mem + OFF_LABEL, TEX_PITCH, c3.mem + OFF_BASE, has ? cover : NULL, w, h);
		__sync_synchronize();
		c3.label_sel = sel;
		c3.label_cover = has;
		c3.label_w = w;
	}
}

static rsxkit_draw make_draw(uint32_t tex_off, unsigned w, unsigned h, float alpha, uint8_t wrap)
{
	rsxkit_draw d;
	memset(&d, 0, sizeof d);
	d.blend = RSXKIT_BLEND_OFF;
	d.cull = RSXKIT_CULL_NONE;
	d.depth = 0;
	d.ntex = 1;
	d.scissor[2] = d.scissor[3] = 100000.0f;
	rsxkit_texture *t = &d.tex[0];
	t->offset = tex_off;
	t->pitch = TEX_PITCH;
	t->width = (uint16_t)w;
	t->height = (uint16_t)h;
	t->location = RSXKIT_MEM_MAIN;
	t->format = RSXKIT_RGBA8888;
	t->wrap_s = t->wrap_t = wrap;
	t->filter = RSXKIT_FILTER_LINEAR;
	t->xf[0] = 1.0f / (float)w;
	t->xf[1] = 1.0f / (float)h;
	/* rgb = T0 · C + C.aaaa (la luz y el brillo del vértice); alfa = la opacidad
	 * de la fila (la textura es opaca): con menos de 1, mezcla con lo de atrás. */
	for (int k = 0; k < 3; k++) {
		d.k[RSXKIT_K_T0][1][k] = 1.0f;
		d.k[RSXKIT_K_ONE][2][k] = 1.0f;
	}
	d.k[RSXKIT_K_T0][0][3] = alpha;
	d.blend = alpha < 0.995f ? RSXKIT_BLEND_ALPHA : RSXKIT_BLEND_OFF;
	return d;
}

void cartucho3d_draw(void *user, float cx, float cy, float size, float alpha)
{
	(void)user;
	if (!c3.ok || alpha <= 0.0f)
		return;
	uint64_t t0 = sysGetSystemTime();
	c3.half ^= 1;
	int hf = c3.half;
	if (c3.fence[hf])
		rsxkit_fence_wait(c3.fence[hf]);  /* el RSX ya no lee estos vértices (de hace dos cuadros) */
	struct cartucho_out *out = (struct cartucho_out *)(c3.mem + OFF_VB(hf));
	double e = c3.t - c3.t0;
	float yaw = (float)(6.283185307179586 * e / SECONDS_PER_TURN);
	cartucho_mesh_draw(&c3.mesh, yaw, 0.0f, cx, cy, size, (float)c3.pw, (float)c3.ph, (float)TEX_N, (float)TEX_N, out);
	__sync_synchronize();
	rsxkit_vertices(c3.off + OFF_VB(hf), RSXKIT_MEM_MAIN);
	float a = alpha > 1.0f ? 1.0f : alpha;
	rsxkit_draw dp = make_draw(c3.off + OFF_PLASTIC, c3.pw, c3.ph, a, RSXKIT_WRAP_REPEAT);
	if (out->nplastic)
		rsxkit_draw_triangles(&dp, 0, out->nplastic);
	rsxkit_draw dl = make_draw(c3.off + OFF_LABEL, TEX_N, TEX_N, a, RSXKIT_WRAP_CLAMP);
	if (out->nlabel)
		rsxkit_draw_triangles(&dl, (uint32_t)(offsetof(struct cartucho_out, label) / sizeof(rsxkit_vertex)), out->nlabel);
	c3.fence[hf] = rsxkit_fence();
	c3.us += sysGetSystemTime() - t0;
	c3.frames++;
}

void cartucho3d_stats(double *avg_us, unsigned *frames)
{
	*avg_us = c3.frames ? (double)c3.us / c3.frames : 0.0;
	*frames = c3.frames;
	c3.us = 0;
	c3.frames = 0;
}
