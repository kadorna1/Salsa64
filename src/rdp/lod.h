/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SALSA64_RDP_LOD_H
#define SALSA64_RDP_LOD_H

#include <stdbool.h>
#include <stdint.h>

enum rdp_lod_mode { RDP_LOD_TILE, RDP_LOD_LOD, RDP_LOD_SHARPEN, RDP_LOD_DETAIL };

struct rdp_lod_result {
	uint8_t tile0, tile1, fraction;
	bool magnify, distant;
};

/* Derivadas 16.5 de S y T entre píxeles vecinos, como las usa el RDP. */
struct rdp_lod_result rdp_lod_select(int32_t dsdx, int32_t dtdx, int32_t dsdy, int32_t dtdy,
				     uint8_t max_level, uint8_t min_level, enum rdp_lod_mode mode);

#endif
