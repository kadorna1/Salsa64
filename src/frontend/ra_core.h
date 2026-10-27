/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — RetroAchievements, parte portable (rcheevos rc_client; ver
 * docs/retroachievements.md). Sin PSL1GHT: la red, los hilos, el teclado y los
 * avisos los pone quien llama (ra.c en la PS3, tests/unit/test_ra.c en el
 * host con un servidor simulado).
 *
 * Hilos: todas las llamadas a rc_client van bajo `ops.lock` (recursivo). Las
 * respuestas del servidor se entregan con ra_core_complete desde cualquier
 * hilo (la entrega corre bajo ese cerrojo); ra_core_frame va en el hilo de
 * emulación, una vez por campo.
 *
 * Memoria (N64, RC_CONSOLE_NINTENDO_64): RetroAchievements numera la RDRAM
 * desde 0 y la espera como la ve un anfitrión little-endian (Project64,
 * mupen64plus): palabras de 32 bits guardadas al revés. Como la RDRAM
 * emulada es big-endian, el byte `a` de RetroAchievements es el byte `a ^ 3`
 * de la nuestra (ra_mem_read).
 */
#ifndef SALSA64_FRONTEND_RA_CORE_H
#define SALSA64_FRONTEND_RA_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RA_USER_MAX   64
#define RA_TOKEN_MAX  80
#define RA_TITLE_MAX  64
#define RA_ITEM_TITLE 56
#define RA_ITEM_DESC  72
#define RA_ITEMS_MAX  400

/* Qué se lee de la RDRAM emulada (big-endian) como la espera RetroAchievements.
 * Devuelve los bytes leídos; 0 si el rango se sale de `size`. */
uint32_t ra_mem_read(const uint8_t *rdram, uint32_t size, uint32_t address, uint8_t *buf, uint32_t n);

/* La huella de RetroAchievements para una ROM de N64 en memoria (rc_hash:
 * MD5 del z64; v64 y n64 se normalizan). `hash` lleva 32 caracteres hex y
 * un NUL. false si no es una ROM de N64. */
bool ra_rom_hash(const uint8_t *rom, size_t size, char hash[33]);

struct ra_ops {
	void *ctx;
	/* Encola una petición HTTP (POST si `post` no es NULL). La respuesta llega
	 * con ra_core_complete(id, …), desde cualquier hilo, también dentro de esta
	 * llamada. `url` y `post` valen solo durante la llamada. */
	void (*send)(void *ctx, unsigned id, const char *url, const char *post, const char *content_type);
	void (*lock)(void *ctx);    /* recursivos; pueden ser NULL (un solo hilo) */
	void (*unlock)(void *ctx);
	bool (*trylock)(void *ctx);  /* o NULL: ra_core_frame espera el cerrojo; con él, salta el campo si está ocupado */
	void (*notice)(void *ctx, const char *text);  /* aviso para la pantalla (ya traducido) */
	void (*log)(void *ctx, const char *text);     /* línea de registro (nunca lleva datos de la sesión) */
	uint64_t (*now_us)(void *ctx);                /* microsegundos monótonos */
};

struct ra_core;
enum ra_login { RA_LOGGED_OUT, RA_LOGGING_IN, RA_LOGGED_IN };
enum ra_game { RA_GAME_NONE, RA_GAME_LOADING, RA_GAME_LOADED, RA_GAME_UNKNOWN, RA_GAME_FAILED };

/* Crea el cliente (rc_client). Lo que apunta `ops` debe seguir vivo. */
struct ra_core *ra_core_create(const struct ra_ops *ops);
void ra_core_destroy(struct ra_core *c);
/* Una base distinta de https://retroachievements.org (ra.cfg, «servidor»). */
void ra_core_set_host(struct ra_core *c, const char *url);
/* «rcheevos/12.5» para el User-Agent. */
const char *ra_core_agent_clause(struct ra_core *c);

/* La RDRAM emulada, para las lecturas de los logros. */
void ra_core_set_memory(struct ra_core *c, const uint8_t *rdram, uint32_t size);
/* Hardcore: antes de abrir el juego. */
void ra_core_set_hardcore(struct ra_core *c, bool on);
bool ra_core_hardcore(struct ra_core *c);
/* Hay un juego con logros cargado y Hardcore pedido: los estados y los trucos
 * se desactivan. */
bool ra_core_hardcore_active(struct ra_core *c);

/* Sesión. El resultado llega después: ra_core_login_state, y un aviso. */
void ra_core_login_password(struct ra_core *c, const char *user, const char *pass);
void ra_core_login_token(struct ra_core *c, const char *user, const char *token);
void ra_core_logout(struct ra_core *c);
int ra_core_login_state(struct ra_core *c);
/* Usuario y token de la sesión abierta (el token, para guardarlo); copian. */
bool ra_core_user(struct ra_core *c, char *user, size_t usize, char *token, size_t tsize);
/* Se pone en 1 cuando el inicio de sesión con contraseña termina bien (ra.c guarda el token). */
bool ra_core_take_new_token(struct ra_core *c);

/* Juego: con la huella; el resultado llega después (ra_core_game_state). */
void ra_core_load_game(struct ra_core *c, const char *hash);
void ra_core_unload_game(struct ra_core *c);
int ra_core_game_state(struct ra_core *c);
bool ra_core_summary(struct ra_core *c, char *title, size_t tsize, unsigned *total, unsigned *unlocked,
		     unsigned *points, unsigned *points_unlocked);

struct ra_item {
	char title[RA_ITEM_TITLE];
	char desc[RA_ITEM_DESC];
	unsigned points;
	bool unlocked;
};
/* Copia los logros del juego (los conseguidos al final de su grupo). Devuelve cuántos. */
unsigned ra_core_items(struct ra_core *c, struct ra_item *out, unsigned max);

/* Una vez por campo emulado, en el hilo de emulación. Mide su costo
 * (ra_core_cost). */
void ra_core_frame(struct ra_core *c);
/* Entre campos (pausa): el trabajo periódico de rc_client. */
void ra_core_idle(struct ra_core *c);
/* Costo medio por llamada de ra_core_frame desde la última lectura (µs) y
 * cuántas llamadas; reinicia la cuenta. */
double ra_core_cost(struct ra_core *c, unsigned *calls, double *worst_us);
/* Campos que ra_core_frame saltó por tener el cerrojo ocupado (otro hilo entregando una respuesta). */
unsigned ra_core_skipped(struct ra_core *c);

/* La respuesta de la petición `id`. status = código HTTP, o <= 0 si no hubo
 * respuesta (rc_client reintenta o avisa). */
void ra_core_complete(struct ra_core *c, unsigned id, int status, const char *body, size_t len);

#endif
