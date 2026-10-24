/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Selección de niveles de textura del RDP de Nintendo 64. */
#include "rdp/lod.h"

static uint32_t mag32(int32_t v)
{
	/* El RDP obtiene la magnitud quitando el signo; para negativos queda
	 * una unidad por debajo del valor absoluto. */
	uint32_t u = (uint32_t)v;
	return u ^ (uint32_t)((int32_t)u >> 31);
}

static uint8_t msb(uint32_t v)
{
	uint8_t n = 0;
	while (v >>= 1)
		n++;
	return n;
}

struct rdp_lod_result rdp_lod_select(int32_t dsdx, int32_t dtdx, int32_t dsdy, int32_t dtdy,
				     uint8_t max_level, uint8_t min_level, enum rdp_lod_mode mode)
{
	uint32_t d = mag32(dsdx), v = mag32(dtdx);
	d = v > d ? v : d;
	v = mag32(dsdy); d = v > d ? v : d;
	v = mag32(dtdy); d = v > d ? v : d;
	struct rdp_lod_result r = { 0, 0, 0, false, false };
	if (mode == RDP_LOD_TILE)
		return r;
	r.magnify = d < 32;
	uint8_t level = d >= 32 ? msb(d >> 5) : 0;
	r.distant = d >= 0x4000u || level >= max_level;
	if (r.distant)
		level = max_level;
	if (r.distant && mode == RDP_LOD_LOD) {
		r.fraction = 255;
	} else if (r.magnify) {
		if (mode != RDP_LOD_LOD) {
			uint32_t q = d << 3;
			q = q > min_level ? q : min_level;
			int32_t f = (int32_t)q - (mode == RDP_LOD_SHARPEN ? 256 : 0);
			r.fraction = (uint8_t)f;
		}
	} else {
		r.fraction = (uint8_t)((d << 3) >> level);
	}
	if (mode == RDP_LOD_DETAIL) {
		r.tile0 = (uint8_t)(level + (r.magnify ? 0 : 1));
		r.tile1 = (uint8_t)(level + ((r.magnify || r.distant) ? 1 : 2));
	} else {
		r.tile0 = level;
		r.tile1 = (uint8_t)(level + ((!r.distant && (!r.magnify || mode == RDP_LOD_SHARPEN)) ? 1 : 0));
	}
	return r;
}
