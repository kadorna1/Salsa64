/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — idiomas del menú y de los avisos. La clave es el texto en español
 * tal como está en el código; tr() devuelve su traducción al idioma elegido o,
 * si no está en la tabla, el mismo texto. Los textos con %u, %s... se
 * traducen enteros (la traducción lleva los mismos % y en el mismo orden:
 * tests/unit/test_i18n.c lo comprueba). La tabla va ordenada por clave
 * (strcmp) para buscar con bsearch. La tabla vive en tools/i18n/tabla.tsv (una
 * fila por texto, una columna por idioma) y tools/i18n-gen.py la vuelca en
 * i18n_tabla.inc: editar el .tsv, no el .inc. La fuente propia (8x13) tiene
 * ASCII, Latin-1 y las letras del polaco, el turco y el ruso; el japonés, el
 * coreano y el chino necesitan la del sistema (i18n_set_cjk).
 */
#include "i18n.h"

#include <stdlib.h>
#include <string.h>

/* es -> en, fr, de, it, pt, nl, sv, da, no, fi, pl, tr, ru, ja, ko, zh-hant, zh-hans */
struct entry {
	const char *es;
	const char *t[MENU_LANGS_TR];
};

static const struct entry table[] = {
#include "i18n_tabla.inc"
};

static int lang_cur;
static int sys_lang = -1;
static int cjk_ok;

static const char *const codes[] = { "es", "en", "fr", "de", "it", "pt", "nl", "sv", "da",
				     "no", "fi", "pl", "tr", "ru", "ja", "ko", "zh-hant", "zh-hans" };
_Static_assert(sizeof codes / sizeof codes[0] == MENU_LANGS_TR + 1, "un código por idioma");

int i18n_is_cjk(int lang)
{
	return lang >= I18N_JA && lang <= I18N_ZH_HANS;
}

void i18n_set_cjk(int available)
{
	cjk_ok = available != 0;
}

int i18n_cjk(void)
{
	return cjk_ok;
}

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
	case 16: return 11;  /* polaco */
	case 19: return 12;  /* turco */
	case 8:  return 13;  /* ruso */
	case 13: return 7;   /* sueco */
	case 14: return 8;   /* danés */
	case 15: return 9;   /* noruego */
	case 0:  return cjk_ok ? I18N_JA : 1;       /* japonés */
	case 9:  return cjk_ok ? I18N_KO : 1;       /* coreano */
	case 10: return cjk_ok ? I18N_ZH_HANT : 1;  /* chino tradicional */
	case 11: return cjk_ok ? I18N_ZH_HANS : 1;  /* chino simplificado */
	default: return 1;   /* inglés (y el inglés británico) */
	}
}

int i18n_from_system_cjk(int sys)
{
	int saved = cjk_ok;
	cjk_ok = 1;
	int l = i18n_from_system(sys);
	cjk_ok = saved;
	return l;
}

const char *i18n_system_code(int sys)
{
	static const char *const names[20] = { "ja", "en-US", "fr", "es", "de", "it", "nl", "pt-PT", "ru", "ko",
					       "zh-hant", "zh-hans", "fi", "sv", "da", "no", "pl", "pt-BR", "en-GB", "tr" };
	return sys >= 0 && sys < 20 ? names[sys] : "?";
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
	if (i18n_is_cjk(lang) && !cjk_ok)
		return 1;
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
