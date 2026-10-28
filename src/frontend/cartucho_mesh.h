/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — la malla del cartucho 3D del menú (assets/menu/cartucho.mesh,
 * hecho por tools/cartucho-malla.py; formato en la cabecera de ese script) y
 * lo que se calcula con ella en el PPE cada cuadro: giro, descarte de caras
 * traseras, orden de pintor y luz por vértice. Sin RSX: compila en cualquier
 * host y lo prueba tests/unit/test_cartucho.c.
 */
#ifndef SALSA64_CARTUCHO_MESH_H
#define SALSA64_CARTUCHO_MESH_H

#include <stddef.h>
#include <stdint.h>

#include <rsxkit/rsxkit.h>

#define CARTUCHO_MAX_VERTS 512
#define CARTUCHO_MAX_TRIS  512

struct cartucho_mesh {
	uint32_t nv, np, nl;                 /* vértices; triángulos de plástico y de etiqueta */
	float v[CARTUCHO_MAX_VERTS][8];      /* x y z, nx ny nz, u v */
	uint16_t idx[CARTUCHO_MAX_TRIS * 3]; /* primero los del plástico */
};

/* Lee el recurso. Devuelve 0 o un código < 0 (formato, conteos, índice fuera
 * de rango o una normal que no mide 1). */
int cartucho_mesh_parse(struct cartucho_mesh *m, const uint8_t *data, size_t size);

/* Lo que sale de cartucho_mesh_draw: triángulos sueltos listos para
 * rsxkit_draw_triangles, de atrás hacia adelante. */
struct cartucho_out {
	rsxkit_vertex plastic[CARTUCHO_MAX_TRIS * 3];
	rsxkit_vertex label[CARTUCHO_MAX_TRIS * 3];
	uint32_t nplastic, nlabel; /* vértices */
};

/* El cartucho girado `yaw` (alrededor de su eje vertical) e inclinado
 * `pitch` (radianes), de `size` unidades lógicas de alto, centrado en
 * (cx, cy) (y hacia abajo); proyección ortogonal. Sin caras traseras. Los
 * vértices llevan la luz en rgb (ambiente + difusa) y el brillo especular
 * en el alfa; st = (u, v) de la malla por (tw, th), el tamaño de la textura
 * de cada material. El orden de pintor vale dentro de cada material: la
 * etiqueta va delante del plástico y se dibuja después. Usa memoria
 * estática de trabajo (un solo hilo). */
void cartucho_mesh_draw(const struct cartucho_mesh *m, float yaw, float pitch, float cx, float cy, float size,
			float ptw, float pth, float ltw, float lth, struct cartucho_out *out);

/* Gris de respaldo de la etiqueta (sin la textura del recurso) y de los
 * íconos planos. */
#define CARTUCHO_LABEL_GRAY 0xB4B8BEu
#define CARTUCHO_LABEL_TEX  256
/* El interior del sticker en la textura de la etiqueta (fracciones de ancho
 * y alto de la imagen, v hacia abajo): llega hasta abajo, donde el sticker
 * sigue fuera de las caras de la etiqueta. */
#define CARTUCHO_STICKER_U0 0.0646f
#define CARTUCHO_STICKER_U1 0.8711f
#define CARTUCHO_STICKER_V0 0.11f

/* Textura de la etiqueta: RGBA8888 (bytes R, G, B, A) de CARTUCHO_LABEL_TEX
 * por CARTUCHO_LABEL_TEX, `pitch` bytes por fila (también el de `base`).
 * Parte de `base` (la etiqueta del recurso, o gris liso si es NULL) y, con
 * carátula (RGBA8888, w × h; NULL, ninguna), la estira dentro del sticker
 * con las esquinas de arriba redondeadas (5 % del ancho); el resto queda
 * como `base`. */
void cartucho_label_compose(uint8_t *dst, uint32_t pitch, const uint8_t *base, const uint8_t *cover_rgba,
			    unsigned w, unsigned h);

#endif
