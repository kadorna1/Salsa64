/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Pruebas de la escala automática con series sintéticas. */
#include "test.h"

#include "frontend/scale_auto.h"

#define PERIOD_NS 16666667u

static struct scale_auto_result second(struct scale_auto *p, unsigned fields, unsigned presented, unsigned skipped,
				       unsigned rsx_pm)
{
	struct scale_auto_sample s = {
		1000000, (uint64_t)fields * PERIOD_NS * rsx_pm / 1000u, PERIOD_NS, fields, presented, skipped,
	};
	return scale_auto_update(p, &s);
}

static void snap_5x_baja_y_se_queda(void)
{
	struct scale_auto p;
	scale_auto_init(&p, true, 5, 5);
	/* Serie de la decisión en la PS3: VI 92,7 %, RSX 107,8 %, sin
	 * presentaciones y 56 saltos en la última ventana. */
	CHECK_EQ(second(&p, 56, 0, 56, 1078).action, SCALE_AUTO_NONE);
	CHECK_EQ(second(&p, 56, 0, 56, 1078).action, SCALE_AUTO_NONE);
	struct scale_auto_result r = second(&p, 56, 0, 56, 1078);
	CHECK_EQ(r.action, SCALE_AUTO_DOWN);
	CHECK_EQ(p.current, 4);
	CHECK_EQ(scale_auto_ceiling(&p), 4);
	for (int i = 0; i < 120; i++)
		CHECK_EQ(second(&p, 60, 60, 0, i & 1 ? 312 : 444).action, SCALE_AUTO_NONE);
	CHECK_EQ(p.current, 4);
}

static void snap_reabre_en_la_estable(void)
{
	struct scale_auto p;
	scale_auto_init(&p, true, 4, 5);
	CHECK_EQ(p.current, 4);
	CHECK(p.failed);
	CHECK_EQ(scale_auto_ceiling(&p), 4);
	for (int i = 0; i < 120; i++)
		CHECK_EQ(second(&p, 60, 60, 0, 400).action, SCALE_AUTO_NONE);
	CHECK_EQ(p.current, 4);
	CHECK_EQ(scale_auto_ceiling(&p), 4);
}

static void shadows_lento_por_cpu_no_baja(void)
{
	struct scale_auto p;
	scale_auto_init(&p, true, 5, 5);
	for (int i = 0; i < 20; i++)
		CHECK_EQ(second(&p, 36, 30, 6, 1250).action, SCALE_AUTO_NONE);
	CHECK_EQ(p.current, 5);
}

static void tiron_de_carga_no_baja(void)
{
	struct scale_auto p;
	scale_auto_init(&p, true, 5, 5);
	CHECK_EQ(second(&p, 60, 30, 30, 1400).action, SCALE_AUTO_NONE);
	CHECK_EQ(second(&p, 60, 35, 25, 1250).action, SCALE_AUTO_NONE);
	CHECK_EQ(second(&p, 60, 60, 0, 500).action, SCALE_AUTO_NONE);
	CHECK_EQ(p.current, 5);
}

static void margen_sostenido_no_sube(void)
{
	struct scale_auto p;
	scale_auto_init(&p, true, 4, 5);
	for (int i = 0; i < 120; i++)
		CHECK_EQ(second(&p, 60, 60, 0, 500).action, SCALE_AUTO_NONE);
	CHECK_EQ(p.current, 4);
}

static void escala_fallida_no_se_reintenta(void)
{
	struct scale_auto p;
	scale_auto_init(&p, true, 5, 5);
	for (int i = 0; i < 3; i++)
		second(&p, 60, 35, 25, 1200);
	CHECK_EQ(p.current, 4);
	CHECK_EQ(scale_auto_ceiling(&p), 4);
	for (int i = 0; i < 120; i++)
		second(&p, 60, 60, 0, 200);
	CHECK_EQ(p.current, 4);
}

static void escala_fija_avisa_sin_cambiar(void)
{
	struct scale_auto p;
	scale_auto_init(&p, false, 5, 5);
	for (int i = 0; i < 2; i++)
		CHECK_EQ(second(&p, 60, 35, 25, 1200).action, SCALE_AUTO_NONE);
	CHECK_EQ(second(&p, 60, 35, 25, 1200).action, SCALE_AUTO_WARN);
	CHECK_EQ(p.current, 5);
}

void t_suite_scale_auto(void)
{
	RUN(snap_5x_baja_y_se_queda);
	RUN(snap_reabre_en_la_estable);
	RUN(shadows_lento_por_cpu_no_baja);
	RUN(tiron_de_carga_no_baja);
	RUN(margen_sostenido_no_sube);
	RUN(escala_fallida_no_se_reintenta);
	RUN(escala_fija_avisa_sin_cambiar);
}
