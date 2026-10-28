/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — miniaturas de carátulas para las filas del menú (thumbs.h): lo
 * que no toca el RSX ni el sistema, para probarlo en el host. Reducción de
 * una carátula a la casilla de 96 × 128 con las esquinas redondeadas del
 * sticker, y la caché de 9 casillas (LRU) del atlas de la ranura 2 del 2D
 * (320 × 448: 3 × 3 casillas con 4 y 12 píxeles de hueco transparente, para
 * que el filtro lineal no mezcle vecinas).
 */
#ifndef SALSA64_THUMBS_CALC_H
#define SALSA64_THUMBS_CALC_H

#include <stdbool.h>
#include <stdint.h>

#define THUMB_W       96
#define THUMB_H       128
#define THUMB_COLS    3
#define THUMB_CELLS   9
#define THUMB_STEP_X  100
#define THUMB_STEP_Y  140
#define THUMB_ATLAS_W 320
#define THUMB_ATLAS_H 448
#define THUMB_BYTES   (THUMB_W * THUMB_H * 4)
#define THUMB_RADIUS  0.08f  /* de las esquinas, como fracción del ancho */
#define THUMB_MISSING 64     /* juegos sin carátula que se recuerdan */

/* RGBA8888 w × h → THUMB_W × THUMB_H (promedio de bloques; la carátula se
 * estira a la casilla) con el alfa en cero fuera de las esquinas redondeadas
 * (con 1 píxel de borde suave). */
void thumb_scale(const uint8_t *rgba, unsigned w, unsigned h, uint8_t *dst);

struct thumb_cache {
	uint32_t key[THUMB_CELLS];    /* 0: libre */
	uint32_t stamp[THUMB_CELLS];  /* cuadro del último uso */
	uint32_t missing[THUMB_MISSING];
	unsigned nmissing, missing_next;
	uint32_t hits, misses;
};

uint32_t thumb_key(const char *name);  /* nunca 0 */
void thumb_cache_init(struct thumb_cache *c);
/* La casilla de `key` (y la marca usada en el cuadro `frame`), o −1. */
int thumb_cache_find(struct thumb_cache *c, uint32_t key, uint32_t frame);
/* La casilla donde guardar `key`: la libre o la menos usada, de preferencia
 * una que no se haya usado en el cuadro `frame`. La deja asignada. */
int thumb_cache_assign(struct thumb_cache *c, uint32_t key, uint32_t frame);
/* Origen (x, y) de la casilla en el atlas. */
void thumb_cell_origin(int cell, uint32_t *x, uint32_t *y);
bool thumb_cache_is_missing(const struct thumb_cache *c, uint32_t key);
void thumb_cache_mark_missing(struct thumb_cache *c, uint32_t key);
void thumb_cache_forget_missing(struct thumb_cache *c);

#endif
