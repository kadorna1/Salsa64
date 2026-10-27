/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — RetroAchievements en la PS3 (docs/retroachievements.md): el hilo
 * de red (netstack.h), el teclado del sistema para el usuario y la contraseña,
 * ra.cfg (usuario y TOKEN; la contraseña no se guarda nunca) y los avisos. La
 * lógica de rc_client está en ra_core.h.
 *
 * Hilos: el menú y la presentación llaman a ra_init, ra_apply, ra_login_begin,
 * ra_pump y la lista; la emulación, a ra_frame; la red y la huella de la ROM
 * corren en su propio hilo, sin bloquear la emulación.
 */
#ifndef SALSA64_FRONTEND_RA_H
#define SALSA64_FRONTEND_RA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ra_core.h"

/* Al arrancar: lee ra.cfg y crea el cliente. false si no hubo memoria. */
bool ra_init(void);
/* Al cerrar el programa. */
void ra_shutdown(void);

/* Los ajustes del menú (menu.cfg). Con `enabled` y un token guardado, inicia
 * la sesión. Hardcore vale para el siguiente juego que se abra. */
void ra_apply(bool enabled, bool hardcore);
bool ra_enabled(void);

/* La sesión: nombre del usuario, o NULL si no hay (en proceso, tampoco). */
const char *ra_user(void);
bool ra_logging_in(void);
/* Pide usuario y contraseña con el teclado del sistema (oskLoadAsync) e inicia
 * sesión; el menú sigue dibujando. */
void ra_login_begin(void);
/* Cierra la sesión y borra el token de ra.cfg. */
void ra_logout(void);
/* El teclado está abierto. */
bool ra_osk_active(void);
/* Un evento del sistema (sysutil_cb): true si era del teclado. */
bool ra_sysutil_event(uint64_t status, uint64_t param);

/* Un juego abierto: la ROM ya en z64 y la RDRAM emulada. Calcula la huella en
 * el hilo de red y carga los logros; los avisos salen por ra_poll_notice. */
void ra_game_open(const uint8_t *rom, size_t rom_size, const uint8_t *rdram, size_t rdram_size);
void ra_game_close(void);
/* Una vez por campo emulado (hilo de emulación). */
void ra_frame(void);
/* Hay un juego con logros y Hardcore: nada de estados ni trucos. */
bool ra_hardcore_active(void);
bool ra_game_loaded(void);

/* Del hilo de presentación (o del menú), cada vuelta: guarda el token nuevo,
 * mantiene el trabajo periódico en la pausa (`paused`) y escribe el costo de
 * ra_frame al registro de vez en cuando. */
void ra_pump(bool paused);
/* El siguiente aviso para la pantalla (ya traducido); false si no hay. */
bool ra_poll_notice(char *buf, size_t size);

/* La lista de logros para la pausa: ra_list_refresh() la copia del cliente. */
unsigned ra_list_refresh(void);
unsigned ra_list_count(void);
const struct ra_item *ra_list_get(unsigned i);
/* «Título del juego», o "" . */
const char *ra_game_title(void);
unsigned ra_list_unlocked(void);

#endif
