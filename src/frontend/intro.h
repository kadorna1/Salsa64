/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — secuencia de arranque que imita una tele CRT: encendido (punto,
 * línea, apertura con destello), estática con el rótulo «No Signal», el logo
 * (como si la tele encontrara señal) y fundido a negro. Todo pasa por el
 * filtro Trinitron (filters.h): la secuencia se dibuja en un destino de
 * 640 × 480 (rsxkit_ui_begin_target) que el filtro reduce y presenta.
 *
 * Ocupa el tiempo de la carga (fuente, ajustes, lista de ROMs: un hilo,
 * main.c) sin alargarlo; si la carga acaba antes, dura lo de intro_logic.h.
 * Memoria: docs/memory-budget.md («Secuencia de arranque»).
 *
 * Uso (main.c): intro_begin(); por cuadro intro_update(), y mientras no dé
 * INTRO_DONE, rsxkit_frame_begin + intro_draw + rsxkit_frame_end; al final
 * intro_end().
 */
#ifndef SALSA64_FRONTEND_INTRO_H
#define SALSA64_FRONTEND_INTRO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "intro_logic.h"

/* Prepara el destino, la nieve y el logo (se decodifica en `scratch`, de
 * `scratch_size` bytes: el búfer de carátulas). false si algo falla (el 2D
 * del RSX, el destino, la memoria): se sigue sin secuencia. El 2D de
 * rsxkit/ui.h debe estar listo. */
bool intro_begin(uint8_t *scratch, size_t scratch_size);

/* Avanza el reloj de la secuencia. `loaded`: la carga terminó; `skip`: se
 * pidió saltarla. */
enum intro_phase intro_update(bool loaded, bool skip);

/* Dibuja el cuadro en curso (entre rsxkit_frame_begin y _end). false si el
 * filtro falló: hay que terminar la secuencia. */
bool intro_draw(void);

/* Suelta la memoria y las tablas del filtro (espera al RSX) y escribe en el
 * log cuánto duró. `loaded_ms`: lo que tardó la carga desde que empezó. */
void intro_end(uint32_t loaded_ms);

/* Después de intro_end: el negro que se va del menú (0 = ya nada), para
 * dibujarlo como último rectángulo del cuadro (fundido de entrada). */
uint8_t intro_menu_fade(void);

#endif
