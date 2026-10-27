/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — la parte del fondo «Shader» del menú que corre en la CPU, la
 * misma en el PPE (fondo.c) y en la referencia de la Mac (tools/fondo-ref.c):
 * tablas del hash, malla de la ola, tabla del polvo y constantes de las
 * pasadas. Ver docs/fondo-shader.md.
 *
 * Basado en el shader «XdGfRR» de Shadertoy, de int_45h
 * (https://www.shadertoy.com/view/XdGfRR). Ver docs/THIRD_PARTY.md.
 *
 * Cómo se reparte el original:
 * - La ola es la cáscara |g| < 0,04 alrededor de la superficie g = 0, con
 *   g = A·y + h(x, z) (un campo de alturas). Lo que acumula el raymarching
 *   es 0,015 por paso de 0,05 dentro de la cáscara, por f(t): la integral
 *   de 0,3·f(t) sobre el tramo del rayo dentro de ella. Con F̃ una primitiva
 *   de 0,3·f, eso es la suma de ±F̃(t) en cada cruce del rayo con las dos
 *   superficies g = +0,04 y g = −0,04 (− al entrar, + al salir), y entrar
 *   o salir lo dice hacia dónde mira el triángulo. Las dos superficies van
 *   como mallas que el RSX rasteriza sumando en una superficie fp16: cada
 *   píxel recibe exactamente sus cruces, también los rasantes (las líneas
 *   brillantes) sin buscar nada por píxel. Dos tiras cierran la cáscara en
 *   t = 40 (el rayo sigue adentro: +F̃(40)) y en t = 1.
 * - El polvo: 1 − g_min/3, con g_min el mínimo de |g| para t en (10, 40].
 *   Para un rayo, g(t) = A·dy·t + h(dx·t, t): h depende solo de la columna,
 *   así que la CPU tabula h en 32 valores de t por columna y la pasada del
 *   polvo (a resolución reducida) solo hace g_k = dy·A·t_k + h_k.
 * - Las estrellas leen el hash exacto del original desde texturas (anillos
 *   que la CPU rellena a medida que el campo se mueve); el brillo de cada
 *   capa, pow(value2d·0,5 + 0,5, 8,3), va horneado a 8 muestras por celda.
 */
#ifndef SALSA64_FONDO_CALC_H
#define SALSA64_FONDO_CALC_H

#include <stdbool.h>
#include <stdint.h>

#include "frontend/fondo_ref.h"

/* g = A·y + B·z + 2,5 + o/2, con (A, B) = 2·normalize(0, 1, 0,05). */
#define FONDO_A 1.99750467f
#define FONDO_B 0.09987523f
/* Cáscara y primitiva centrada: F̃(t) = 0,3·∫₀ᵗ f − FONDO_C0 (fp16 más fino). */
#define FONDO_SHELL 0.04f
#define FONDO_C0    3.5f
#define FONDO_AMAX  0.615f  /* 41 pasos dentro (MAX_DRAWS + 1) de 0,015 */

/* Malla de la ola: (NU + 1) × (NZ + 1) vértices en u = dx (la columna del
 * rayo) y z (= t), de z = Z0 a 40. */
#define FONDO_NU 64
#define FONDO_NZ 256
#define FONDO_Z0 1.0f
#define FONDO_SURF_VERTS   ((FONDO_NU + 1) * (FONDO_NZ + 1))
#define FONDO_SURF_INDICES (FONDO_NU * FONDO_NZ * 6)
#define FONDO_CAP_VERTS    (2 * (FONDO_NU + 1))
#define FONDO_CAP_INDICES  (FONDO_NU * 6)
/* Vértices: superficie +0,04, superficie −0,04, tapa en t = 40, tapa en t = Z0. */
#define FONDO_VERT_SURF_P 0
#define FONDO_VERT_SURF_N FONDO_SURF_VERTS
#define FONDO_VERT_CAP_FAR (2 * FONDO_SURF_VERTS)
#define FONDO_VERT_CAP_NEAR (2 * FONDO_SURF_VERTS + FONDO_CAP_VERTS)
#define FONDO_VERTS (2 * FONDO_SURF_VERTS + 2 * FONDO_CAP_VERTS)
/* Índices: los de una superficie y luego los de una tapa. */
#define FONDO_INDICES (FONDO_SURF_INDICES + FONDO_CAP_INDICES)

/* Tabla de h de la marcha y el polvo (resolución reducida, lw × lh con lw
 * múltiplo de 4): una fila por cada 4 columnas (una por canal), FONDO_NT
 * valores de t_j = 40·j/(NT − 1) de h − 2,5 − B·t en fp16. La marcha da los
 * FONDO_STEPS pasos del original desde t = 1 (el jitter medio); el polvo
 * mira g en FONDO_TS valores de t en [10, 40]. */
#define FONDO_NT    512
#define FONDO_STEPS 100
#define FONDO_TS    32
#define FONDO_T0    1.0f

/* Texturas del hash (RGBA8, un anillo por canal). */
#define FONDO_STAR_W 2048
#define FONDO_STAR_H 1024
#define FONDO_BRI_W  1024
#define FONDO_BRI_H  1024
#define FONDO_BRI_SS 8      /* muestras del brillo por celda */
#define FONDO_LAYERS 3

/* Vértice de la malla (el de rsxkit_post_vertex): posición en el espacio
 * de recorte y TEXCOORD0 = (t, 0, 0, 0). */
typedef struct fondo_vertex {
	float pos[4];
	float t0[4];
} fondo_vertex;

/* Píxeles de la pantalla y de la pasada del polvo. */
struct fondo_view {
	uint32_t w, h;
	uint32_t lw, lh;
};

/* Rectángulo de la red ya escrito en un anillo: [x0, x1) × [y0, y1). */
struct fondo_ring {
	bool valid;
	int64_t x0, x1, y0, y1;
};

/* Texturas del hash en la memoria que dé quien llama: bytes R, G, B, A por
 * texel; R, G y B son las capas 1, 2 y 3 (estrellas y brillo). */
struct fondo_tex {
	uint8_t *star;  /* FONDO_STAR_W × FONDO_STAR_H × 4 */
	uint8_t *bri;   /* FONDO_BRI_W × FONDO_BRI_H × 4 */
	struct fondo_ring ring[2 * FONDO_LAYERS];
	uint32_t filled;  /* texeles escritos en la última llamada */
};

/* Constantes afines de la pasada final (rsxkit_post_pass k, kx, ky: el
 * valor de TEXCOORD(j + 1) en el centro del píxel (px, py) de la pantalla es
 * k[j] + kx[j]·(px + 0,5) + ky[j]·(py + 0,5)), las de la marcha y el polvo
 * (a = A·dy + B por fila de la resolución reducida) y las filas de la
 * pantalla donde hay polvo. Ver fondo_final.cgh. */
struct fondo_frame {
	float k[5][4], kx[5][4], ky[5][4];
	float low_k[4], low_ky[4];  /* TEXCOORD1 de la marcha y el polvo */
	float unpack_k[4];          /* TEXCOORD1 del desempaque */
	uint32_t band_y0, band_y1;  /* filas [y0, y1) con estrellas */
};

/* Índices de una superficie (triángulos sobre la red de (NU + 1) × (NZ + 1))
 * y de una tapa (tiras de NU cuadros), en `idx` (FONDO_INDICES). */
void fondo_indices(uint16_t *idx);

/* Vértices de la malla para el instante T (FONDO_VERTS). */
void fondo_mesh(fondo_vertex *v, const struct fondo_view *vw, double T);

/* Tabla de h: vw->lw / 4 filas de FONDO_NT texeles de 4 fp16 (el orden de
 * la máquina), `pitch` bytes por fila. */
void fondo_htable(uint16_t *tab, uint32_t pitch, const struct fondo_view *vw, double T);
/* dx del rayo de la columna i de la resolución reducida, dy de la fila j. */
double fondo_low_dx(const struct fondo_view *vw, uint32_t i);
double fondo_low_dy(const struct fondo_view *vw, uint32_t j);

/* Rellena en los anillos lo que el instante T necesita y no está. */
void fondo_tex_update(struct fondo_tex *t, const struct fondo_view *vw, double T);
/* Olvida lo escrito (otra resolución). */
void fondo_tex_reset(struct fondo_tex *t);

/* Constantes del cuadro: degradado a la hora `day_s`, desplazamientos de
 * las estrellas y del brillo, filas con polvo. */
void fondo_frame_consts(struct fondo_frame *fr, const struct fondo_view *vw, double T, double day_s);

/* h(x, z) de la superficie (g = A·y + h) y la primitiva centrada F̃. */
double fondo_h(double x, double z, double T);
float fondo_prim(float t);

/* fp16 ↔ float (redondeo al par más cercano). */
uint16_t fondo_half(float f);
float fondo_unhalf(uint16_t h);

#endif
