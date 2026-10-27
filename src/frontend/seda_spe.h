/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — fondo «Seda»: el trabajo del SPE (src/spu/seda/main.c). Lo
 * comparten seda.c (PPE) y el programa SPU.
 *
 * Un trabajo es un cuadro: el SPE calcula las SEDA_LAYERS cintas y los
 * destellos (src/frontend/seda_calc.c) y escribe por DMA los SEDA_VERTS
 * vértices de 32 bytes, en ese orden (capas y luego destellos), a `out_ea`
 * (alineada a 128), de donde los lee el RSX.
 */
#ifndef SALSA64_SEDA_SPE_H
#define SALSA64_SEDA_SPE_H

#include <stdint.h>

#define SEDA_SPE_JOB 1

struct seda_spe_job {
	uint64_t out_ea;  /* destino de los vértices (alineado a 128) */
	float aspect;     /* ancho / alto de la pantalla */
	float tf;         /* la fase (seda_phase) */
};

#endif
