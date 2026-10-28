/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — miniaturas de carátulas para las filas del menú (thumbs.h): lo
 * que no toca el RSX ni el sistema, para probarlo en el host. Reducción de
 * una carátula a la casilla de 96 × 128 con las esquinas redondeadas del
 * sticker, y la caché de 9 casillas (LRU) del atlas de la ranura 2 del 2D
 * (320 × 448: 5 × 5 casillas, 24 en uso, con 4 píxeles de hueco transparente,
 * para que el filtro lineal no mezcle vecinas).
 */
#ifndef SALSA64_THUMBS_CALC_H
#define SALSA64_THUMBS_CALC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THUMB_W       56
#define THUMB_H       64
#define THUMB_COLS    5
#define THUMB_CELLS   24
#define THUMB_STEP_X  60
#define THUMB_STEP_Y  68
#define THUMB_ATLAS_W 320
#define THUMB_ATLAS_H 448
#define THUMB_BYTES   (THUMB_W * THUMB_H * 4)
#define THUMB_RADIUS  0.08f  /* de las esquinas, como fracción del ancho */
#define THUMB_MISSING 64     /* juegos sin carátula que se recuerdan en memoria */

/* Recorte para llenar una caja de tw × th con una imagen w × h sin deformarla: el
 * rectángulo centrado de la imagen (x, y, cw, ch) con la proporción de la caja. */
void cover_fit_crop(unsigned w, unsigned h, unsigned tw, unsigned th, unsigned *x, unsigned *y, unsigned *cw, unsigned *ch);

/* Archivo de la caché en disco de una miniatura (covers/.mini/<nombre>.mini),
 * todo big-endian: 0 "SMIN", 4 u16 versión (1), 6 u8 clase (THUMB_FILE_*), 8 u16 ancho,
 * 10 u16 alto, 12 u32 tamaño del PNG, 16 u32 fecha del PNG, 20 u16 largo de la
 * ruta, 22.. la ruta del PNG (hasta THUMB_PATH_MAX), y en THUMB_FILE_HEADER
 * los píxeles RGBA8888 (solo en la clase THUMB_FILE_OK). En THUMB_FILE_NONE
 * («no tiene carátula») el tamaño es 0 y la fecha es la de la carpeta de
 * carátulas cuando se buscó: si cambia, la búsqueda vale otra vez. */
#define THUMB_FILE_OK     1
#define THUMB_FILE_NONE   2
#define THUMB_PATH_MAX    255
#define THUMB_FILE_HEADER 280
struct thumb_file {
	int kind;
	uint32_t png_size, png_time;
	char path[THUMB_PATH_MAX + 1];
};
/* Escribe el archivo en `out` (THUMB_FILE_HEADER + THUMB_BYTES como mucho);
 * devuelve los bytes, 0 si no cabe la ruta. `px` solo con THUMB_FILE_OK. */
size_t thumb_file_pack(uint8_t *out, const struct thumb_file *f, const uint8_t *px);
/* Lee uno: 0 si es válido (con `px` apuntando a los píxeles en la clase OK), o < 0. */
int thumb_file_unpack(const uint8_t *in, size_t size, struct thumb_file *f, const uint8_t **px);

/* RGBA8888 w × h → THUMB_W × THUMB_H: la carátula llena la casilla sin
 * deformarse (se recorta centrada a la proporción de la casilla, que es la del
 * sticker) y se reduce con promedio de bloques; el alfa es cero fuera de las
 * esquinas redondeadas (con 1 píxel de borde suave). */
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
