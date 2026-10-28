/*
 * Salsa64 — pruebas del analizador de íconos de botones (src/frontend/prompts.c).
 */
#include "test.h"

#include <stdio.h>
#include <string.h>

#include "frontend/prompts.h"

/* Los trozos de una cadena: [MARCADOR] por cada ícono y <texto> por cada tramo. */
static void dump(const char *s, char *out, size_t cap)
{
	struct prompt_iter it;
	struct prompt_tok t;
	size_t o = 0;
	out[0] = 0;
	prompt_iter_init(&it, s);
	while (prompt_iter_next(&it, &t)) {
		int n;
		if (t.icon >= 0)
			n = snprintf(out + o, cap - o, "[%s]", prompt_table[t.icon].name);
		else
			n = snprintf(out + o, cap - o, "<%.*s>", (int)t.n, t.s);
		o += (size_t)n;
	}
}

#define DUMP(s) (dump(s, buf, sizeof buf), buf)

static void symbols(void)
{
	char buf[256];
	CHECK(!strcmp(DUMP("×  Abrir   ↑↓  Elegir"), "[X]<  Abrir   >[UPDOWN]<  Elegir>"));
	CHECK(!strcmp(DUMP("□  Trucos   SELECT  Ajustes   △  Opciones"),
		      "[SQ]<  Trucos   >[SELECT]<  Ajustes   >[TRI]<  Opciones>"));
	CHECK(!strcmp(DUMP("×: Sí     ○: No"), "[X]<: Sí     >[O]<: No>"));
	CHECK(!strcmp(DUMP("↑↓  Elegir     ○  Volver     START  Continuar"),
		      "[UPDOWN]<  Elegir     >[O]<  Volver     >[START]<  Continuar>"));
	CHECK(!strcmp(DUMP("START + SELECT en el juego"), "[START]< + >[SELECT]< en el juego>"));
	CHECK(!strcmp(DUMP("SELECT + L2, R1"), "[SELECT]< + >[L2]<, >[R1]"));
	CHECK(!strcmp(DUMP("Nada de botones"), "<Nada de botones>"));
	CHECK(!strcmp(DUMP(""), ""));
}

/* La multiplicación y las palabras que contienen un botón no son íconos. */
static void not_icons(void)
{
	char buf[256];
	CHECK(!strcmp(DUMP("%d× = 1280×720 puntos"), "<%d× = 1280×720 puntos>"));
	CHECK(!strcmp(DUMP("Automática: 4× (hasta 5×)"), "<Automática: 4× (hasta 5×)>"));
	CHECK(!strcmp(DUMP("3×3"), "<3×3>"));
	CHECK(!strcmp(DUMP("STARTED RESELECT PL1 L10 XSTART"), "<STARTED RESELECT PL1 L10 XSTART>"));
	CHECK(!strcmp(DUMP("↑ ↓ solos"), "<↑ ↓ solos>"));
	CHECK(!strcmp(DUMP("Star Soldier start"), "<Star Soldier start>"));
}

static void markers(void)
{
	char buf[256];
	CHECK(!strcmp(DUMP("{X} Abrir {UPDOWN} Elegir"), "[X]< Abrir >[UPDOWN]< Elegir>"));
	CHECK(!strcmp(DUMP("{N64A}{N64B}{N64CUP}"), "[N64A][N64B][N64CUP]"));
	CHECK(!strcmp(DUMP("{NOEXISTE} {X"), "<{NOEXISTE} {X>"));
	CHECK(!strcmp(DUMP("{}"), "<{}>"));
}

/* Sin el atlas, los marcadores y los símbolos se dibujan como texto. */
static void fallback(void)
{
	prompts_enable(false);
	CHECK(!prompts_enabled());
	CHECK(prompt_width("{X} Abrir", 1.0f) == prompt_width("× Abrir", 1.0f));
	CHECK(prompt_width("×  Abrir", 1.0f) > 0.0f);
	CHECK(!strcmp(prompt_fallback(PROMPT_X), "×"));
	CHECK(!strcmp(prompt_fallback(PROMPT_START), "START"));
	CHECK(!strcmp(prompt_fallback(PROMPT_DPAD_UD), "↑↓"));
	/* Con el atlas, un ícono mide lo que prompt_icon_size. */
	prompts_enable(true);
	float w = prompt_width("{X}", 2.0f);
	CHECK(w == prompt_icon_size(2.0f));
	CHECK(prompt_width("{X}{O}", 2.0f) == 2.0f * w);
	prompts_enable(false);
}

/* Todo ícono tiene casilla dentro del atlas y marcador propio. */
static void table(void)
{
	for (int i = 0; i < PROMPT_COUNT; i++) {
		CHECK(prompt_table[i].name && prompt_table[i].name[0]);
		CHECK(prompt_table[i].x + PROMPT_CELL <= PROMPT_ATLAS_W);
		CHECK(prompt_table[i].y + PROMPT_CELL <= PROMPT_ATLAS_H);
		for (int j = 0; j < i; j++)
			CHECK(strcmp(prompt_table[i].name, prompt_table[j].name));
		CHECK(prompt_fallback(i)[0]);
	}
}

void t_suite_prompts(void)
{
	RUN(symbols);
	RUN(not_icons);
	RUN(markers);
	RUN(fallback);
	RUN(table);
}
