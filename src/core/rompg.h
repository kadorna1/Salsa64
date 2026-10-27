/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — ROM del cartucho por fragmentos.
 *
 * Una ROM que no cabe en memoria (hacks y homebrew de más de 64 MB; la N64
 * direcciona hasta 252 MB de cartucho) se queda en el disco y el núcleo lee
 * páginas bajo demanda a una caché con LRU. La caché (decenas de MB) la pone
 * quien llama, preasignada al abrir el juego: nada se reserva en el bucle de
 * emulación. Las lecturas son síncronas, siempre desde el hilo de emulación;
 * cada vez que se trae una página y la anterior ya está en la caché
 * (acceso secuencial, como un DMA grande del PI) se lee también la siguiente.
 *
 * El intercambio de bytes de los volcados .v64 y .n64 se aplica página a
 * página al leerla, así que el núcleo ve siempre el orden de la consola.
 *
 * Adaptado de src/core/cart.c de SalsaDS (mismo autor, GPL-2.0-or-later).
 */
#ifndef SALSA64_CORE_ROMPG_H
#define SALSA64_CORE_ROMPG_H

#include "core/rom.h"
#include "core/types.h"

#define SALSA64_ROMPG_PAGE_SHIFT 18u   /* páginas de 256 KB por omisión */

struct salsa64_rompg {
	/* Lee `len` bytes del archivo en `off`; devuelve los leídos (<0 = error). */
	int (*rd)(void *user, uint64_t off, uint8_t *dst, size_t len);
	void *user;
	uint64_t (*clock_us)(void);   /* opcional: para medir lo que tarda cada lectura */
	uint8_t *mem;                 /* nslots páginas de (1 << shift) bytes */
	int32_t *map;                 /* página -> ranura, -1 = fuera de la caché */
	uint32_t *slot_page;          /* ranura -> página (0xFFFFFFFF = libre) */
	uint64_t *slot_stamp;         /* uso más reciente (LRU) */
	uint64_t stamp;
	uint32_t size;                /* bytes de la ROM */
	uint32_t shift, nslots, npages;
	uint32_t last_page;           /* última página servida y su dirección, para el acceso de palabra */
	const uint8_t *last_ptr;
	uint8_t fmt;                  /* enum salsa64_rom_format del archivo */
	/* Estadísticas acumuladas: accesos (por llamada a read), fallos (páginas leídas por falta), páginas leídas por
	 * adelantado, errores de lectura, microsegundos y bytes leídos del disco y la lectura más larga. */
	uint64_t acc, miss, ra, errs, read_us, read_bytes, read_max_us;
	/* Ventana anterior de salsa64_rompg_window. */
	uint64_t w_acc, w_miss, w_ra, w_us;
};

/* Abre la caché sobre `mem` (mem_bytes >> page_shift ranuras, al menos 2). `size` es el tamaño del archivo (hasta lo que
 * direcciona el cartucho). 0 = bien; si falla no deja nada reservado (salvo `mem`, que sigue siendo de quien llama). */
int salsa64_rompg_open(struct salsa64_rompg *pg, int (*rd)(void *, uint64_t, uint8_t *, size_t), void *user,
		       uint32_t size, enum salsa64_rom_format fmt, unsigned page_shift, uint8_t *mem,
		       size_t mem_bytes);
void salsa64_rompg_close(struct salsa64_rompg *pg);

/* Copia `len` bytes de la ROM (ya en orden z64) desde `off`. El llamador asegura off + len <= size. */
void salsa64_rompg_read(struct salsa64_rompg *pg, uint32_t off, uint8_t *dst, uint32_t len);
/* Palabra big-endian en `off` (off + 4 <= size). */
uint32_t salsa64_rompg_rd32(struct salsa64_rompg *pg, uint32_t off);

/* Estadísticas desde la última llamada: accesos, fallos, páginas por adelantado y microsegundos de lectura. */
void salsa64_rompg_window(struct salsa64_rompg *pg, uint64_t *acc, uint64_t *miss, uint64_t *ra, uint64_t *us);

#endif
