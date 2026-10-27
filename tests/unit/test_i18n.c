/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Pruebas de los idiomas (src/frontend/i18n.c): la tabla va ordenada, cada
 * traducción tiene los mismos % que el original, solo usa caracteres que la
 * fuente del menú sabe dibujar y cabe donde cabía el español.
 */
#include <stdio.h>
#include <string.h>

#include <rsxkit/text.h>
#include "../../lib/rsxkit/src/font.h"

#include "frontend/i18n.h"
#include "test.h"

static void sorted(void)
{
	CHECK(i18n_count() > 100);
	for (size_t i = 1; i < i18n_count(); i++) {
		if (strcmp(i18n_key(i - 1), i18n_key(i)) >= 0) {
			printf("   fuera de orden o repetida: «%s» / «%s»\n", i18n_key(i - 1), i18n_key(i));
			CHECK(0);
		}
	}
}

/* Los % de un texto, en orden («%u», «%s», «%ux%u»...), sin los «%%». */
static void formats(const char *s, char *out, size_t size)
{
	size_t o = 0;
	for (; *s; s++) {
		if (*s != '%')
			continue;
		if (s[1] == '%') {
			s++;
			continue;
		}
		const char *e = s + 1;
		while (*e && strchr("-+ #0123456789.l", *e))
			e++;
		if (*e && o + (size_t)(e - s) + 2 < size) {
			memcpy(out + o, s, (size_t)(e - s) + 1);
			o += (size_t)(e - s) + 1;
			out[o++] = ';';
		}
		s = *e ? e : e - 1;
	}
	out[o] = '\0';
}

static void same_formats(void)
{
	for (size_t i = 0; i < i18n_count(); i++) {
		char want[64];
		formats(i18n_key(i), want, sizeof want);
		for (int l = 1; l <= MENU_LANGS_TR; l++) {
			char got[64];
			const char *t = i18n_text(i, l);
			CHECK(t != NULL && t[0]);
			formats(t ? t : "", got, sizeof got);
			if (strcmp(want, got)) {
				printf("   «%s» idioma %d: «%s» (%s / %s)\n", i18n_key(i), l, t, want, got);
				CHECK(0);
			}
		}
	}
}

static void glyphs(void)
{
	/* Los íconos de filas que el menú dibuja con la fuente y las letras propias
	 * del polaco, el turco y el ruso. */
	const char *icons = "~□▶─A↓■▲●#?◀►ĄąĆćĘęŁłŃńŚśŹźŻżĞğİıŞşÇçÖöÜüАЯабяёЁ";
	for (const char *p = icons; *p;) {
		uint32_t cp = rsxkit_utf8_next(&p);
		if (!rsxkit_font_has(cp)) {
			printf("   sin glifo U+%04X\n", cp);
			CHECK(0);
		}
	}
	for (size_t i = 0; i < i18n_count(); i++) {
		for (int l = 0; l <= MENU_LANGS_TR; l++) {
			const char *t = l ? i18n_text(i, l) : i18n_key(i);
			for (const char *p = t; *p;) {
				uint32_t cp = rsxkit_utf8_next(&p);
				if (cp >= 0x20 && !rsxkit_font_has(cp)) {
					printf("   «%s» idioma %d: sin glifo U+%04X\n", t, l, cp);
					CHECK(0);
				}
			}
		}
	}
}

/* Una traducción no debe ser mucho más larga que el español: donde cabía el
 * español (líneas de 30 celdas del panel, pie de 66) debe caber ella. */
static void widths(void)
{
	for (size_t i = 0; i < i18n_count(); i++) {
		uint32_t es = rsxkit_text_cells(i18n_key(i));
		for (int l = 1; l <= MENU_LANGS_TR; l++) {
			uint32_t n = rsxkit_text_cells(i18n_text(i, l));
			uint32_t limit = es <= 31 ? 34 : es <= 66 ? 66 : es + 8;
			if (n > limit) {
				printf("   «%s» idioma %d: %u celdas (límite %u)\n", i18n_key(i), l, n, limit);
				CHECK(0);
			}
		}
	}
}

/* Los rótulos de los datos de la ROM caben en su columna (9 celdas a 1,5×). */
static void labels(void)
{
	static const char *const keys[] = { "Archivo", "Ubicación", "Código", "Tamaño", "Región", "Guardado" };
	for (unsigned k = 0; k < sizeof keys / sizeof keys[0]; k++) {
		i18n_set(0);
		for (int l = 1; l <= MENU_LANGS_TR; l++) {
			i18n_set(l);
			uint32_t n = rsxkit_text_cells(tr(keys[k]));
			if (n > 9) {
				printf("   rótulo «%s» idioma %d: %u celdas\n", tr(keys[k]), l, n);
				CHECK(0);
			}
		}
	}
	i18n_set(0);
}

static void lookup(void)
{
	static const char *const accessory_keys[] = {
		"Accesorio inicial de los cuatro", "controles de N64.", "Controller Pak guarda datos.",
		"Rumble Pak activa la vibración.", "Transfer Pak carga .gb/.gbc y usa .sav.",
		"Controller Pak, Rumble Pak, Transfer Pak o ninguno", "Volver al menú",
		"×  Cambiar      ↑↓  Elegir      ○  Volver",
	};
	i18n_set(0);
	CHECK(!strcmp(tr("Volver"), "Volver"));
	i18n_set(1);
	CHECK(!strcmp(tr("Volver"), "Back"));
	CHECK(!strcmp(tr("Sí"), "Yes"));
	for (unsigned i = 0; i < sizeof accessory_keys / sizeof accessory_keys[0]; i++)
		CHECK(strcmp(tr(accessory_keys[i]), accessory_keys[i]) != 0);
	i18n_set(2);
	CHECK(!strcmp(tr("Volver"), "Retour"));
	i18n_set(3);
	CHECK(!strcmp(tr("Volver"), "Zurück"));
	i18n_set(4);
	CHECK(!strcmp(tr("Volver"), "Indietro"));
	i18n_set(5);
	CHECK(!strcmp(tr("Volver"), "Voltar"));
	i18n_set(6);
	CHECK(!strcmp(tr("Volver"), "Terug"));
	i18n_set(7);
	CHECK(!strcmp(tr("Volver"), "Tillbaka"));
	i18n_set(8);
	CHECK(!strcmp(tr("Volver"), "Tilbage"));
	i18n_set(9);
	CHECK(!strcmp(tr("Volver"), "Tilbake"));
	i18n_set(10);
	CHECK(!strcmp(tr("Volver"), "Takaisin"));
	i18n_set(11);
	CHECK(!strcmp(tr("Volver"), "Wróć"));
	i18n_set(12);
	CHECK(!strcmp(tr("Volver"), "Geri"));
	i18n_set(13);
	CHECK(!strcmp(tr("Volver"), "Назад"));
	/* Lo que no está sale igual (títulos de juegos, rutas...). */
	CHECK(!strcmp(tr("GoldenEye 007"), "GoldenEye 007"));
	CHECK(tr("Rareza") != NULL);
	i18n_set(40);  /* fuera de rango: español */
	CHECK_EQ(i18n_lang(), 0);
	CHECK(!strcmp(tr("Volver"), "Volver"));
	CHECK(tr(NULL) == NULL);
	i18n_set(0);
}

/* Los nombres de los accesorios no se traducen en ningún idioma. */
static void untranslated_names(void)
{
	static const char *const keep[] = { "Controller Pak", "Rumble Pak", "Transfer Pak" };
	for (unsigned k = 0; k < sizeof keep / sizeof keep[0]; k++)
		for (size_t i = 0; i < i18n_count(); i++)
			if (!strcmp(i18n_key(i), keep[k]))
				for (int l = 1; l <= MENU_LANGS_TR; l++)
					CHECK(!strcmp(i18n_text(i, l), keep[k]));
	/* Y en toda la tabla, el nombre propio aparece tal cual si aparece. */
	for (size_t i = 0; i < i18n_count(); i++)
		for (unsigned k = 0; k < sizeof keep / sizeof keep[0]; k++)
			if (strstr(i18n_key(i), keep[k]))
				for (int l = 1; l <= MENU_LANGS_TR; l++)
					if (!strstr(i18n_text(i, l), keep[k])) {
						printf("   «%s» idioma %d perdió «%s»\n", i18n_key(i), l, keep[k]);
						CHECK(0);
					}
}

/* «Automático»: el idioma de la consola, y el mapeo de los 20 que tiene la PS3. */
static void automatic(void)
{
	/* SYSUTIL_LANG_*: 0 ja, 1 en-US, 2 fr, 3 es, 4 de, 5 it, 6 nl, 7 pt-PT, 8 ru, 9 ko, 10 zh-T, 11 zh-S,
	 * 12 fi, 13 sv, 14 da, 15 no, 16 pl, 17 pt-BR, 18 en-GB, 19 tr. */
	static const int want[20] = { 1, 1, 2, 0, 3, 4, 6, 5, 13, 1, 1, 1, 10, 7, 8, 9, 11, 5, 1, 12 };
	for (int sys = 0; sys < 20; sys++) {
		if (i18n_from_system(sys) != want[sys]) {
			printf("   idioma de la consola %d: %d (esperado %d)\n", sys, i18n_from_system(sys), want[sys]);
			CHECK(0);
		}
	}
	CHECK_EQ(i18n_from_system(-1), 1);
	CHECK_EQ(i18n_from_system(99), 1);

	/* Sin saber el de la consola (host, pruebas): español. */
	i18n_set_system(-1);
	i18n_set(I18N_AUTO);
	CHECK_EQ(i18n_lang(), 0);
	CHECK(!strcmp(tr("Volver"), "Volver"));
	CHECK_EQ(i18n_resolve(I18N_AUTO), 0);

	/* Con el de la consola. */
	i18n_set_system(13);  /* sueco */
	i18n_set(I18N_AUTO);
	CHECK_EQ(i18n_lang(), 7);
	CHECK(!strcmp(tr("Volver"), "Tillbaka"));
	i18n_set_system(8);  /* ruso */
	i18n_set(I18N_AUTO);
	CHECK_EQ(i18n_lang(), 13);
	CHECK(!strcmp(tr("Volver"), "Назад"));
	i18n_set_system(16);  /* polaco */
	i18n_set(I18N_AUTO);
	CHECK_EQ(i18n_lang(), 11);
	i18n_set_system(19);  /* turco */
	i18n_set(I18N_AUTO);
	CHECK_EQ(i18n_lang(), 12);
	i18n_set_system(9);  /* coreano: inglés */
	i18n_set(I18N_AUTO);
	CHECK_EQ(i18n_lang(), 1);
	CHECK(!strcmp(tr("Volver"), "Back"));
	i18n_set_system(17);  /* portugués de Brasil: la columna del portugués */
	i18n_set(I18N_AUTO);
	CHECK_EQ(i18n_lang(), 5);

	/* Una elección explícita no depende de la consola. */
	i18n_set(MENU_LANGS_TR > 1 ? 2 : 0);
	CHECK_EQ(i18n_lang(), 2);
	CHECK_EQ(i18n_resolve(3), 3);
	CHECK(!strcmp(i18n_code(I18N_AUTO), "auto"));
	CHECK(!strcmp(i18n_code(10), "fi"));
	CHECK(!strcmp(i18n_code(13), "ru"));

	i18n_set_system(-1);
	i18n_set(0);
}

void t_suite_i18n(void)
{
	RUN(untranslated_names);
	RUN(automatic);
	RUN(sorted);
	RUN(same_formats);
	RUN(glyphs);
	RUN(widths);
	RUN(labels);
	RUN(lookup);
}
