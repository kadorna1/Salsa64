/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — arreglos por juego en el código cargado en la RDRAM. Ver
 * gamefix.h.
 *
 * Donkey Kong 64 (USA, NDOE): el juego mide cuántos retrazados del VI pasaron
 * desde el cuadro anterior (contador del planificador en 0x80767CC4, el valor
 * anterior en 0x8076AF10) y lo guarda en 0x80744478; después divide entre ese
 * número en coma flotante con las excepciones de operación inválida y de
 * división entre cero habilitadas en el FCSR. En los mapas de juego el
 * cálculo pone un mínimo de 1; en los de escena cinemática (la bandera de
 * 0x8076AF14 en 0) guarda la resta tal cual. Al cambiar de escena a la mitad
 * de la introducción de una partida nueva (mapa 0xAC), el juego reinicia el
 * contador, y con nuestro CPU los dos primeros cuadros de la escena caben en
 * el mismo retrazado: el segundo mide 0, divide 0 entre 0, el hilo principal
 * se para por la excepción y quedan solo el audio y el VI (pantalla negra con
 * música, issue #51). En la consola, mucho más lenta por instrucción, entre
 * esos dos cuadros siempre pasa al menos un retrazado.
 *
 * El arreglo reescribe las 11 instrucciones del camino de las escenas
 * (0x80600914–0x8060093C, la función que llama el bucle principal antes de
 * esperar el cuadro) para que haga lo mismo que el camino de juego: la resta
 * con mínimo 1. Las direcciones salen del volcado de la RDRAM de nuestra
 * propia emulación; nada de material de Nintendo.
 */
#include "core/gamefix.h"

#include <string.h>

#include "core/bus.h"

struct gamefix {
	const char *name;
	char code[4];       /* encabezado 0x3B–0x3E */
	uint32_t crc1, crc2;  /* encabezado 0x10 y 0x14 */
	uint32_t pa;        /* dirección física del código */
	unsigned n;         /* palabras */
	uint32_t orig[16];
	uint32_t fix[16];
};

static const struct gamefix fixes[] = {
	{
		"Donkey Kong 64 (USA): retrazados entre cuadros de las escenas con mínimo 1",
		{ 'N', 'D', 'O', 'E' }, 0xEC58EABFu, 0xAD7C7169u, 0x00600914u, 11,
		{
			0x3C038076u,  /* lui   v1, 0x8076 */
			0x3C048077u,  /* lui   a0, 0x8077 */
			0x2484AF10u,  /* addiu a0, a0, -0x50F0      a0 = &anterior */
			0x24637A40u,  /* addiu v1, v1, 0x7A40 */
			0x8C6E0284u,  /* lw    t6, 0x284(v1)        contador */
			0x8C8F0000u,  /* lw    t7, 0(a0)            anterior */
			0x3C018074u,  /* lui   at, 0x8074 */
			0x01CFC023u,  /* subu  t8, t6, t7 */
			0xAC384478u,  /* sw    t8, 0x4478(at)       retrazados, puede ser 0 */
			0x8C790284u,  /* lw    t9, 0x284(v1) */
			0xAC990000u,  /* sw    t9, 0(a0)            anterior = contador */
		},
		{
			0x3C038076u,  /* lui   v1, 0x8076 */
			0x3C048077u,  /* lui   a0, 0x8077 */
			0x8C6E7CC4u,  /* lw    t6, 0x7CC4(v1)       contador */
			0x8C8FAF10u,  /* lw    t7, -0x50F0(a0)      anterior */
			0xAC8EAF10u,  /* sw    t6, -0x50F0(a0)      anterior = contador */
			0x01CFC023u,  /* subu  t8, t6, t7 */
			0x2F190001u,  /* sltiu t9, t8, 1 */
			0x0319C021u,  /* addu  t8, t8, t9           mínimo 1 */
			0x3C018074u,  /* lui   at, 0x8074 */
			0xAC384478u,  /* sw    t8, 0x4478(at) */
			0x00000000u,  /* nop */
		},
	},
};

#define NFIXES (sizeof fixes / sizeof fixes[0])

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void salsa64_gamefix_select(struct salsa64_bus *bus)
{
	bus->gamefix = 0;
	if (!bus->rom || bus->rom_resident < 0x40)
		return;
	const uint8_t *h = bus->rom;
	for (unsigned i = 0; i < NFIXES; i++) {
		const struct gamefix *f = &fixes[i];
		if (!memcmp(h + 0x3B, f->code, 4) && rd32(h + 0x10) == f->crc1 && rd32(h + 0x14) == f->crc2) {
			bus->gamefix = (uint8_t)(i + 1);
			return;
		}
	}
}

const char *salsa64_gamefix_name(const struct salsa64_bus *bus)
{
	return bus->gamefix ? fixes[bus->gamefix - 1].name : NULL;
}

void salsa64_gamefix_field(struct salsa64_bus *bus)
{
	if (!bus->gamefix)
		return;
	const struct gamefix *f = &fixes[bus->gamefix - 1];
	if ((uint64_t)f->pa + 4 * f->n > bus->rdram_size)
		return;
	uint8_t *p = bus->rdram + f->pa;
	for (unsigned k = 0; k < f->n; k++)
		if (rd32(p + 4 * k) != f->orig[k])
			return;  /* todavía no está cargado, ya está arreglado o es otro código */
	for (unsigned k = 0; k < f->n; k++) {
		p[4 * k] = (uint8_t)(f->fix[k] >> 24);
		p[4 * k + 1] = (uint8_t)(f->fix[k] >> 16);
		p[4 * k + 2] = (uint8_t)(f->fix[k] >> 8);
		p[4 * k + 3] = (uint8_t)f->fix[k];
	}
	salsa64_bus_rdram_patched(bus, f->pa, 4 * f->n);
	bus->gamefix_applied++;
}
