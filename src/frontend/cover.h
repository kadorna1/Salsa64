/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — carátulas: lee un PNG y lo deja en RGBA8888 (bytes R, G, B, A)
 * reducido a lo más COVER_MAX_W × COVER_MAX_H, listo para
 * rsxkit_ui_image_set.
 *
 * Archivos: SALSA64_DATA_DIR/covers/<nombre de la ROM sin extensión>.png o,
 * si no está, <código del juego>.png (los 4 caracteres de la cabecera, p. ej.
 * NSME.png). Sin dependencias de PSL1GHT. El desinflado de los IDAT es el de zlib; puff (de
 * Mark Adler, escrito de nuevo para este programa) queda dentro para comparar
 * tiempos (cover_use_puff) y como respaldo. Admite todos
 * los tipos de color y profundidades de PNG, también entrelazado (Adam7).
 * La imagen se reduce por promedio de las cajas que caen en cada píxel; una
 * más chica que el máximo se deja como está (la escala el RSX).
 */
#ifndef SALSA64_FRONTEND_COVER_H
#define SALSA64_FRONTEND_COVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COVER_MAX_W 320
#define COVER_MAX_H 448
#define COVER_RGBA_BYTES (COVER_MAX_W * COVER_MAX_H * 4)

/* Topes del pico de memoria en el menú (archivo + IDAT + desinflado, unos
 * 40 MB; docs/memory-budget.md): una carátula de 2400 × 2500 RGBA cabe. */
#define COVER_MAX_FILE_BYTES (8u << 20)   /* PNG más grande que esto no se lee */
#define COVER_MAX_RAW_BYTES  (24u << 20)  /* ni los que desinflados pasan de esto */

enum cover_result {
	COVER_OK,
	COVER_NO_FILE,
	COVER_BAD,          /* no es un PNG o está dañado */
	COVER_TOO_BIG,      /* el archivo o la imagen desinflada pasan del límite */
	COVER_UNSUPPORTED,  /* un modo de PNG que no existe (profundidad que no va con el tipo) */
};

/* Lo más que pidió con malloc el último cover_decode_png, aparte del archivo en memoria
 * (los IDAT juntos y lo desinflado, o lo desinflado y las sumas de la reducción, y el
 * estado de zlib), en bytes: sale del tamaño real de esa imagen, no de los topes. */
size_t cover_decode_peak(void);

/* Con true, los IDAT se desinflan con puff en vez de zlib (por omisión, zlib). */
void cover_use_puff(bool on);

/* Decodifica un PNG en memoria. `rgba` tiene COVER_RGBA_BYTES; deja el
 * tamaño final en *w y *h. */
enum cover_result cover_decode_png(const uint8_t *data, size_t size, uint8_t *rgba, unsigned *w, unsigned *h);
enum cover_result cover_load_png(const char *path, uint8_t *rgba, unsigned *w, unsigned *h);
const char *cover_result_name(enum cover_result r);

/* Índice en memoria de la carpeta de carátulas: se lee una vez (cover_index_build,
 * que tarda lo que la carpeta; hacerlo en un hilo) y cover_find busca en él sin
 * tocar el disco mientras la carpeta sea la misma. Con otra carpeta, o sin índice,
 * va al disco como siempre. cover_index_build rehace el índice (tras descargar
 * carátulas); devuelve cuántos PNG tiene y deja en *bytes la memoria. */
unsigned cover_index_build(const char *dir, size_t *bytes);
void cover_index_free(void);

/* El archivo de la carátula de una ROM en `dir`: <rom sin extensión>.png y
 * luego <code>.png (sin distinguir mayúsculas) y, por último, una de nombre
 * parecido y de su región (namematch.h): así valen los nombres de RetroArch
 * («007 - GoldenEye (USA).png»). Devuelve false si no hay. */
bool cover_find(const char *dir, const char *rom_name, const char *title, const char *code, char *path, size_t size);

#endif
