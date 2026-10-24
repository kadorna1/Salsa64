/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — política portable de escala interna automática.
 *
 * Recibe ventanas de contadores ya medidos por el frontend. No conoce el
 * RSX, hilos ni menús: solo decide si conviene bajar, subir o avisar.
 */
#ifndef SALSA64_FRONTEND_SCALE_AUTO_H
#define SALSA64_FRONTEND_SCALE_AUTO_H

#include <stdbool.h>
#include <stdint.h>

#define SCALE_AUTO_DOWN_US       3000000ull
#define SCALE_AUTO_UP_US        10000000ull
#define SCALE_AUTO_COOLDOWN_US  10000000ull
#define SCALE_AUTO_WARN_US      30000000ull
#define SCALE_AUTO_EMU_PM             920u
#define SCALE_AUTO_UP_RSX_PM           600u

enum scale_auto_action {
	SCALE_AUTO_NONE,
	SCALE_AUTO_DOWN,
	SCALE_AUTO_UP,
	SCALE_AUTO_WARN,
};

struct scale_auto_sample {
	uint64_t duration_us;
	uint64_t rsx_ns;       /* tareas del juego + presentación, marcas del RSX */
	uint32_t period_ns;    /* período de un campo de la tele emulada */
	uint32_t fields;
	uint32_t presented;
	uint32_t skipped;
};

struct scale_auto_result {
	enum scale_auto_action action;
	uint32_t emu_pm;       /* ritmo del VI, milésimas del nominal */
	uint32_t rsx_pm;       /* tiempo del RSX por campo, milésimas del período */
	uint64_t sustained_us; /* cuánto duró la condición que decidió */
};

struct scale_auto {
	bool automatic;
	uint8_t current, maximum;
	uint64_t overload_us, margin_us, cooldown_us;
};

void scale_auto_init(struct scale_auto *p, bool automatic, unsigned current, unsigned maximum);
struct scale_auto_result scale_auto_update(struct scale_auto *p, const struct scale_auto_sample *s);

#endif
