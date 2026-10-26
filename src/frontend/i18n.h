/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — idiomas. tr("texto en español") da el texto en el idioma elegido
 * (Opciones > Idioma; menu.cfg «idioma = auto|es|en|fr|de|it|pt|nl|sv|da|no|fi»).
 * Lo que no está en la tabla (i18n.c) sale tal cual. Sin dependencias de
 * PSL1GHT: el idioma de la consola lo lee el frontend y se lo pasa a
 * i18n_set_system().
 */
#ifndef SALSA64_FRONTEND_I18N_H
#define SALSA64_FRONTEND_I18N_H

#include <stddef.h>

/* Idiomas con tabla (todos menos el español, que es la clave). */
#define MENU_LANGS_TR 10

/* 0 español, 1 inglés, 2 francés, 3 alemán, 4 italiano, 5 portugués,
 * 6 neerlandés, 7 sueco, 8 danés, 9 noruego, 10 finés (los valores de
 * enum menu_lang en menu.h) y I18N_AUTO: el idioma de la consola. */
#define I18N_AUTO 11
void i18n_set(int lang);
int i18n_lang(void);  /* el idioma en uso, nunca I18N_AUTO */

/* Idioma de la consola (SYSUTIL_LANG_* de PSL1GHT: 0 japonés, 1 inglés EE. UU.,
 * 2 francés, 3 español, 4 alemán, 5 italiano, 6 neerlandés, 7 portugués,
 * 8 ruso, 9 coreano, 10-11 chino, 12 finés, 13 sueco, 14 danés, 15 noruego,
 * 16 polaco, 17 portugués de Brasil, 18 inglés británico, 19 turco). */
int i18n_from_system(int sys_lang);      /* al idioma de la tabla; lo que no hay, inglés */
void i18n_set_system(int sys_lang);      /* -1: no se sabe (host): «Automático» = español */
int i18n_system(void);
int i18n_resolve(int lang);              /* I18N_AUTO -> idioma de la consola; lo demás igual */
const char *i18n_code(int lang);         /* «es», «en»... («auto») para el log */
const char *tr(const char *es);

/* Para las pruebas: la tabla entera. */
size_t i18n_count(void);
const char *i18n_key(size_t i);
const char *i18n_text(size_t i, int lang);

#endif
