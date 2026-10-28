/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — secuencia de arranque al estilo de una tele CRT (intro.h).
 *
 * Cada cuadro: la escena (2D de rsxkit/ui.h) se dibuja en un destino de
 * 640 × 480 y el filtro Trinitron la reduce a 320 × 240 y la presenta. La
 * nieve es una imagen de 320 × 240 que se rehace en el PPE cada cuadro (77 K
 * píxeles, ~1 ms) y sube a la ranura de imagen 0 del 2D; el logo es
 * assets/ps3/ICON0.PNG, embebido, que ocupa la misma ranura desde el corte
 * de estática.
 *
 * Memoria (docs/memory-budget.md): nieve 300 KB y logo 220 KB en el heap,
 * el destino de 1,2 MB en el RSX y las tablas del filtro mientras dura.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/systime.h>
#include <sysutil/sysutil.h>

#include <rsxkit/rsxkit.h>
#include <rsxkit/ui.h>

#include "cover.h"
#include "filters/filters.h"
#include "icon_logo_bin.h"
#include "intro.h"
#include "platform/ps3/log.h"

#define W  640.0f  /* el destino, en espacio lógico y en píxeles */
#define H  480.0f
#define NW 320u    /* la nieve */
#define NH 240u

#define GREEN 0x3CEB5Au  /* el verde del OSD de una tele de los 90 */
#define FADE_IN_US 250000u

static struct {
	bool on;
	rsxkit_target *tgt;
	uint8_t *noise, *logo;
	unsigned logo_w, logo_h;
	uint32_t seed;
	struct intro_clock clk;
	uint64_t begin_us, t0_us, end_us;
	bool t0_set, failed, shown_noise;
	int img;  /* lo que tiene la ranura 0: 0 nada, 1 nieve, 2 logo */
	unsigned frames;
	float t;
	uint64_t fade_from;
} in;

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* 0xRRGGBB y un alfa de 0 a 1 → 0xRRGGBBAA. */
static uint32_t col(uint32_t rgb, float alpha)
{
	return (rgb << 8) | (uint32_t)(clampf(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static void centered(float cx, float cy, float w, float h, uint32_t rgba)
{
	rsxkit_ui_rect(cx - w * 0.5f, cy - h * 0.5f, w, h, rgba);
}

bool intro_begin(uint8_t *scratch, size_t scratch_size)
{
	memset(&in, 0, sizeof in);
	in.begin_us = sysGetSystemTime();
	if (!scratch || scratch_size < COVER_RGBA_BYTES)
		return false;
	in.tgt = rsxkit_target_create((uint32_t)W, (uint32_t)H);
	in.noise = malloc((size_t)NW * NH * 4);
	if (!in.tgt || !in.noise) {
		salsa64_log("intro: sin memoria para el destino o la nieve");
		goto fail;
	}
	in.seed = (uint32_t)in.begin_us | 1u;
	/* El logo es opcional: sin él, se escribe el nombre. */
	unsigned w = 0, h = 0;
	if (cover_decode_png(icon_logo_bin, icon_logo_bin_size, scratch, &w, &h) == COVER_OK && w <= RSXKIT_UI_IMAGE_MAX_W &&
	    h <= RSXKIT_UI_IMAGE_MAX_H && (in.logo = malloc((size_t)w * h * 4)) != NULL) {
		memcpy(in.logo, scratch, (size_t)w * h * 4);
		in.logo_w = w;
		in.logo_h = h;
	} else {
		salsa64_log("intro: el logo no se pudo decodificar, va el nombre en texto");
	}
	intro_clock_init(&in.clk);
	in.on = true;
	return true;
fail:
	free(in.noise);
	if (in.tgt)
		rsxkit_target_destroy(in.tgt);
	memset(&in, 0, sizeof in);
	return false;
}

enum intro_phase intro_update(bool loaded, bool skip)
{
	if (!in.on)
		return INTRO_DONE;
	/* El reloj arranca con el primer cuadro dibujado: preparar el filtro la
	 * primera vez espera al RSX y no debe comerse el encendido. */
	in.t = in.t0_set ? (float)(sysGetSystemTime() - in.t0_us) / 1e6f : 0.0f;
	return intro_clock_step(&in.clk, in.t, loaded, skip);
}

/* ---- Lo que se dibuja ----------------------------------------------------- */

/* Sube la nieve nueva o el logo a la ranura de imagen 0, según la fase. */
static void image_prepare(enum intro_phase ph)
{
	bool logo = (ph == INTRO_LOGO || ph == INTRO_FADE) && in.logo;
	if (!logo) {
		intro_noise_fill(&in.seed, in.noise, NW, NH);
		if (rsxkit_ui_image_set(0, in.noise, NW, NH) == RSXKIT_OK)
			in.img = 1;
	} else if (in.img != 2) {
		if (rsxkit_ui_image_set(0, in.logo, in.logo_w, in.logo_h) == RSXKIT_OK)
			in.img = 2;
	}
}

static void text_shadow(float x, float y, const char *s, float scale, uint32_t rgba, float shadow_alpha)
{
	rsxkit_ui_text(x + 2.0f, y + 2.0f, s, scale, col(0x000000, shadow_alpha), 0);
	rsxkit_ui_text(x, y, s, scale, rgba, 0);
}

/* Fecha y hora al estilo de una videocasetera: blancas con contorno negro. */
static void vcr_osd(float alpha)
{
	u64 sec = 0, nsec = 0;
	s32 tz = 0, dst = 0;
	sysGetCurrentTime(&sec, &nsec);
	if (sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, &tz) != 0)
		tz = 0;
	if (sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME, &dst) != 0)
		dst = 0;
	char date[16], tm[16];
	intro_vcr_text(sec, tz, dst != 0, date, tm);
	const float scale = 2.0f, right = W * 0.90f, bottom = H * 0.88f, line = 13.0f * scale;
	const char *lines[2] = { date, tm };
	for (int i = 0; i < 2; i++) {
		float x = right - rsxkit_ui_text_width(lines[i], scale);
		float y = bottom - line * (2 - i) - (i == 0 ? 3.0f : 0.0f);
		static const float off[8][2] = { { -2.0f, 0 }, { 2.0f, 0 }, { 0, -2.0f }, { 0, 2.0f },
						 { -1.5f, -1.5f }, { 1.5f, -1.5f }, { -1.5f, 1.5f }, { 1.5f, 1.5f } };
		for (int k = 0; k < 8; k++)
			rsxkit_ui_text(x + off[k][0], y + off[k][1], lines[i], scale, col(0x000000, alpha), 0);
		rsxkit_ui_text(x, y, lines[i], scale, col(0xFFFFFF, alpha), 0);
	}
}

static void osd(enum intro_phase ph, float in_ph)
{
	/* Aparece cuando la imagen ya se abrió. */
	float a = ph == INTRO_POWER ? clampf((in_ph - 0.42f) / 0.08f, 0.0f, 1.0f) : 1.0f;
	if (a <= 0.0f)
		return;
	text_shadow(W * 0.10f, H * 0.08f, "VIDEO1", 2.5f, col(GREEN, a), 0.85f * a);
	if (ph == INTRO_POWER || ph == INTRO_STATIC)
		text_shadow(W * 0.10f, H * 0.85f - 13.0f * 2.0f, "No Signal", 2.0f, col(GREEN, a), 0.9f * a);
	vcr_osd(a);
}

static void power_on(float t)
{
	const float cx = W * 0.5f, cy = H * 0.5f;
	if (t < 0.10f) {
		/* El punto brillante. */
		float u = t / 0.10f, s = 3.0f + 7.0f * u;
		centered(cx, cy, s * 5.0f, s * 5.0f, 0xFFFFFF18u);
		centered(cx, cy, s * 2.5f, s * 2.5f, 0xFFFFFF50u);
		centered(cx, cy, s, s, 0xFFFFFFFFu);
	} else if (t < 0.24f) {
		/* Se abre en una línea fina. */
		float u = (t - 0.10f) / 0.14f, e = 1.0f - (1.0f - u) * (1.0f - u), w = 10.0f + (W - 10.0f) * e;
		centered(cx, cy, w, 12.0f, 0xFFFFFF22u);
		centered(cx, cy, w, 6.0f, 0xFFFFFF60u);
		centered(cx, cy, w, 3.0f, 0xFFFFFFFFu);
	} else {
		/* Crece en vertical hasta llenar la pantalla; el destello se apaga. */
		float u = clampf((t - 0.24f) / 0.26f, 0.0f, 1.0f), e = u * u * (3.0f - 2.0f * u);
		float h = 3.0f + (H - 3.0f) * e, y0 = cy - h * 0.5f;
		rsxkit_ui_image(0, 0.0f, y0, W, h, 0xFFFFFFFFu);
		float hot = (1.0f - u) * (1.0f - u);
		rsxkit_ui_rect(0.0f, y0, W, h, col(0xFFFFFF, 0.85f * hot));
		rsxkit_ui_rect(0.0f, y0 - 1.5f, W, 3.0f, col(0xFFFFFF, 0.9f * (1.0f - u * 0.6f)));
		rsxkit_ui_rect(0.0f, y0 + h - 1.5f, W, 3.0f, col(0xFFFFFF, 0.9f * (1.0f - u * 0.6f)));
	}
}

bool intro_draw(void)
{
	if (!in.on || in.failed)
		return false;
	enum intro_phase ph = in.clk.phase;
	float t = in.t, p = t - in.clk.start;
	image_prepare(ph);

	rsxkit_ui_begin_target(in.tgt, W, H);
	rsxkit_clear(RSXKIT_CLEAR_COLOR, 0x000000, 0.0f, NULL);
	switch (ph) {
	case INTRO_POWER:
		power_on(p);
		break;
	case INTRO_STATIC:
		rsxkit_ui_image(0, 0.0f, 0.0f, W, H, 0xFFFFFFFFu);
		if (p < 0.12f)  /* el último destello de la apertura */
			rsxkit_ui_rect(0.0f, 0.0f, W, H, col(0xFFFFFF, 0.45f * (1.0f - p / 0.12f)));
		break;
	case INTRO_CUT: {
		/* La tele pierde el sincronismo un instante: barra que rueda y brillo. */
		rsxkit_ui_image(0, 0.0f, 0.0f, W, H, 0xFFFFFFFFu);
		float u = p / INTRO_CUT_S;
		rsxkit_ui_rect(0.0f, -80.0f + u * (H + 160.0f), W, 70.0f, 0x000000D0u);
		rsxkit_ui_rect(0.0f, 0.0f, W, H, col(0xFFFFFF, 0.25f * (1.0f - u)));
		break;
	}
	case INTRO_LOGO:
	case INTRO_FADE: {
		float jx = 0.0f, a = 1.0f;
		if (ph == INTRO_LOGO && p < 0.18f) {
			/* Engancha la señal: tiembla y parpadea los primeros cuadros. */
			uint32_t r = in.seed ^ (in.frames * 2654435761u);
			jx = (float)(int)(r >> 28) - 8.0f;
			a = 0.55f + 0.45f * (float)((r >> 8) & 0xFF) / 255.0f;
		}
		if (in.logo && in.img == 2) {
			float lw = 560.0f, lh = lw * (float)in.logo_h / (float)in.logo_w;
			rsxkit_ui_image(0, (W - lw) * 0.5f + jx, (H - lh) * 0.5f, lw, lh, col(0xFFFFFF, a));
		} else {
			const char *s = "SALSA64";
			float sc = 7.0f;
			rsxkit_ui_text((W - rsxkit_ui_text_width(s, sc)) * 0.5f + jx, H * 0.5f - 13.0f * sc * 0.5f, s, sc,
				       col(0xE03020, a), 0);
		}
		break;
	}
	default:
		break;
	}
	osd(ph, p);
	if (ph == INTRO_FADE)
		rsxkit_ui_rect(0.0f, 0.0f, W, H, col(0x000000, clampf(p / INTRO_FADE_S, 0.0f, 1.0f)));
	rsxkit_ui_end();
	rsxkit_texture_sync();

	struct salsa64_filter_src src;
	memset(&src, 0, sizeof src);
	rsxkit_target_texture(in.tgt, 0, 0, &src.tex);
	src.region = (rsxkit_rect){ 0, 0, (uint32_t)W, (uint32_t)H };
	src.factor = 2;
	src.aspect = 4.0f / 3.0f;
	if (!salsa64_filters_draw(SALSA64_CRT_TRINITRON, &src, NULL)) {
		salsa64_log("intro: el filtro Trinitron no dibujó: sin secuencia de arranque");
		in.failed = true;
		return false;
	}
	if (!in.t0_set) {
		in.t0_set = true;
		in.t0_us = sysGetSystemTime();
	}
	in.frames++;
	return true;
}

void intro_end(uint32_t loaded_ms)
{
	if (!in.on)
		return;
	in.end_us = sysGetSystemTime();
	salsa64_log("intro: primer cuadro a los %llu ms de prepararla, duró %u ms en %u cuadros, %s; carga lista a los %u ms",
		    (unsigned long long)((in.t0_set ? in.t0_us - in.begin_us : 0) / 1000),
		    (unsigned)((in.end_us - in.begin_us) / 1000), in.frames,
		    in.failed ? "el filtro falló" : in.clk.skipped ? "saltada" : "completa", loaded_ms);
	salsa64_filters_release();
	rsxkit_target_destroy(in.tgt);
	in.tgt = NULL;
	free(in.noise);
	free(in.logo);
	in.noise = in.logo = NULL;
	in.on = false;
	in.fade_from = in.clk.skipped || in.failed ? 0 : sysGetSystemTime();
}

uint8_t intro_menu_fade(void)
{
	if (!in.fade_from)
		return 0;
	uint64_t dt = sysGetSystemTime() - in.fade_from;
	if (dt >= FADE_IN_US) {
		in.fade_from = 0;
		return 0;
	}
	return (uint8_t)(255 - 255 * dt / FADE_IN_US);
}
