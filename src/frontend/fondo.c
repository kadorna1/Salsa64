/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — fondo «Shader» del menú en el RSX. Ver fondo.h, fondo_calc.h y
 * docs/fondo-shader.md.
 *
 * Basado en el shader «XdGfRR» de Shadertoy, de int_45h
 * (https://www.shadertoy.com/view/XdGfRR). Ver docs/THIRD_PARTY.md.
 *
 * Cada cuadro, la CPU rellena lo que falte de los anillos del hash, escribe
 * la malla de la ola y la tabla de h (en dos copias: una la lee el RSX
 * mientras se escribe la otra) y el RSX hace:
 *   marcha (4 pasadas de 25 pasos) → polvo → desempaque   a 1/8 de la pantalla
 *   borrar la ola → 4 mallas y 2 tapas sumando en fp16    a la de la pantalla
 *   pasada final, con estrellas solo en las filas con polvo
 */
#include "frontend/fondo.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/memory.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>

#include <rsxkit/post.h>

#include "frontend/fondo_calc.h"
#include "platform/ps3/log.h"
#include "platform/ps3/paths.h"

#include "fondo_final_fpo.h"
#include "fondo_final_lite_fpo.h"
#include "fondo_low_fpo.h"
#include "fondo_marcha0_fpo.h"
#include "fondo_marcha_fpo.h"
#include "fondo_ola_fpo.h"
#include "fondo_polvo_fpo.h"

enum { P_OLA_POS, P_OLA_NEG, P_MARCHA0, P_MARCHA, P_POLVO, P_LOW, P_FINAL, P_LITE, PROGRAMS };
static const u8 *const program_fpo[PROGRAMS] = {
	[P_OLA_POS] = fondo_ola_fpo,     [P_OLA_NEG] = fondo_ola_fpo, [P_MARCHA0] = fondo_marcha0_fpo,
	[P_MARCHA] = fondo_marcha_fpo,   [P_POLVO] = fondo_polvo_fpo, [P_LOW] = fondo_low_fpo,
	[P_FINAL] = fondo_final_fpo,     [P_LITE] = fondo_final_lite_fpo,
};

/* La resolución reducida (marcha y polvo): 1/8 de la pantalla, columnas
 * en múltiplos de 4. */
#define LOW_DIV 8
#define HTAB_ROWS_MAX ((1920 / LOW_DIV + 3) / 4)
#define HTAB_PITCH (FONDO_NT * 8)

/* Memoria principal mapeada al RSX (páginas de 1 MB del sistema). */
#define ALIGN128(x) (((x) + 127u) & ~127u)
#define OFF_STAR 0u
#define OFF_BRI  (OFF_STAR + FONDO_STAR_W * FONDO_STAR_H * 4u)
#define OFF_VERT (OFF_BRI + FONDO_BRI_W * FONDO_BRI_H * 4u)
#define VERT_BYTES ALIGN128(FONDO_VERTS * (uint32_t)sizeof(fondo_vertex))
#define OFF_IDX  (OFF_VERT + 2u * VERT_BYTES)
#define OFF_HTAB (OFF_IDX + ALIGN128(FONDO_INDICES * 2u))
#define HTAB_BYTES ((uint32_t)HTAB_ROWS_MAX * HTAB_PITCH)
/* La primitiva F̃ de la ola en 256 valores de t (fondo_ola.fcg). */
#define PRIM_N 256u
#define OFF_PRIM (OFF_HTAB + 2u * HTAB_BYTES)
#define MEM_USED (OFF_PRIM + PRIM_N * 8u)
#define MEM_BYTES ((MEM_USED + (1u << 20) - 1) & ~((1u << 20) - 1))

/* Marcas de tiempo del RSX (las de los filtros van de 0 a 31). */
#define STAMP_BASE 40
enum { ST_BEGIN, ST_LOW, ST_OLA, ST_END, STAMPS };

static struct {
	bool failed;     /* no se pudo: olas hasta fondo_shader_release */
	bool half;       /* la ola a la mitad de la resolución (`fondo-ola-media`) */
	bool ready;
	uint32_t w, h;
	struct fondo_view vw;
	uint8_t *mem;
	uint32_t off;
	bool mapped;
	struct fondo_tex tx;
	rsxkit_texture star, bri;
	rsxkit_program *prog[PROGRAMS];
	rsxkit_surface *glow, *marcha[2], *polvo, *low, *htab[2], *prim;
	uint32_t fence[2];
	bool stamped[2];
	uint32_t frame;
	/* Medición: ms del RSX acumulados desde el último registro. */
	double ms[STAMPS - 1], ms_total;
	uint32_t measured, band_rows;
	uint64_t log_us, setup_us, cpu_us;
	int64_t tz_s;    /* zona horaria y horario de verano, en segundos */
} fd;

static void release(void)
{
	if (fd.ready || fd.mapped)
		rsxkit_finish();
	rsxkit_surface_destroy(fd.glow);
	rsxkit_surface_destroy(fd.polvo);
	rsxkit_surface_destroy(fd.low);
	rsxkit_surface_destroy(fd.prim);
	for (int i = 0; i < 2; i++) {
		rsxkit_surface_destroy(fd.marcha[i]);
		rsxkit_surface_destroy(fd.htab[i]);
	}
	for (int i = 0; i < PROGRAMS; i++)
		rsxkit_program_destroy(fd.prog[i]);
	if (fd.mapped)
		rsxkit_unmap_main(fd.mem);
	if (fd.mem)
		sysMemoryFree((sys_mem_addr_t)(uintptr_t)fd.mem);
	memset(&fd, 0, sizeof fd);
}

void fondo_shader_release(void)
{
	if (fd.ready || fd.mem)
		salsa64_log("fondo: shader liberado");
	release();
}

static bool setup(uint32_t w, uint32_t h)
{
	uint64_t t0 = sysGetSystemTime();
	release();
	sys_mem_addr_t addr = 0;
	if (sysMemoryAllocate(MEM_BYTES, SYS_MEMORY_PAGE_SIZE_1M, &addr) != 0) {
		salsa64_log("fondo: sin %u MB de memoria principal", MEM_BYTES >> 20);
		return false;
	}
	fd.mem = (uint8_t *)(uintptr_t)addr;
	if (rsxkit_map_main(fd.mem, MEM_BYTES, &fd.off) != RSXKIT_OK) {
		salsa64_log("fondo: no se pudo mapear la memoria para el RSX");
		return false;
	}
	fd.mapped = true;
	fd.w = w, fd.h = h;
	uint32_t lw = (w / LOW_DIV + 3) & ~3u;
	fd.vw = (struct fondo_view){ w, h, lw, h / LOW_DIV };
	if (lw / 4 > HTAB_ROWS_MAX) {
		salsa64_log("fondo: pantalla de %ux%u más ancha que la tabla", w, h);
		return false;
	}

	for (int i = 0; i < PROGRAMS; i++)
		if (!(fd.prog[i] = rsxkit_program_create(program_fpo[i]))) {
			salsa64_log("fondo: no se pudo crear el programa %d", i);
			return false;
		}
	const float pos[4] = { 1.0f, 0.0f, 0.0f, 0.0f }, neg[4] = { -1.0f, 0.0f, 0.0f, 0.0f };
	rsxkit_program_set(fd.prog[P_OLA_POS], "sgn", pos, 1);
	rsxkit_program_set(fd.prog[P_OLA_NEG], "sgn", neg, 1);

	const struct fondo_view *vw = &fd.vw;
	/* Con `fondo-ola-media`, la ola a la mitad de la resolución (un cuarto
	 * de los fragmentos y del borrado; las líneas, algo más blandas). */
	FILE *hf = fopen(SALSA64_DATA_DIR "/fondo-ola-media", "r");
	fd.half = hf != NULL;
	if (hf)
		fclose(hf);
	fd.glow = rsxkit_surface_create(fd.half ? w / 2 : w, fd.half ? h / 2 : h, RSXKIT_POST_RGBA16F);
	fd.marcha[0] = rsxkit_surface_create(vw->lw / 4, vw->lh, RSXKIT_POST_RGBA16F);
	fd.marcha[1] = rsxkit_surface_create(vw->lw / 4, vw->lh, RSXKIT_POST_RGBA16F);
	fd.polvo = rsxkit_surface_create(vw->lw / 4, vw->lh, RSXKIT_POST_RGBA16F);
	fd.low = rsxkit_surface_create(vw->lw, vw->lh, RSXKIT_POST_RGBA16F);
	for (int i = 0; i < 2; i++)
		fd.htab[i] = rsxkit_surface_wrap_main(FONDO_NT, vw->lw / 4, RSXKIT_POST_RGBA16F,
						      fd.mem + OFF_HTAB + i * HTAB_BYTES, fd.off + OFF_HTAB + i * HTAB_BYTES,
						      HTAB_PITCH);
	fd.prim = rsxkit_surface_wrap_main(PRIM_N, 1, RSXKIT_POST_RGBA16F, fd.mem + OFF_PRIM, fd.off + OFF_PRIM,
					   PRIM_N * 8u);
	if (!fd.glow || !fd.marcha[0] || !fd.marcha[1] || !fd.polvo || !fd.low || !fd.htab[0] || !fd.htab[1] ||
	    !fd.prim) {
		salsa64_log("fondo: sin superficies del RSX (%.1f MB en uso)", rsxkit_surface_bytes() / 1048576.0);
		return false;
	}

	/* Las texturas del hash (RGBA8, bytes R, G, B, A), que se repiten. */
	fd.tx.star = fd.mem + OFF_STAR;
	fd.tx.bri = fd.mem + OFF_BRI;
	fondo_tex_reset(&fd.tx);
	memset(&fd.star, 0, sizeof fd.star);
	fd.star.offset = fd.off + OFF_STAR;
	fd.star.pitch = FONDO_STAR_W * 4;
	fd.star.width = FONDO_STAR_W;
	fd.star.height = FONDO_STAR_H;
	fd.star.location = RSXKIT_MEM_MAIN;
	fd.star.format = RSXKIT_RGBA8888;
	fd.bri = fd.star;
	fd.bri.offset = fd.off + OFF_BRI;
	fd.bri.pitch = FONDO_BRI_W * 4;
	fd.bri.width = FONDO_BRI_W;
	fd.bri.height = FONDO_BRI_H;
	fondo_indices((uint16_t *)(fd.mem + OFF_IDX));
	uint16_t *prim = (uint16_t *)(fd.mem + OFF_PRIM);
	for (uint32_t i = 0; i < PRIM_N; i++) {
		prim[i * 4] = fondo_half(fondo_prim(FONDO_MAX_DIST * i / (PRIM_N - 1)));
		prim[i * 4 + 1] = prim[i * 4 + 2] = prim[i * 4 + 3] = 0;
	}
	__sync_synchronize();
	s32 tz = 0, summer = 0;
	sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, &tz);
	sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME, &summer);
	fd.tz_s = (int64_t)tz * 60 + (summer ? 3600 : 0);
	fd.ready = true;
	fd.setup_us = sysGetSystemTime() - t0;
	uint32_t insns[PROGRAMS];
	for (int i = 0; i < PROGRAMS; i++)
		insns[i] = rsxkit_program_instructions(fd.prog[i]);
	salsa64_log("fondo: shader a %ux%u (ola a %s), marcha y polvo a %ux%u, %u MB de memoria principal y %.1f MB "
		    "del RSX, hora local UTC%+d min; instrucciones: ola %u, marcha %u+3×%u, polvo %u, desempaque %u, "
		    "final %u (sin estrellas %u)",
		    w, h, fd.half ? "la mitad" : "la misma", vw->lw, vw->lh, MEM_BYTES >> 20,
		    rsxkit_surface_bytes() / 1048576.0, (int)(fd.tz_s / 60),
		    insns[P_OLA_POS],
		    insns[P_MARCHA0], insns[P_MARCHA], insns[P_POLVO], insns[P_LOW], insns[P_FINAL], insns[P_LITE]);
	return true;
}

/* Segundos desde la medianoche en la hora local de la consola (la zona y
 * el horario de verano se leen al armar el fondo). */
static double day_seconds(void)
{
	u64 sec = 0, nsec = 0;
	sysGetCurrentTime(&sec, &nsec);
	int64_t d = ((int64_t)sec + fd.tz_s) % 86400;
	if (d < 0)
		d += 86400;
	return (double)d + nsec * 1e-9;
}

static rsxkit_post_input input(const rsxkit_surface *s, rsxkit_filter f)
{
	rsxkit_post_input in = rsxkit_post_surface_input(s, f, RSXKIT_POST_CLAMP);
	in.texels = 0;  /* coordenadas normalizadas */
	return in;
}

static rsxkit_post_input input_tex(const rsxkit_texture *t)
{
	rsxkit_post_input in = rsxkit_post_texture_input(t, RSXKIT_FILTER_LINEAR, RSXKIT_POST_REPEAT);
	in.texels = 0;
	return in;
}

static bool run(rsxkit_post_pass *p)
{
	int rc = rsxkit_post_run(p);
	if (rc != RSXKIT_OK)
		salsa64_log("fondo: una pasada falló (%d)", rc);
	return rc == RSXKIT_OK;
}

/* Lo que mide un cuadro ya terminado. */
static void collect(int b)
{
	if (!fd.stamped[b])
		return;
	fd.stamped[b] = false;
	uint64_t t[STAMPS];
	for (int i = 0; i < STAMPS; i++)
		t[i] = rsxkit_stamp_ns((uint32_t)(STAMP_BASE + b * 8 + i));
	if (t[ST_END] <= t[ST_BEGIN] || t[ST_END] - t[ST_BEGIN] > 1000000000ull)
		return;
	for (int i = 0; i + 1 < STAMPS; i++)
		fd.ms[i] += (t[i + 1] - t[i]) / 1e6;
	fd.ms_total += (t[ST_END] - t[ST_BEGIN]) / 1e6;
	fd.measured++;
}

static void report(uint64_t now)
{
	if (!fd.log_us)
		fd.log_us = now;
	if (now - fd.log_us < 5000000u || !fd.measured)
		return;
	double n = fd.measured;
	salsa64_log("fondo: %.2f ms del RSX por cuadro (marcha y polvo %.2f, ola %.2f, final %.2f; filas con estrellas "
		    "%u de %u), CPU %.2f ms, %u cuadros",
		    fd.ms_total / n, fd.ms[0] / n, fd.ms[1] / n, fd.ms[2] / n, fd.band_rows, fd.h, fd.cpu_us / n / 1000.0,
		    fd.measured);
	memset(fd.ms, 0, sizeof fd.ms);
	fd.ms_total = 0;
	fd.measured = 0;
	fd.cpu_us = 0;
	fd.log_us = now;
}

static bool passes(int b, const struct fondo_frame *fr)
{
	const struct fondo_view *vw = &fd.vw;
	rsxkit_post_pass p;
	/* Marcha: 4 pasadas de 25 pasos, de marcha[0] a marcha[1] y vuelta. */
	for (int i = 0; i < FONDO_STEPS / 25; i++) {
		memset(&p, 0, sizeof p);
		p.program = fd.prog[i ? P_MARCHA : P_MARCHA0];
		p.out = fd.marcha[i & 1];
		p.in[0] = input(fd.htab[b], RSXKIT_FILTER_LINEAR);
		p.inputs = 1;
		if (i) {
			p.in[1] = input(fd.marcha[(i - 1) & 1], RSXKIT_FILTER_NEAREST);
			p.inputs = 2;
		}
		memcpy(p.k[0], fr->low_k, sizeof p.k[0]);
		memcpy(p.ky[0], fr->low_ky, sizeof p.ky[0]);
		if (!run(&p))
			return false;
	}
	rsxkit_surface *marcha = fd.marcha[(FONDO_STEPS / 25 - 1) & 1];
	memset(&p, 0, sizeof p);
	p.program = fd.prog[P_POLVO];
	p.out = fd.polvo;
	p.in[0] = input(fd.htab[b], RSXKIT_FILTER_LINEAR);
	p.in[1] = input(marcha, RSXKIT_FILTER_NEAREST);
	p.inputs = 2;
	memcpy(p.k[0], fr->low_k, sizeof p.k[0]);
	memcpy(p.ky[0], fr->low_ky, sizeof p.ky[0]);
	if (!run(&p))
		return false;
	memset(&p, 0, sizeof p);
	p.program = fd.prog[P_LOW];
	p.out = fd.low;
	p.in[0] = input(marcha, RSXKIT_FILTER_NEAREST);
	p.in[1] = input(fd.polvo, RSXKIT_FILTER_NEAREST);
	p.inputs = 2;
	memcpy(p.k[0], fr->unpack_k, sizeof p.k[0]);
	if (!run(&p))
		return false;
	rsxkit_stamp((uint32_t)(STAMP_BASE + b * 8 + ST_LOW));

	/* La ola: superficie +0,04 (de frente se entra, de atrás se sale),
	 * −0,04 al revés, y las tapas en t = 40 y t = 1. */
	if (rsxkit_post_clear(fd.glow) != RSXKIT_OK)
		return false;
	static const struct {
		uint32_t vert, idx, count;
		uint8_t cull;
		int prog;
	} draws[] = {
		{ FONDO_VERT_SURF_P, 0, FONDO_SURF_INDICES, RSXKIT_CULL_BACK, P_OLA_NEG },
		{ FONDO_VERT_SURF_P, 0, FONDO_SURF_INDICES, RSXKIT_CULL_FRONT, P_OLA_POS },
		{ FONDO_VERT_SURF_N, 0, FONDO_SURF_INDICES, RSXKIT_CULL_BACK, P_OLA_POS },
		{ FONDO_VERT_SURF_N, 0, FONDO_SURF_INDICES, RSXKIT_CULL_FRONT, P_OLA_NEG },
		{ FONDO_VERT_CAP_FAR, FONDO_SURF_INDICES, FONDO_CAP_INDICES, RSXKIT_CULL_NONE, P_OLA_POS },
		{ FONDO_VERT_CAP_NEAR, FONDO_SURF_INDICES, FONDO_CAP_INDICES, RSXKIT_CULL_NONE, P_OLA_NEG },
	};
	for (unsigned i = 0; i < sizeof draws / sizeof draws[0]; i++) {
		memset(&p, 0, sizeof p);
		p.program = fd.prog[draws[i].prog];
		p.out = fd.glow;
		p.in[0] = input(fd.low, RSXKIT_FILTER_LINEAR);
		p.in[1] = input(fd.prim, RSXKIT_FILTER_LINEAR);
		p.inputs = 2;
		rsxkit_post_mesh m = {
			.vertices = fd.off + OFF_VERT + b * VERT_BYTES + draws[i].vert * (uint32_t)sizeof(fondo_vertex),
			.indices = fd.off + OFF_IDX + draws[i].idx * 2u,
			.count = draws[i].count,
			.location = RSXKIT_MEM_MAIN,
			.blend = RSXKIT_BLEND_ADD,
			.cull = draws[i].cull,
		};
		int rc = rsxkit_post_run_mesh(&p, &m);
		if (rc != RSXKIT_OK) {
			salsa64_log("fondo: la malla de la ola falló (%d)", rc);
			return false;
		}
	}
	rsxkit_stamp((uint32_t)(STAMP_BASE + b * 8 + ST_OLA));

	/* Final en tres franjas: sin estrellas, con estrellas (las filas con
	 * polvo) y sin estrellas. */
	const uint32_t rows[4] = { 0, fr->band_y0, fr->band_y1, fd.h };
	for (int s = 0; s < 3; s++) {
		if (rows[s + 1] <= rows[s])
			continue;
		bool stars = s == 1;
		memset(&p, 0, sizeof p);
		p.program = fd.prog[stars ? P_FINAL : P_LITE];
		p.out = NULL;
		p.rect = (rsxkit_rect){ 0, (int32_t)rows[s], fd.w, rows[s + 1] - rows[s] };
		/* TEXCOORD0.xy: la coordenada normalizada de la pantalla. */
		p.origin[0] = 0.0f;
		p.origin[1] = (float)rows[s] / fd.h;
		p.step[0] = 1.0f / fd.w;
		p.step[1] = 1.0f / fd.h;
		p.in[0] = input(fd.glow, fd.half ? RSXKIT_FILTER_LINEAR : RSXKIT_FILTER_NEAREST);
		p.inputs = 1;
		if (stars) {
			p.in[1] = input(fd.low, RSXKIT_FILTER_LINEAR);
			p.in[2] = input_tex(&fd.star);
			p.in[3] = input_tex(&fd.bri);
			p.inputs = 4;
		}
		/* Las constantes afines cuentan desde la esquina del rectángulo. */
		for (int j = 0; j < 5; j++)
			for (int c = 0; c < 4; c++) {
				p.k[j][c] = fr->k[j][c] + fr->ky[j][c] * rows[s];
				p.kx[j][c] = fr->kx[j][c];
				p.ky[j][c] = fr->ky[j][c];
			}
		if (!run(&p))
			return false;
	}
	(void)vw;
	return true;
}

/* Con el archivo `fondo-volcado` en SALSA64_DATA_DIR, el cuadro 40 deja la
 * superficie de la ola y la reducida (fp16 tal cual, en el orden de la
 * PS3) en fondo-ola.f16 y fondo-low.f16, con su iTime en el log, para
 * compararlas con tools/fondo-ref.c (`ola`). */
static void dump_surface(const rsxkit_surface *s, const char *name)
{
	uint32_t w, h, pitch;
	rsxkit_surface_size(s, &w, &h);
	const uint8_t *p = rsxkit_surface_data(s, &pitch);
	FILE *f = fopen(name, "wb");
	if (!f || !p) {
		if (f)
			fclose(f);
		return;
	}
	uint32_t hdr[2] = { w, h };
	fwrite(hdr, sizeof hdr, 1, f);
	static uint8_t row[4096 * 8];
	for (uint32_t y = 0; y < h; y++) {
		memcpy(row, p + (size_t)y * pitch, w * 8);
		fwrite(row, 8, w, f);
	}
	fclose(f);
}

static void maybe_dump(double t)
{
	if (fd.frame != 40)
		return;
	FILE *f = fopen(SALSA64_DATA_DIR "/fondo-volcado", "r");
	if (!f)
		return;
	fclose(f);
	rsxkit_finish();
	dump_surface(fd.glow, SALSA64_DATA_DIR "/fondo-ola.f16");
	dump_surface(fd.low, SALSA64_DATA_DIR "/fondo-low.f16");
	salsa64_log("fondo: volcado de la ola y de la reducida en iTime %.6f", t);
}

bool fondo_shader_draw(double t)
{
	if (fd.failed)
		return false;
	const rsxkit_screen *sc = rsxkit_screen_info();
	if (!fd.ready || sc->width != fd.w || sc->height != fd.h) {
		if (!setup(sc->width, sc->height)) {
			release();
			fd.failed = true;
			salsa64_log("fondo: el shader no cargó, vuelven las olas");
			return false;
		}
	}
	uint64_t c0 = sysGetSystemTime();
	int b = (int)(fd.frame & 1);
	/* El RSX ya soltó lo que escribió en esta copia hace dos cuadros. */
	if (fd.fence[b])
		rsxkit_fence_wait(fd.fence[b]);
	collect(b);

	struct fondo_frame fr;
	fondo_tex_update(&fd.tx, &fd.vw, t);
	if (!fd.frame)
		salsa64_log("fondo: %u texeles del hash en %.1f ms del PPE (la primera vez)", fd.tx.filled,
			    (sysGetSystemTime() - c0) / 1000.0);
	fondo_mesh((fondo_vertex *)(fd.mem + OFF_VERT + b * VERT_BYTES), &fd.vw, t);
	fondo_htable((uint16_t *)(fd.mem + OFF_HTAB + b * HTAB_BYTES), HTAB_PITCH, &fd.vw, t);
	fondo_frame_consts(&fr, &fd.vw, t, day_seconds());
	__sync_synchronize();  /* lo escrito, antes de que el RSX lo lea */
	fd.cpu_us += sysGetSystemTime() - c0;
	fd.band_rows = fr.band_y1 - fr.band_y0;

	rsxkit_stamp((uint32_t)(STAMP_BASE + b * 8 + ST_BEGIN));
	if (!passes(b, &fr)) {
		release();
		fd.failed = true;
		salsa64_log("fondo: el shader falló, vuelven las olas");
		return false;
	}
	rsxkit_stamp((uint32_t)(STAMP_BASE + b * 8 + ST_END));
	fd.stamped[b] = true;
	fd.fence[b] = rsxkit_fence();
	maybe_dump(t);
	fd.frame++;
	report(sysGetSystemTime());
	return true;
}
