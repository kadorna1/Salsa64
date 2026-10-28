/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — buscar el archivo de un juego (carátula o trucos) por un nombre
 * parecido: los paquetes de RetroArch llevan nombres de No-Intro
 * («007 - GoldenEye (USA).png», «1080 Snowboarding (JU) (M2).cht») y las ROMs
 * del usuario casi nunca se llaman igual. Sin dependencias de PSL1GHT.
 */
#ifndef SALSA64_FRONTEND_NAMEMATCH_H
#define SALSA64_FRONTEND_NAMEMATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Busca en `dir` un archivo con extensión `ext` («.png», sin distinguir
 * mayúsculas) cuyo nombre, sin lo que va entre () y [] ni signos, sea el de la
 * ROM o el título (o lo contenga, con 5 letras o más). Si el archivo y la ROM
 * dicen de qué región son (USA, Europa, Japón: por las etiquetas del nombre o,
 * en la ROM, por la letra de región de `code`) y no coinciden, no sirve: los
 * trucos de otra región escriben en otras direcciones. Entre los que sirven
 * gana el nombre igual, luego el que contiene o está dentro del otro, y a
 * igualdad la misma región. `min_key`: letras que debe tener el nombre de la
 * ROM para aceptar un parecido que no sea igual (los títulos cortos como «MACE»
 * o «RUSH» se confunden con otros juegos: 4 para carátulas, que si fallan solo
 * dan una imagen equivocada, y 8 para trucos, que escribirían en la RDRAM).
 * Deja la ruta en `path`. */
bool name_find(const char *dir, const char *ext, const char *rom_name, const char *title, const char *code, size_t min_key,
	       char *path, size_t size);

/* Lo mismo con una lista de nombres (uno por línea, p. ej. el índice de un
 * servidor) en lugar de una carpeta: deja el nombre elegido en `name`. */
bool name_find_list(const char *list, size_t len, const char *ext, const char *rom_name, const char *title,
		    const char *code, size_t min_key, char *name, size_t size);

/* Una carpeta leída una sola vez: nombres con la extensión `ext` y sus claves ya
 * hechas, para buscar sin volver al disco (y sin normalizar cada nombre en cada
 * búsqueda). NULL si no se pudo abrir la carpeta. Hasta 8192 nombres y 2 MB;
 * con 1000 carátulas son ~100 KB. */
struct name_index;
struct name_index *name_index_build(const char *dir, const char *ext);
void name_index_free(struct name_index *x);
size_t name_index_count(const struct name_index *x);
size_t name_index_bytes(const struct name_index *x);  /* memoria reservada */
/* <base><ext> tal cual o con otras mayúsculas (el nombre del archivo, en `name`). */
bool name_index_exact(const struct name_index *x, const char *ext, const char *base, size_t base_len, char *name,
		      size_t size);
/* Como name_find, sobre el índice. */
bool name_index_find(const struct name_index *x, const char *ext, const char *rom_name, const char *title,
		     const char *code, size_t min_key, char *name, size_t size);

#endif
