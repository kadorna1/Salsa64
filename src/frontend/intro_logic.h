/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — la secuencia de arranque (intro.c) sin dependencias del RSX: la
 * línea de tiempo, la fecha y la hora al estilo de una videocasetera y la
 * nieve. Corre igual en el host, para las pruebas unitarias.
 */
#ifndef SALSA64_FRONTEND_INTRO_LOGIC_H
#define SALSA64_FRONTEND_INTRO_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Duraciones (segundos). Con la carga ya lista, la secuencia dura
 * POWER + STATIC_MIN + CUT + LOGO + FADE ≈ 2,8 s; si la carga tarda más, la
 * estática sigue hasta que termine. */
#define INTRO_POWER_S      0.50f  /* encendido del CRT: punto, línea, apertura */
#define INTRO_STATIC_MIN_S 1.00f  /* estática, como mínimo */
#define INTRO_CUT_S        0.12f  /* corte de estática antes del logo */
#define INTRO_LOGO_S       0.90f  /* logo */
#define INTRO_FADE_S       0.25f  /* fundido a negro, antes del menú */

enum intro_phase { INTRO_POWER, INTRO_STATIC, INTRO_CUT, INTRO_LOGO, INTRO_FADE, INTRO_DONE };

struct intro_clock {
	enum intro_phase phase;
	float start;     /* segundo en que empezó la fase en curso */
	float load_at;   /* segundo en que se vio la carga lista (< 0: aún no) */
	bool skipped;
};

void intro_clock_init(struct intro_clock *c);
/* Avanza a t segundos desde el primer cuadro. `loaded`: la carga terminó.
 * `skip`: el jugador pidió saltarla (va directo a INTRO_DONE). */
enum intro_phase intro_clock_step(struct intro_clock *c, float t, bool loaded, bool skip);

/* Fecha y hora como las pone una videocasetera: «OCT.27 2026» y «20:45:06».
 * `utc_sec`: segundos desde 1970; `tz_minutes`: la zona de la consola
 * (minutos al este de UTC); `dst`: 1 si hay horario de verano (suma 60). */
void intro_vcr_text(uint64_t utc_sec, int tz_minutes, int dst, char date[16], char time[16]);

/* Nieve: llena w × h píxeles RGBA (bytes R, G, B, A) con gris-blanco de
 * grano fino (de 16 a 215). `seed` avanza con cada llamada (xorshift32; 0 se cambia por
 * una semilla fija). */
void intro_noise_fill(uint32_t *seed, uint8_t *rgba, unsigned w, unsigned h);

#endif
