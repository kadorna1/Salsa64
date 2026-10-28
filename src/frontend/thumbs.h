/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — miniaturas de las carátulas en las filas del menú que no son la
 * elegida (la elegida va en 3D, cartucho3d.h). Una caché de 9 casillas de
 * 96 × 128 en un atlas de la ranura THUMBS_SLOT del 2D; cada carátula se
 * busca, se decodifica y se reduce en un hilo del PPE de baja prioridad (una a
 * la vez), y la casilla se copia al atlas entre cuadros. Si no hay carátula
 * (o aún no llega), la fila queda con el sticker del sprite.
 *
 * Memoria: 576 KB de la ranura del 2D (rsxkit_ui_init), un búfer de
 * decodificación de COVER_RGBA_BYTES (560 KB) y la pila del hilo (128 KB),
 * reservados al iniciar.
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
 * hilo terminó y le pide la siguiente carátula que alguna fila haya pedido. */
void thumbs_frame(void);
/* El gancho `thumb` del menú (menu.h): la casilla de la carátula de `e` en el
 * atlas (rect = x, y, w, h en píxeles) o false si no está (la pide). `dist`
 * es la distancia de la fila a la elegida: se atiende primero la más cercana. */
bool thumbs_get(void *user, const struct rom_entry *e, float dist, float rect[4]);
/* Después de descargar carátulas: los juegos que no tenían vuelven a buscarse. */
void thumbs_forget_missing(void);

#endif
