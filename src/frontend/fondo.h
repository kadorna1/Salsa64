/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — fondo «Shader» del menú en el RSX: la ola translúcida, el polvo
 * de estrellas y el degradado de día o de noche del shader «XdGfRR» de
 * Shadertoy, de int_45h (https://www.shadertoy.com/view/XdGfRR; ver
 * docs/THIRD_PARTY.md y docs/fondo-shader.md).
 */
#ifndef SALSA64_FONDO_H
#define SALSA64_FONDO_H

#include <stdbool.h>

/* Dibuja el fondo en la pantalla del cuadro en curso, en el instante `t`
 * (segundos, el iTime del original), antes de rsxkit_ui_begin. La primera
 * vez reserva lo suyo (15 MB de memoria principal y la superficie de la
 * ola en la del RSX). false si no pudo: el menú dibuja las olas, y no lo
 * vuelve a intentar hasta fondo_shader_release. */
bool fondo_shader_draw(double t);

/* Suelta todo (al abrir un juego, al salir del menú o con otro fondo). */
void fondo_shader_release(void);

#endif
