/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — el cartucho 3D de la fila elegida del menú (modelo «Stylized N64»,
 * docs/THIRD_PARTY.md): la malla de cartucho_mesh.h dibujada por el RSX con
 * rsxkit_draw_triangles, girando despacio. El menú lo llama por un gancho del
 * 2D (rsxkit_ui_hook), así que queda encima de la franja de la fila y debajo
 * de lo que se dibuja después (el panel). Si algo falla, init devuelve
 * false y el menú dibuja el sprite.
 *
 * Memoria: 1 MB de memoria principal mapeada al RSX (dos búferes de
 * vértices de 128 KB, la textura del plástico y la de la etiqueta, de
 * 256 × 256 × 4 bytes cada una), reservada al iniciar y para siempre.
 */
#ifndef SALSA64_CARTUCHO3D_H
#define SALSA64_CARTUCHO3D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Una vez, con rsxkit ya iniciado: el recurso .mesh y la textura del
 * plástico (RGBA8888, bytes R, G, B, A, `w` × `h` hasta 256 × 256). */
bool cartucho3d_init(const uint8_t *mesh, size_t mesh_size, const uint8_t *plastic_rgba, unsigned w, unsigned h);
bool cartucho3d_ready(void);

/* Una vez por cuadro del menú, antes de dibujar: la fila elegida (si cambia,
 * el giro arranca de frente), la carátula de esa fila (RGBA8888, `w` × `h`; NULL
 * si no hay: etiqueta en gris) y el tiempo en segundos. Con una carátula
 * nueva rehace la textura de la etiqueta (entre cuadros: espera al RSX). */
void cartucho3d_update(int sel, const uint8_t *cover_rgba, unsigned w, unsigned h, double t);

/* El gancho del menú (menu.h, `cart3d`): dibuja el cartucho de `size`
 * unidades lógicas de alto con el centro en (cx, cy). */
void cartucho3d_draw(void *user, float cx, float cy, float size, float alpha);

/* Tiempo medio de dibujo en µs (armar los vértices en el PPE + enviarlos) y
 * cuadros medidos desde el último llamado; los pone a cero. */
void cartucho3d_stats(double *avg_us, unsigned *frames);

#endif
