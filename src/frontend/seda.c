/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — fondo «Seda» del menú. Ver seda.h, seda_calc.h y
 * docs/fondo-seda.md.
 *
 * Cada cuadro:
 *   1. un SPE (o el PPE, con el archivo `seda-ppe` en SALSA64_DATA_DIR)
 *      calcula los vértices del cuadro siguiente en uno de 3 búferes de
 *      memoria principal mapeados al RSX, y la CPU escribe las rampas de
 *      color (16 texeles) de ese búfer;
 *   2. el RSX dibuja el cuadro anterior: el degradado de la hora, las tres
 *      cintas con mezcla aditiva y los destellos.
 * Los búferes rotan de a tres: antes de reescribir uno se espera la barrera
 * de su último dibujo (casi siempre ya pasó).
 */
#include "frontend/seda.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/memory.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>

#include <rsxkit/post.h>
#include <spukit/spukit.h>

#include "frontend/seda_calc.h"
#include "frontend/seda_spe.h"
#include "platform/ps3/log.h"
#include "platform/ps3/paths.h"

#include "seda_cinta_fpo.h"
#include "seda_destello_fpo.h"
#include "seda_fondo_fpo.h"
#include "spu_seda_bin.h"

enum { P_FONDO, P_CINTA, P_DESTELLO, PROGRAMS };
static const u8 *const program_fpo[PROGRAMS] = { seda_fondo_fpo, seda_cinta_fpo, seda_destello_fpo };

#define BUFS 3
#define ALIGN128(x) (((x) + 127u) & ~127u)
#define LAYER_BYTES ((uint32_t)(SEDA_LAYER_VERTS * sizeof(seda_vertex)))
#define VERT_BYTES  ALIGN128((uint32_t)(SEDA_VERTS * sizeof(seda_vertex)))
#define OFF_VERT 0u
#define OFF_IDX  (OFF_VERT + BUFS * VERT_BYTES)
#define OFF_SIDX (OFF_IDX + ALIGN128(SEDA_INDICES * 2u))
#define OFF_RAMP (OFF_SIDX + ALIGN128(SEDA_SPARK_INDICES * 2u))
#define RAMPS    (SEDA_LAYERS + 1)  /* una por capa y una de los destellos */
#define RAMP_N   16u
#define RAMP_BYTES 128u
#define MEM_USED (OFF_RAMP + BUFS * RAMPS * RAMP_BYTES)
#define MEM_BYTES ((MEM_USED + (1u << 20) - 1) & ~((1u << 20) - 1))

/* Marcas de tiempo del RSX (las de los filtros van de 0 a 31). */
#define STAMP_BASE 56

static struct {
	bool failed;     /* no se pudo: ondas hasta seda_release */
	bool ready;
	bool use_spe;
	uint32_t w, h;
	uint8_t *mem;
	uint32_t off;
	bool mapped;
	rsxkit_program *prog[PROGRAMS];
	spukit_worker *spe;
	uint32_t seq[BUFS];
	bool pending[BUFS];
	uint32_t fence[BUFS];
	bool stamped[BUFS];
	uint32_t frame;
	/* Medición desde el último registro. */
	double rsx_ms, spe_ms;
	uint32_t rsx_n, spe_n, cpu_n;
	uint64_t cpu_us, log_us, setup_us;
	int64_t tz_s;
} sd;

static float scratch[SEDA_LAYER_VERTS][4];  /* el camino del PPE */

static void release(void)
{
	if (sd.ready || sd.mapped)
		rsxkit_finish();
	if (sd.spe)
		spukit_worker_stop(sd.spe);
	for (int i = 0; i < PROGRAMS; i++)
		rsxkit_program_destroy(sd.prog[i]);
	if (sd.mapped)
		rsxkit_unmap_main(sd.mem);
	if (sd.mem)
		sysMemoryFree((sys_mem_addr_t)(uintptr_t)sd.mem);
	memset(&sd, 0, sizeof sd);
}

void seda_release(void)
{
	if (sd.ready || sd.mem)
		salsa64_log("fondo seda: liberado");
	release();
}

static bool setup(uint32_t w, uint32_t h)
{
	uint64_t t0 = sysGetSystemTime();
	release();
	sys_mem_addr_t addr = 0;
	if (sysMemoryAllocate(MEM_BYTES, SYS_MEMORY_PAGE_SIZE_1M, &addr) != 0) {
		salsa64_log("fondo seda: sin %u MB de memoria principal", MEM_BYTES >> 20);
		return false;
	}
	sd.mem = (uint8_t *)(uintptr_t)addr;
	if (rsxkit_map_main(sd.mem, MEM_BYTES, &sd.off) != RSXKIT_OK) {
		salsa64_log("fondo seda: no se pudo mapear la memoria para el RSX");
		return false;
	}
	sd.mapped = true;
	sd.w = w, sd.h = h;
	for (int i = 0; i < PROGRAMS; i++)
		if (!(sd.prog[i] = rsxkit_program_create(program_fpo[i]))) {
			salsa64_log("fondo seda: no se pudo crear el programa %d", i);
			return false;
		}
	memset(sd.mem, 0, MEM_BYTES);
	seda_indices((uint16_t *)(sd.mem + OFF_IDX));
	seda_spark_indices((uint16_t *)(sd.mem + OFF_SIDX));

	/* El SPE es el camino principal; el archivo `seda-ppe` fuerza el del PPE
	 * (para comparar), y si el SPE no arranca se usa el PPE. */
	FILE *pf = fopen(SALSA64_DATA_DIR "/seda-ppe", "r");
	bool force_ppe = pf != NULL;
	if (pf)
		fclose(pf);
	if (!force_ppe) {
		spukit_init(6);
		sd.spe = spukit_worker_start(spu_seda_bin, "salsa64_seda", 0);
		if (!sd.spe)
			salsa64_log("fondo seda: no arrancó el SPE (%d): los vértices van en el PPE", spukit_last_error());
	}
	sd.use_spe = sd.spe != NULL;

	s32 tz = 0, summer = 0;
	sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, &tz);
	sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME, &summer);
	sd.tz_s = (int64_t)tz * 60 + (summer ? 3600 : 0);
	sd.ready = true;
	sd.setup_us = sysGetSystemTime() - t0;
	salsa64_log("fondo seda: %ux%u, %u MB de memoria principal, vértices en el %s, %u + %u + %u instrucciones "
		    "(degradado, cinta, destello), listo en %.1f ms",
		    w, h, MEM_BYTES >> 20, sd.use_spe ? "SPE" : "PPE", rsxkit_program_instructions(sd.prog[P_FONDO]),
		    rsxkit_program_instructions(sd.prog[P_CINTA]), rsxkit_program_instructions(sd.prog[P_DESTELLO]),
		    sd.setup_us / 1000.0);
	return true;
}

/* Segundos desde la medianoche en la hora local de la consola. */
static double day_seconds(void)
{
	u64 sec = 0, nsec = 0;
	sysGetCurrentTime(&sec, &nsec);
	int64_t d = ((int64_t)sec + sd.tz_s) % 86400;
	if (d < 0)
		d += 86400;
	return (double)d + nsec * 1e-9;
}

static uint8_t to8(float v)
{
	return (uint8_t)((v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v) * 255.0f + 0.5f);
}

/* Las rampas de color del búfer b: 16 texeles RGBA8 del tinte A al B,
 * por la ganancia de cada capa; la de los destellos, más blanca y con su brillo. */
static void write_ramps(int b, const struct seda_colors *c)
{
	for (int r = 0; r < RAMPS; r++) {
		uint8_t *p = sd.mem + OFF_RAMP + (uint32_t)(b * RAMPS + r) * RAMP_BYTES;
		float g = r < SEDA_LAYERS ? seda_layer_gain(r) : 0.9f * 1.513f * c->stars + 0.25f;
		for (uint32_t i = 0; i < RAMP_N; i++) {
			float t = (float)i / (float)(RAMP_N - 1);
			for (int k = 0; k < 3; k++) {
				float v = c->tint_a[k] + (c->tint_b[k] - c->tint_a[k]) * t;
				if (r == SEDA_LAYERS)
					v = 0.5f + 0.5f * v;
				p[i * 4 + (uint32_t)k] = to8(v * g);
			}
			p[i * 4 + 3] = 255;
		}
	}
}

static rsxkit_texture ramp_texture(int b, int r)
{
	rsxkit_texture t;
	memset(&t, 0, sizeof t);
	t.offset = sd.off + OFF_RAMP + (uint32_t)(b * RAMPS + r) * RAMP_BYTES;
	t.pitch = RAMP_BYTES / 2;  /* 16 texeles de 4 bytes: 64 */
	t.width = RAMP_N;
	t.height = 1;
	t.location = RSXKIT_MEM_MAIN;
	t.format = RSXKIT_RGBA8888;
	return t;
}

/* Lo que mide un cuadro ya terminado. */
static void collect(int b)
{
	if (!sd.stamped[b])
		return;
	sd.stamped[b] = false;
	uint64_t a = rsxkit_stamp_ns((uint32_t)(STAMP_BASE + b * 2)), e = rsxkit_stamp_ns((uint32_t)(STAMP_BASE + b * 2 + 1));
	if (e <= a || e - a > 1000000000ull)
		return;
	sd.rsx_ms += (e - a) / 1e6;
	sd.rsx_n++;
}

static void report(uint64_t now)
{
	if (!sd.log_us)
		sd.log_us = now;
	if (now - sd.log_us < 5000000u || !sd.rsx_n)
		return;
	salsa64_log("fondo seda: %.2f ms del RSX por cuadro, %.2f ms del SPE, %.2f ms del PPE, %u cuadros",
		    sd.rsx_ms / sd.rsx_n, sd.spe_n ? sd.spe_ms / sd.spe_n : 0.0, sd.cpu_n ? sd.cpu_us / 1000.0 / sd.cpu_n : 0.0,
		    sd.rsx_n);
	sd.rsx_ms = sd.spe_ms = 0.0;
	sd.rsx_n = sd.spe_n = sd.cpu_n = 0;
	sd.cpu_us = 0;
	sd.log_us = now;
}

static rsxkit_post_input input(const rsxkit_texture *t)
{
	rsxkit_post_input in = rsxkit_post_texture_input(t, RSXKIT_FILTER_LINEAR, RSXKIT_POST_CLAMP);
	in.texels = 0;
	return in;
}

static bool draw_buffer(int b, const struct seda_colors *col)
{
	rsxkit_post_pass p;
	rsxkit_stamp((uint32_t)(STAMP_BASE + b * 2));

	/* El degradado de la hora, el resplandor y la viñeta. */
	memset(&p, 0, sizeof p);
	p.program = sd.prog[P_FONDO];
	p.out = NULL;
	p.rect = (rsxkit_rect){ 0, 0, sd.w, sd.h };
	p.step[0] = p.step[1] = 1.0f;
	memcpy(p.k[0], col->top, sizeof col->top);
	memcpy(p.k[1], col->bottom, sizeof col->bottom);
	for (int k = 0; k < 3; k++)
		p.k[2][k] = 0.433f * col->glow[k];
	p.k[3][0] = 0.60f, p.k[3][1] = 0.48f, p.k[3][2] = (float)sd.w / (float)sd.h, p.k[3][3] = 6.0f;
	int rc = rsxkit_post_run(&p);
	if (rc != RSXKIT_OK) {
		salsa64_log("fondo seda: el degradado falló (%d)", rc);
		return false;
	}

	/* Las capas y los destellos, sumados. */
	for (int l = 0; l <= SEDA_LAYERS; l++) {
		bool spark = l == SEDA_LAYERS;
		rsxkit_texture ramp = ramp_texture(b, l);
		memset(&p, 0, sizeof p);
		p.program = sd.prog[spark ? P_DESTELLO : P_CINTA];
		p.out = NULL;
		p.in[0] = input(&ramp);
		p.inputs = 1;
		rsxkit_post_mesh m = {
			.vertices = sd.off + OFF_VERT + (uint32_t)b * VERT_BYTES + (uint32_t)l * LAYER_BYTES,
			.indices = sd.off + (spark ? OFF_SIDX : OFF_IDX),
			.count = spark ? SEDA_SPARK_INDICES : SEDA_INDICES,
			.location = RSXKIT_MEM_MAIN,
			.blend = RSXKIT_BLEND_ADD,
			.cull = RSXKIT_CULL_NONE,
		};
		rc = rsxkit_post_run_mesh(&p, &m);
		if (rc != RSXKIT_OK) {
			salsa64_log("fondo seda: la malla %d falló (%d)", l, rc);
			return false;
		}
	}
	rsxkit_stamp((uint32_t)(STAMP_BASE + b * 2 + 1));
	return true;
}

bool seda_draw(double t)
{
	if (sd.failed)
		return false;
	const rsxkit_screen *sc = rsxkit_screen_info();
	if (!sd.ready || sc->width != sd.w || sc->height != sd.h) {
		if (!setup(sc->width, sc->height)) {
			release();
			sd.failed = true;
			salsa64_log("fondo seda: no cargó, vuelven las olas");
			return false;
		}
	}
	uint64_t c0 = sysGetSystemTime();
	const float aspect = (float)sd.w / (float)sd.h, tf = seda_phase(t * 0.608);  /* la tela, un poco más lenta */
	const int nb = (int)(sd.frame % BUFS);
	/* El RSX ya soltó lo que leyó de este búfer hace tres cuadros. */
	if (sd.fence[nb])
		rsxkit_fence_wait(sd.fence[nb]);
	collect(nb);

	struct seda_colors col;
	seda_colors(day_seconds(), &col);
	write_ramps(nb, &col);
	seda_vertex *v = (seda_vertex *)(sd.mem + OFF_VERT + (uint32_t)nb * VERT_BYTES);
	if (sd.use_spe) {
		struct seda_spe_job j = { .out_ea = (uint64_t)(uintptr_t)v, .aspect = aspect, .tf = tf };
		sd.seq[nb] = spukit_post(sd.spe, SEDA_SPE_JOB, &j, sizeof j);
		sd.pending[nb] = true;
	} else {
		for (int l = 0; l < SEDA_LAYERS; l++)
			seda_layer(l, aspect, tf, scratch, v + l * SEDA_LAYER_VERTS);
		seda_sparkles(aspect, tf, v + SEDA_LAYERS * SEDA_LAYER_VERTS);
		sd.cpu_us += sysGetSystemTime() - c0;
		sd.cpu_n++;
	}
	__sync_synchronize();  /* lo escrito, antes de que el RSX lo lea */

	/* Se dibuja el cuadro anterior (el primero, el mismo que se acaba de pedir). */
	const int db = sd.frame ? (int)((sd.frame - 1) % BUFS) : nb;
	if (sd.pending[db]) {
		const spukit_result *r = spukit_wait(sd.spe, sd.seq[db]);
		sd.pending[db] = false;
		if (r->error) {
			salsa64_log("fondo seda: el SPE falló (%d), vuelven las olas", r->error);
			release();
			sd.failed = true;
			return false;
		}
		sd.spe_ms += r->ticks * 1000.0 / (double)spukit_tick_hz();
		sd.spe_n++;
	}
	if (!draw_buffer(db, &col)) {
		release();
		sd.failed = true;
		salsa64_log("fondo seda: falló el dibujo, vuelven las olas");
		return false;
	}
	sd.stamped[db] = true;
	sd.fence[db] = rsxkit_fence();
	sd.frame++;
	report(sysGetSystemTime());
	return true;
}
