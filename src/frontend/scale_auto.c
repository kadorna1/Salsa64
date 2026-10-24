/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Salsa64 — política portable de escala interna automática. */
#include "scale_auto.h"

#include <string.h>

static uint64_t sat_add(uint64_t a, uint64_t b, uint64_t limit)
{
	return a >= limit || b >= limit - a ? limit : a + b;
}

void scale_auto_init(struct scale_auto *p, bool automatic, unsigned current, unsigned maximum)
{
	memset(p, 0, sizeof *p);
	p->automatic = automatic;
	p->maximum = maximum < 1 ? 1 : maximum > 5 ? 5 : (uint8_t)maximum;
	p->current = current < 1 ? 1 : current > p->maximum ? p->maximum : (uint8_t)current;
}

struct scale_auto_result scale_auto_update(struct scale_auto *p, const struct scale_auto_sample *s)
{
	struct scale_auto_result r = { SCALE_AUTO_NONE, 0, 0, 0 };
	if (!s || !s->duration_us || !s->period_ns)
		return r;

	uint64_t duration_ns = s->duration_us * 1000ull;
	r.emu_pm = duration_ns ? (uint32_t)((uint64_t)s->fields * s->period_ns * 1000ull / duration_ns) : 0;
	r.rsx_pm = s->fields ? (uint32_t)(s->rsx_ns * 1000ull / ((uint64_t)s->fields * s->period_ns)) : 0;
	bool emulation_ready = r.emu_pm >= SCALE_AUTO_EMU_PM;
	bool overloaded = emulation_ready && s->skipped && r.rsx_pm >= 1000;
	bool margin = emulation_ready && !s->skipped && s->presented && r.rsx_pm < SCALE_AUTO_UP_RSX_PM;

	p->cooldown_us = p->cooldown_us > s->duration_us ? p->cooldown_us - s->duration_us : 0;
	if (overloaded) {
		p->overload_us = sat_add(p->overload_us, s->duration_us, SCALE_AUTO_DOWN_US);
		p->margin_us = 0;
	} else if (margin) {
		p->margin_us = sat_add(p->margin_us, s->duration_us, SCALE_AUTO_UP_US);
		p->overload_us = 0;
	} else {
		p->overload_us = p->margin_us = 0;
	}

	if (p->automatic && p->overload_us >= SCALE_AUTO_DOWN_US && !p->cooldown_us && p->current > 1) {
		r.action = SCALE_AUTO_DOWN;
		r.sustained_us = p->overload_us;
		p->current--;
		p->overload_us = p->margin_us = 0;
		p->cooldown_us = SCALE_AUTO_COOLDOWN_US;
	} else if (p->automatic && p->margin_us >= SCALE_AUTO_UP_US && !p->cooldown_us && p->current < p->maximum) {
		r.action = SCALE_AUTO_UP;
		r.sustained_us = p->margin_us;
		p->current++;
		p->overload_us = p->margin_us = 0;
		p->cooldown_us = SCALE_AUTO_COOLDOWN_US;
	} else if (!p->automatic && p->overload_us >= SCALE_AUTO_DOWN_US && !p->cooldown_us && p->current > 1) {
		r.action = SCALE_AUTO_WARN;
		r.sustained_us = p->overload_us;
		p->overload_us = 0;
		p->cooldown_us = SCALE_AUTO_WARN_US;
	}
	return r;
}
