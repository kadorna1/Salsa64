/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — idiomas del menú y de los avisos. La clave es el texto en español
 * tal como está en el código; tr() devuelve su traducción al idioma elegido o,
 * si no está en la tabla, el mismo texto. Los textos con %u, %s... se
 * traducen enteros (la traducción lleva los mismos % y en el mismo orden:
 * tests/unit/test_i18n.c lo comprueba). La tabla va ordenada por clave
 * (strcmp) para buscar con bsearch. La tabla vive en tools/i18n/tabla.tsv (una
 * fila por texto, una columna por idioma) y tools/i18n-gen.py la vuelca en
 * i18n_tabla.inc: editar el .tsv, no el .inc. La fuente solo tiene ASCII y
 * Latin-1.
 */
#include "i18n.h"

#include <stdlib.h>
#include <string.h>

/* es -> en, fr, de, it, pt, nl, sv, da, no, fi */
struct entry {
	const char *es;
	const char *t[MENU_LANGS_TR];
};

static const struct entry table[] = {
#include "i18n_tabla.inc"
};

static int lang_cur;
static int sys_lang = -1;

static const char *const codes[] = { "es", "en", "fr", "de", "it", "pt", "nl", "sv", "da", "no", "fi" };

int i18n_from_system(int sys)
{
	switch (sys) {
	case 2:  return 2;   /* francés */
	case 3:  return 0;   /* español */
	case 4:  return 3;   /* alemán */
	case 5:  return 4;   /* italiano */
	case 6:  return 6;   /* neerlandés */
	case 7:               /* portugués (Portugal) */
	case 17: return 5;   /* portugués (Brasil): comparten columna */
	case 12: return 10;  /* finés */
	case 13: return 7;   /* sueco */
	case 14: return 8;   /* danés */
	case 15: return 9;   /* noruego */
	default: return 1;   /* inglés (también japonés, coreano, chino, ruso, polaco, turco...) */
	}
}

void i18n_set_system(int sys)
{
	sys_lang = sys;
}

int i18n_system(void)
{
	return sys_lang;
}

int i18n_resolve(int lang)
{
	if (lang == I18N_AUTO)
		return sys_lang < 0 ? 0 : i18n_from_system(sys_lang);
	return lang >= 0 && lang <= MENU_LANGS_TR ? lang : 0;
}

const char *i18n_code(int lang)
{
	return lang == I18N_AUTO ? "auto" : lang >= 0 && lang <= MENU_LANGS_TR ? codes[lang] : codes[0];
}

void i18n_set(int lang)
{
	lang_cur = i18n_resolve(lang);
}

int i18n_lang(void)
{
	return lang_cur;
}

static int cmp(const void *key, const void *el)
{
	return strcmp((const char *)key, ((const struct entry *)el)->es);
}

const char *tr(const char *es)
{
	if (!lang_cur || !es)
		return es;
	const struct entry *e = bsearch(es, table, sizeof table / sizeof table[0], sizeof table[0], cmp);
	return e ? e->t[lang_cur - 1] : es;
}

size_t i18n_count(void)
{
	return sizeof table / sizeof table[0];
}

const char *i18n_key(size_t i)
{
	return i < i18n_count() ? table[i].es : NULL;
}

const char *i18n_text(size_t i, int lang)
{
	return i < i18n_count() && lang >= 1 && lang <= MENU_LANGS_TR ? table[i].t[lang - 1] : NULL;
}
