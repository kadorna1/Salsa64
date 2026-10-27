/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — fondo «Seda» del menú: la geometría y los colores de la cinta.
 * Lo comparten el PPE (camino de respaldo y pruebas), el SPE
 * (src/spu/seda/main.c, el camino principal) y las herramientas de la Mac
 * (tools/seda-vista.c): el mismo código C, sin PSL1GHT, solo `float`.
 *
 * Todo el movimiento sale de funciones propias: sumas de senos con otras
 * frecuencias, direcciones y velocidades, y una torsión de la cinta a lo
 * largo de su eje. Nada viene de ningún firmware ni de tablas ajenas.
 *
 * Cada capa es una cinta de SEDA_NU × SEDA_NV vértices. El eje de la cinta
 * C(s) (s de −1 a 1) serpentea en el espacio; el ancho gira alrededor del eje
 * con un ángulo φ(s) que da más de media vuelta, así que la tela se tuerce y
 * queda de canto en algunos puntos; encima van los pliegues, cuatro ondas
 * h(s, v) que corren a lo largo de la cinta y se cruzan en diagonal.
 * El vértice lleva, en t0, lo que el fragment program necesita:
 *   x = |n·V|        (de frente, 1; de canto, 0)
 *   y = n·H          (Blinn: el brillo que corre por los pliegues)
 *   z = intensidad   (de la capa y de la orilla)
 *   w = tono         (0..1 en la rampa de color de la capa)
 * El tiempo es una fase: la fracción de SEDA_PERIOD segundos, y cada onda
 * da un número entero de ciclos por periodo, así que no hay saltos al
 * reiniciar.
 */
#ifndef SALSA64_SEDA_CALC_H
#define SALSA64_SEDA_CALC_H

#include <stdint.h>

#define SEDA_NU      128   /* vértices a lo largo de la cinta */
#define SEDA_NV      24    /* y a lo ancho */
#define SEDA_LAYERS  3
#define SEDA_LAYER_VERTS (SEDA_NU * SEDA_NV)
#define SEDA_SPARKS  48    /* destellos (cada uno, un cuadrito de 4 vértices) */
#define SEDA_SPARK_VERTS (SEDA_SPARKS * 4)
/* Todos los vértices de un cuadro: las capas y, después, los destellos. */
#define SEDA_VERTS   (SEDA_LAYERS * SEDA_LAYER_VERTS + SEDA_SPARK_VERTS)
#define SEDA_INDICES ((SEDA_NU - 1) * (SEDA_NV - 1) * 6)
#define SEDA_SPARK_INDICES (SEDA_SPARKS * 6)
#define SEDA_PERIOD  600.0  /* segundos de un ciclo completo de las fases */

/* Vértice: el mismo diseño que rsxkit_post_vertex (32 bytes). */
typedef struct seda_vertex {
	float pos[4];  /* espacio de recorte: x/w e y/w en [−1, 1], y hacia arriba */
	float t0[4];
} seda_vertex;

/* La fase del instante `t` (segundos): fracción de SEDA_PERIOD en [0, 1). */
float seda_phase(double t);

/* Posiciones (x, y, z, 0) en el espacio de la cámara de la capa `layer`, en
 * orden i·SEDA_NV + j (i a lo largo, j a lo ancho). `aspect` = ancho/alto
 * de la pantalla. */
void seda_positions(int layer, float aspect, float tf, float pos[SEDA_LAYER_VERTS][4]);
/* La fila i de vértices (SEDA_NV) de la capa a partir de sus posiciones:
 * normales por diferencias de la red, proyección y t0. */
void seda_row(int layer, float aspect, const float pos[SEDA_LAYER_VERTS][4], int i, seda_vertex *out);
/* Toda la capa, con `scratch` de memoria de trabajo (el camino del PPE). */
void seda_layer(int layer, float aspect, float tf, float scratch[SEDA_LAYER_VERTS][4], seda_vertex *out);
/* Los SEDA_SPARK_VERTS vértices de los destellos (cuadritos que brillan y
 * suben); t0 = (x, y de −1 a 1 en el cuadrito, brillo, tono). */
void seda_sparkles(float aspect, float tf, seda_vertex *out);
/* Índices de una capa (SEDA_INDICES) y de los destellos (SEDA_SPARK_INDICES);
 * los de una capa sirven con el desplazamiento de vértices de cada una. */
void seda_indices(uint16_t *idx);
void seda_spark_indices(uint16_t *idx);

/* Colores según la hora local (segundos desde la medianoche). */
struct seda_colors {
	float top[3], bottom[3];     /* el degradado del fondo, arriba y abajo */
	float tint_a[3], tint_b[3];  /* los dos extremos del tono de la tela */
	float glow[3];               /* el resplandor detrás de la cinta */
	float stars;                 /* brillo de los destellos (más de noche) */
};
void seda_colors(double day_seconds, struct seda_colors *c);
/* Ganancia de color de la capa (la rampa de su textura es tinte · ganancia). */
float seda_layer_gain(int layer);

#endif
