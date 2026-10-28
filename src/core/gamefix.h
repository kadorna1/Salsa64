/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — arreglos por juego en el código cargado en la RDRAM.
 *
 * Para los casos en que un juego depende de que el VR4300 sea más lento que
 * el nuestro (una instrucción por ciclo, sin esperas de caché ni de RDRAM) y
 * eso lo lleva a un camino que en la consola no ocurre. Cada arreglo se
 * identifica por el código y los CRC del encabezado y solo se aplica si las
 * palabras de la RDRAM son exactamente las originales; así no toca otros
 * juegos, otras versiones ni código que el juego haya cambiado.
 */
#ifndef SALSA64_CORE_GAMEFIX_H
#define SALSA64_CORE_GAMEFIX_H

#include <stdint.h>

struct salsa64_bus;

/* Elige los arreglos de la ROM puesta en el bus (encabezado en bus->rom). */
void salsa64_gamefix_select(struct salsa64_bus *bus);
/* Al empezar cada campo del VI: aplica los arreglos cuyo código original
 * esté en la RDRAM (y avisa al recompilador). */
void salsa64_gamefix_field(struct salsa64_bus *bus);
/* Nombre del arreglo elegido para la ROM o NULL si no hay. */
const char *salsa64_gamefix_name(const struct salsa64_bus *bus);

#endif
