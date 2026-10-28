/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Salsa64 — íconos de botones en línea con el texto. Ver prompts.h. */
#include "prompts.h"

#include <string.h>

#include <rsxkit/text.h>
#include <rsxkit/ui.h>

static bool enabled;

static const struct {
	const char *s;
	int icon;
} symbols[] = {
	{ "\xE2\x86\x91\xE2\x86\x93", PROMPT_DPAD_UD },  /* ↑↓ */
	{ "\xC3\x97", PROMPT_X },                        /* × */
	{ "\xE2\x97\x8B", PROMPT_CIRCLE },               /* ○ */
	{ "\xE2\x96\xA1", PROMPT_SQUARE },               /* □ */
	{ "\xE2\x96\xB3", PROMPT_TRIANGLE },             /* △ */
};

static const struct {
	const char *s;
	int icon;
} words[] = {
	{ "SELECT", PROMPT_SELECT }, { "START", PROMPT_START }, { "L1", PROMPT_L1 }, { "R1", PROMPT_R1 },
	{ "L2", PROMPT_L2 },         { "R2", PROMPT_R2 },       { "L3", PROMPT_L3 }, { "R3", PROMPT_R3 },
};

static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static bool is_word(char c)
{
	return is_digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

/* ¿Hay un ícono en q? Devuelve los bytes que ocupa (0 si no). */
static size_t match_at(const char *base, const char *q, int *icon, bool *marker)
{
	*marker = false;
	if (*q == '{') {
		const char *e = strchr(q, '}');
		if (!e || e - q > 16)
			return 0;
		size_t len = (size_t)(e - q) - 1;
		for (int i = 0; i < PROMPT_COUNT; i++)
			if (strlen(prompt_table[i].name) == len && !memcmp(prompt_table[i].name, q + 1, len)) {
				*icon = i;
				*marker = true;
				return len + 2;
			}
		return 0;
	}
	for (unsigned i = 0; i < sizeof symbols / sizeof symbols[0]; i++) {
		size_t n = strlen(symbols[i].s);
		if (strncmp(q, symbols[i].s, n))
			continue;
		if (symbols[i].icon == PROMPT_X && ((q > base && (is_digit(q[-1]) || (q - base >= 2 && q[-1] == 'd' && q[-2] == '%'))) || is_digit(q[n])))
			return 0;  /* 5×, 3×3: multiplicación */
		*icon = symbols[i].icon;
		return n;
	}
	if (q > base && is_word(q[-1]))
		return 0;
	for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
		size_t n = strlen(words[i].s);
		if (!strncmp(q, words[i].s, n) && !is_word(q[n])) {
			*icon = words[i].icon;
			return n;
		}
	}
	return 0;
}

void prompt_iter_init(struct prompt_iter *it, const char *s)
{
	it->base = it->p = s;
}

bool prompt_iter_next(struct prompt_iter *it, struct prompt_tok *t)
{
	const char *p = it->p;
	if (!*p)
		return false;
	int icon = -1;
	bool marker;
	size_t n = match_at(it->base, p, &icon, &marker);
	if (n) {
		*t = (struct prompt_tok){ icon, marker, p, n };
		it->p = p + n;
		return true;
	}
	const char *q = p + 1;
	while (*q && !match_at(it->base, q, &icon, &marker))
		q++;
	*t = (struct prompt_tok){ -1, false, p, (size_t)(q - p) };
	it->p = q;
	return true;
}

const char *prompt_fallback(int icon)
{
	switch (icon) {
	case PROMPT_X: return "\xC3\x97";
	case PROMPT_CIRCLE: return "\xE2\x97\x8B";
	case PROMPT_SQUARE: return "\xE2\x96\xA1";
	case PROMPT_TRIANGLE: return "\xE2\x96\xB3";
	case PROMPT_SELECT: return "SELECT";
	case PROMPT_START: return "START";
	case PROMPT_L1: return "L1";
	case PROMPT_R1: return "R1";
	case PROMPT_L2: return "L2";
	case PROMPT_R2: return "R2";
	case PROMPT_L3: return "L3";
	case PROMPT_R3: return "R3";
	case PROMPT_DPAD_UD: return "\xE2\x86\x91\xE2\x86\x93";
	case PROMPT_DPAD_LR: return "\xE2\x86\x90\xE2\x86\x92";
	case PROMPT_STICK_L: return "L";
	case PROMPT_STICK_R: return "R";
	case PROMPT_N64_A: return "A";
	case PROMPT_N64_B: return "B";
	case PROMPT_N64_START: return "S";
	case PROMPT_N64_Z: return "Z";
	case PROMPT_N64_L: return "L";
	case PROMPT_N64_R: return "R";
	case PROMPT_N64_DPAD_UP:
	case PROMPT_N64_C_UP: return "\xE2\x86\x91";
	case PROMPT_N64_DPAD_DOWN:
	case PROMPT_N64_C_DOWN: return "\xE2\x86\x93";
	case PROMPT_N64_DPAD_LEFT:
	case PROMPT_N64_C_LEFT: return "\xE2\x86\x90";
	case PROMPT_N64_DPAD_RIGHT:
	case PROMPT_N64_C_RIGHT: return "\xE2\x86\x92";
	default: return "+";
	}
}

void prompts_enable(bool on)
{
	enabled = on;
}

bool prompts_enabled(void)
{
	return enabled;
}

float prompt_icon_size(float scale)
{
	return RSXKIT_FONT_H * scale * 1.4f;
}

#define SPAN_MAX 256

static void span_copy(char *dst, const char *s, size_t n)
{
	if (n >= SPAN_MAX)
		n = SPAN_MAX - 1;
	memcpy(dst, s, n);
	dst[n] = 0;
}

/* Sin atlas: el texto con los marcadores cambiados por sus símbolos. */
static void plain(char *out, size_t cap, const char *s)
{
	struct prompt_iter it;
	struct prompt_tok t;
	size_t o = 0;
	out[0] = 0;
	prompt_iter_init(&it, s);
	while (prompt_iter_next(&it, &t)) {
		const char *src = t.marker ? prompt_fallback(t.icon) : t.s;
		size_t n = t.marker ? strlen(src) : t.n;
		if (o + n >= cap)
			break;
		memcpy(out + o, src, n);
		o += n;
		out[o] = 0;
	}
}

/* Dibuja (draw) o mide la cadena con íconos; después de un ícono, los
 * espacios del texto se reducen a uno. */
static float run(float x, float y, const char *s, float scale, uint32_t rgba, bool shadow, bool draw)
{
	struct prompt_iter it;
	struct prompt_tok t;
	float sz = prompt_icon_size(scale), x0 = x, cy = y + RSXKIT_FONT_H * scale * 0.5f;
	bool after_icon = false;
	char buf[SPAN_MAX];
	prompt_iter_init(&it, s);
	while (prompt_iter_next(&it, &t)) {
		if (t.icon >= 0) {
			if (draw) {
				uint32_t c = rgba & 0xFFu, tint = prompt_table[t.icon].tint;
				if (!shadow) {
					uint32_t m[4];
					for (int k = 0; k < 4; k++)
						m[k] = (tint >> (24 - 8 * k) & 0xFFu) * (k == 3 ? c : 0xFFu) / 0xFFu;
					c = m[0] << 24 | m[1] << 16 | m[2] << 8 | m[3];
				}
				rsxkit_ui_image_part(PROMPTS_SLOT, x, cy - sz * 0.5f, sz, sz, prompt_table[t.icon].x,
						     prompt_table[t.icon].y, PROMPT_CELL, PROMPT_CELL, c);
			}
			x += sz;
			after_icon = true;
			continue;
		}
		const char *p = t.s;
		size_t n = t.n;
		if (after_icon)
			while (n > 1 && *p == ' ' && p[1] == ' ')
				p++, n--;
		after_icon = false;
		span_copy(buf, p, n);
		if (draw)
			rsxkit_ui_text(x, y, buf, scale, rgba, 0);
		x += rsxkit_ui_text_width(buf, scale);
	}
	return x - x0;
}

float prompt_width(const char *s, float scale)
{
	if (!enabled) {
		char buf[512];
		plain(buf, sizeof buf, s);
		return rsxkit_ui_text_width(buf, scale);
	}
	return run(0, 0, s, scale, 0, false, false);
}

float prompt_draw(float x, float y, const char *s, float scale, uint32_t rgba, bool shadow, float max_w)
{
	if (!enabled) {
		char buf[512];
		plain(buf, sizeof buf, s);
		return rsxkit_ui_text_fit(x, y, buf, scale, rgba, max_w);
	}
	if (max_w > 0.0f) {
		float w = run(0, 0, s, scale, 0, false, false);
		if (w > max_w) {
			float k = max_w / w;
			y += RSXKIT_FONT_H * scale * (1.0f - k) * 0.5f;
			scale *= k;
		}
	}
	return run(x, y, s, scale, rgba, shadow, true);
}
