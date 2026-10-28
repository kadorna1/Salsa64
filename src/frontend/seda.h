/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — fondo «Seda» del menú: cintas de tela que flotan y ondulan,
 * con brillo en los bordes, sobre un degradado de la hora de la consola y
 * con destellos. Las mallas las calcula un SPE (src/spu/seda/main.c) y el
 * RSX las suma con un fragment program de Fresnel. Ver docs/fondo-seda.md.
 */
#ifndef SALSA64_SEDA_H
#define SALSA64_SEDA_H

#include <stdbool.h>

/* Dibuja el fondo en la pantalla del cuadro en curso, en el instante `t`
 * (segundos), antes de rsxkit_ui_begin. La primera vez reserva lo suyo
 * (1 MB de memoria principal, un SPE y 6 programas del RSX). false si no
 * pudo: el menú dibuja las ondas, y no lo vuelve a intentar hasta
 * seda_release. */
bool seda_draw(double t);

/* Suelta todo, el SPE incluido (al abrir un juego, al salir del menú o con
 * otro fondo). */
void seda_release(void);

#endif
