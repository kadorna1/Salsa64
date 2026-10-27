/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — pila de red de la PS3 compartida (libhttp, libssl) para las
 * descargas (download.c) y RetroAchievements (ra.c). Se levanta con la primera
 * netstack_acquire y se baja con la última netstack_release; la red de bajo
 * nivel (libnet) es del logger UDP y nunca se baja. Un hilo puede hacer
 * peticiones a la vez que otro: cada netstack_request crea su propio cliente.
 *
 * Los certificados de la consola son viejos: con `strict` falso se sigue aunque
 * no se pueda comprobar la cadena (solo para lo que se baja y nunca se
 * ejecuta); con `strict` verdadero (credenciales y tokens), una cadena que no
 * se puede comprobar rechaza la conexión.
 */
#ifndef SALSA64_FRONTEND_NETSTACK_H
#define SALSA64_FRONTEND_NETSTACK_H

#include <stddef.h>
#include <stdint.h>

struct ns_req {
	const char *method;        /* "GET" o "POST" */
	const char *url;
	const char *body;          /* POST: cuerpo (puede ser NULL con body_len 0) */
	size_t body_len;
	const char *content_type;  /* POST: o NULL */
	const char *user_agent;
	size_t cap;                /* tope de la respuesta, en bytes */
	int strict;                /* rechazar cadenas de certificados que no se verifican */
	volatile int *cancel;      /* si apunta a algo distinto de 0, la petición se corta (código -2); o NULL */
};

/* Crea el cerrojo de la pila: una vez, desde un solo hilo (el principal), antes de
 * que nadie pida la red. */
void netstack_init(void);
/* 0 si quedó lista; si no, un código negativo y `*what` dice en qué paso. */
int netstack_acquire(const char **what);
void netstack_release(void);
/* La consola tiene dirección IP (netctl). */
int netstack_online(void);
/* 0 bien (`*buf` con la respuesta más un NUL, a liberar con free; `*len` sin el
 * NUL); si no, -1 y `*code` lleva el estado HTTP (>= 100) o un error: negativo
 * de libhttp, -1 sin memoria, -2 cancelada, -3 más grande de lo permitido. */
int netstack_request(const struct ns_req *r, uint8_t **buf, size_t *len, int *code);

#endif
