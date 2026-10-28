/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — miniaturas de las carátulas del menú y la carátula completa de la
 * fila elegida, decodificadas fuera del hilo del menú.
 *
 * Las miniaturas (56 × 64, recortadas a la proporción del sticker y con las
 * esquinas redondeadas) van a un atlas de la ranura THUMBS_SLOT del 2D con una
 * caché de 24 casillas por LRU, y se copian también a la memoria del PPE (la
 * etiqueta del cartucho 3D las usa mientras llega la carátula completa). Un
 * hilo del PPE de baja prioridad, una carátula a la vez, hace todo lo lento:
 *   1. mira la caché en disco (covers/.mini/<nombre>.mini): la miniatura lista
 *      con el tamaño y la fecha del PNG, o «esta ROM no tiene carátula» con la
 *      fecha de la carpeta; si vale, no busca ni decodifica;
 *   2. si no, busca el PNG (cover_find), lo lee, lo decodifica, lo reduce y
 *      escribe la caché.
 * Con la selección en movimiento (debounce) solo atiende las que están en disco.
 * Primero atiende la carátula completa de la fila elegida (que de paso deja
 * lista su miniatura: se decodifica una sola vez) y luego las filas por
 * cercanía a la elegida, las visibles y las que van a entrar al hacer scroll.
 *
 * Memoria: 576 KB de la ranura del 2D (rsxkit_ui_init), 336 KB de copias de las
 * miniaturas, un búfer de decodificación de COVER_RGBA_BYTES (560 KB), otro de
 * ~50 KB para la caché en disco y la pila del hilo (128 KB), reservados al
 * iniciar. Los archivos van por newlib (nunca sysFs*, que sin sysModuleLoad
 * cuelga la consola).
 */
#ifndef SALSA64_THUMBS_H
#define SALSA64_THUMBS_H

#include <stdbool.h>
#include <stdint.h>

#include "frontend/romlist.h"

#define THUMBS_SLOT 2

/* Después de rsxkit_ui_init. `covers_dir` debe vivir mientras se use. */
bool thumbs_init(const char *covers_dir);
bool thumbs_ready(void);
/* Una vez por cuadro del menú, antes de dibujarlo: copia al atlas lo que el
 * hilo terminó y le pide lo siguiente. `moving`: la selección o el scroll están
 * en movimiento: solo se cargan las miniaturas que ya estén en disco; lo que hay
 * que buscar y decodificar espera a que lleve quieta ~180 ms (y si se vuelve a
 * mover, lo que no empezó se descarta). */
void thumbs_frame(bool moving);
/* Descarta la carátula completa pedida si el hilo aún no la empezó (la selección
 * volvió a moverse); true si la descartó. */
bool thumbs_big_cancel(void);
/* El gancho `thumb` del menú (menu.h): la casilla de la carátula de `e` en el
 * atlas (rect = x, y, w, h en píxeles) o false si no está (la pide). `dist`
 * es la distancia de la fila a la elegida: se atiende primero la más cercana. */
bool thumbs_get(void *user, const struct rom_entry *e, float dist, float rect[4]);
/* Los píxeles de la miniatura (THUMB_W × THUMB_H RGBA8888, de thumbs_calc.h) o
 * NULL si aún no están (la pide con la mayor prioridad entre las filas). Vale
 * hasta que la casilla se reemplace: usarla en el mismo cuadro. */
const uint8_t *thumbs_pixels(const struct rom_entry *e);
/* La carátula completa de `e` (la fila elegida): se pide una vez y se espera
 * con thumbs_big_poll. Pedir otra antes de que llegue reemplaza a la anterior. */
void thumbs_big_request(const struct rom_entry *e, int token);
/* El `token` de la carátula completa que ya llegó y espera, o −1. */
int thumbs_big_ready(void);
/* Si ya llegó lo último que se pidió: copia la carátula a `dst`
 * (COVER_RGBA_BYTES; NULL: la descarta) con su tamaño, dice si existía y devuelve el `token` de
 * la petición. false si aún no hay nada. */
bool thumbs_big_poll(uint8_t *dst, int *token, bool *ok, unsigned *w, unsigned *h);
/* Después de descargar carátulas: los juegos que no tenían vuelven a buscarse
 * (la caché en disco de «sin carátula» caduca sola con la fecha de la carpeta). */
void thumbs_forget_missing(void);

#endif
