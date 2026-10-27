/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — ROM del cartucho por fragmentos.
 *
 * Una ROM que no cabe en memoria (hacks y homebrew de más de 64 MB; la N64
 * direcciona hasta 252 MB de cartucho) se queda en el disco y el núcleo lee
 * páginas bajo demanda a una caché con LRU. La caché (decenas de MB) la pone
 * quien llama, preasignada al abrir el juego: nada se reserva en el bucle de
 * emulación. Por omisión las lecturas son síncronas, desde el hilo de emulación;
 * cada vez que se trae una página y la anterior ya está en la caché
 * (acceso secuencial, como un DMA grande del PI) se lee también la siguiente.
 *
 * Con salsa64_rompg_async_start el disco lo lee un hilo aparte. Solo el hilo
 * de emulación decide qué ranura se pisa y qué página se pide (las estructuras
 * de la caché no se comparten); el hilo lector solo llena la ranura que se le
 * entrega y la marca lista con una barrera de liberación. Una ranura en lectura
 * nunca se expulsa ni se ve a medias. Al fallar una página, o al seguir un
 * acceso que ya venía precargado, se piden también las `depth` siguientes sin
 * esperarlas. Quien necesita una página que ya viene en camino espera solo a
 * esa lectura; una página fría se pone al frente de la cola.
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

struct salsa64_rompg_async;   /* hilo lector, cola y cerrojos (rompg.c) */

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
	uint8_t *slot_st;             /* ranura -> 0 libre, 1 leyéndose, 2 lista (la escribe el lector con liberación) */
	uint8_t *slot_pf;             /* ranura traída por adelantado y aún sin usar */
	uint32_t *q;                  /* cola circular de ranuras por leer (nslots entradas) */
	uint32_t q_head, q_n;
	uint32_t depth;               /* páginas que se piden por adelantado en modo asíncrono */
	struct salsa64_rompg_async *as;   /* NULL = lectura síncrona */
	/* Estadísticas acumuladas: accesos (por llamada a read), fallos (páginas leídas por falta), páginas leídas por
	 * adelantado, errores de lectura, microsegundos y bytes leídos del disco y la lectura más larga. */
	uint64_t acc, miss, ra, errs, read_us, read_bytes, read_max_us;
	/* Modo asíncrono: precargas acertadas (página lista al pedirla), esperas a una página que venía en camino y sus
	 * microsegundos (total y la más larga), y lo que se esperó por los fallos fríos. */
	uint64_t pf_hit, wait_n, wait_us, wait_max_us, cold_us, cold_max_us;
	/* Ventana anterior de salsa64_rompg_window. */
	uint64_t w_acc, w_miss, w_ra, w_us, w_pf_hit, w_wait_n, w_wait_us, w_cold_us;
};

/* Lo ocurrido desde la última llamada a salsa64_rompg_window. */
struct salsa64_rompg_win {
	uint64_t acc, miss, ra, us;        /* accesos, fallos fríos, páginas por adelantado, µs de disco */
	uint64_t pf_hit, wait_n;           /* precargas acertadas y esperas a páginas en camino */
	uint64_t wait_us, wait_max_us;     /* µs esperando páginas en camino y la espera más larga */
	uint64_t cold_us;                  /* µs esperando por fallos fríos */
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

/* Pasa a lectura en segundo plano: crea el hilo lector y pide `depth` páginas por adelantado (se recorta a nslots/4,
 * mínimo 1). Se llama una vez tras open, desde el hilo de emulación. 0 = bien; si falla sigue síncrona. */
int salsa64_rompg_async_start(struct salsa64_rompg *pg, unsigned depth);

/* Estadísticas desde la última llamada (y reinicia la espera máxima de la ventana). */
void salsa64_rompg_window(struct salsa64_rompg *pg, struct salsa64_rompg_win *w);

#endif
