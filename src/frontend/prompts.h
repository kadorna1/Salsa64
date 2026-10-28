/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — íconos de botones en línea con el texto (menú y pausa).
 *
 * Los íconos son de Kenney «Input Prompts» 1.5A (CC0; docs/THIRD_PARTY.md),
 * en el atlas assets/menu/prompts.png (tools/prompts-atlas.py), que main.c
 * sube a la ranura PROMPTS_SLOT del 2D. Un texto puede llevar marcadores
 * como «{X} Abrir {UPDOWN} Elegir» (los nombres, en prompts_atlas.h), y los
 * textos ya traducidos que llevan los símbolos de siempre (×, ○, □, △, ↑↓ y
 * las palabras START, SELECT, L1, R1, L2, R2, L3, R3) se dibujan igual con
 * íconos: la traducción no cambia. Un «×» pegado a un dígito (5×, 3×3) es una
 * multiplicación y se queda como texto.
 *
 * Sin el atlas cargado (prompts_enable(false)) todo se dibuja como antes, con
 * los símbolos de texto.
 */
#ifndef SALSA64_FRONTEND_PROMPTS_H
#define SALSA64_FRONTEND_PROMPTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "prompts_atlas.h"

#define PROMPTS_SLOT 3  /* ranura del 2D con el atlas (0 carátula, 1 cartucho, 2 miniaturas) */

/* Un trozo del texto: un ícono (icon >= 0; `s` es el marcador o el símbolo
 * escrito) o texto corrido (icon = -1). */
struct prompt_tok {
	int icon;
	bool marker;  /* el ícono venía como {NOMBRE} y no como símbolo */
	const char *s;
	size_t n;
};

struct prompt_iter {
	const char *base, *p;
};

void prompt_iter_init(struct prompt_iter *it, const char *s);
bool prompt_iter_next(struct prompt_iter *it, struct prompt_tok *t);

/* El texto con el que se dibuja un ícono sin atlas: «×», «START», «↑↓»… */
const char *prompt_fallback(int icon);

/* Enciende o apaga los íconos (los enciende main.c al subir el atlas). */
void prompts_enable(bool on);
bool prompts_enabled(void);

/* Lado del ícono para el texto a esta escala. */
float prompt_icon_size(float scale);

/* Ancho de la cadena (con íconos o sin ellos, según prompts_enable). */
float prompt_width(const char *s, float scale);

/* Dibuja la cadena con su esquina superior izquierda en (x, y), como
 * rsxkit_ui_text. Los íconos llevan el alfa de `rgba`; con `shadow` van
 * negros (la sombra). Si pasa de max_w (0 = sin límite) se achica la escala.
 * Devuelve el ancho dibujado. */
float prompt_draw(float x, float y, const char *s, float scale, uint32_t rgba, bool shadow, float max_w);

#endif
