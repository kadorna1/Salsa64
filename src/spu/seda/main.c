/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — programa SPU del fondo «Seda» del menú: calcula cada cuadro las
 * cintas de tela (src/frontend/seda_calc.c, el mismo código C del PPE y de la
 * vista previa de la Mac) y los destellos, y escribe por DMA los vértices a
 * la memoria principal mapeada al RSX (src/frontend/seda_spe.h).
 *
 * Trabajador de lib/spukit: cada trabajo es un cuadro. Por cada capa calcula
 * las posiciones de toda la red (las normales de una fila necesitan a las
 * vecinas) y después, fila por fila (24 vértices, 768 bytes), las normales,
 * la proyección y el t0, con doble búfer de salida.
 *
 * Etiquetas DMA: 0 y 1 las filas de salida, 2 los destellos. La 31 es de
 * spukit.
 *
 * Presupuesto de local store (256 KB):
 *   posiciones de una capa 3072 × 16 bytes        48 KB
 *   filas de salida 2 × 24 × 32 bytes             1,5 KB
 *   destellos 192 × 32 bytes                      6 KB
 *   código (seda_calc, spukit, sqrtf) y pila      ~25 KB
 */
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/spu_thread.h>

#include <spukit/spu.h>

#include "frontend/seda_calc.h"
#include "frontend/seda_spe.h"

#define TAG_ROW(b) (b)
#define TAG_SPARK  2u

#define ROW_BYTES ((uint32_t)(SEDA_NV * sizeof(seda_vertex)))
_Static_assert(sizeof(seda_vertex) == 32, "el vértice es de 32 bytes");
_Static_assert(ROW_BYTES % 16 == 0, "la fila es múltiplo de 16 bytes para el DMA");
_Static_assert(sizeof(struct seda_spe_job) <= SPUKIT_DATA_SIZE, "seda_spe_job no cabe en el descriptor de spukit");

static float pos[SEDA_LAYER_VERTS][4] __attribute__((aligned(128)));
static seda_vertex row[2][SEDA_NV] __attribute__((aligned(128)));
static seda_vertex spark[SEDA_SPARK_VERTS] __attribute__((aligned(128)));

static int run(const struct seda_spe_job *j)
{
	if (j->out_ea & 127u)
		return -1;
	uint32_t b = 0;
	for (int l = 0; l < SEDA_LAYERS; l++) {
		seda_positions(l, j->aspect, j->tf, pos);
		uint64_t ea = j->out_ea + (uint64_t)l * SEDA_LAYER_VERTS * sizeof(seda_vertex);
		for (int i = 0; i < SEDA_NU; i++, b ^= 1u) {
			spukit_dma_wait(1u << TAG_ROW(b));  /* lo que salió de este búfer dos filas antes */
			seda_row(l, j->aspect, (const float(*)[4])pos, i, row[b]);
			spukit_dma_put(row[b], ea + (uint64_t)i * ROW_BYTES, ROW_BYTES, TAG_ROW(b));
		}
	}
	seda_sparkles(j->aspect, j->tf, spark);
	spukit_dma_put(spark, j->out_ea + (uint64_t)SEDA_LAYERS * SEDA_LAYER_VERTS * sizeof(seda_vertex),
		       SEDA_SPARK_VERTS * sizeof(seda_vertex), TAG_SPARK);
	spukit_dma_wait(1u << TAG_ROW(0) | 1u << TAG_ROW(1) | 1u << TAG_SPARK);
	return 0;
}

static int handler(void *ctx, const spukit_job *j, spukit_result *res)
{
	(void)ctx;
	(void)res;
	struct seda_spe_job job;
	if (j->kind != SEDA_SPE_JOB)
		return -1;
	memcpy(&job, j->data, sizeof job);
	return run(&job);
}

int main(uint64_t ctrl_ea, uint64_t arg, uint64_t a2, uint64_t a3)
{
	(void)arg, (void)a2, (void)a3;
	spukit_run(ctrl_ea, handler, NULL);
	spu_thread_exit(0);
	return 0;
}
