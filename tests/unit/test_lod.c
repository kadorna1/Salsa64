/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "test.h"
#include "rdp/lod.h"

static void lod_levels(void)
{
	struct rdp_lod_result r = rdp_lod_select(16, 0, 0, 0, 4, 0, RDP_LOD_LOD);
	CHECK(r.magnify); CHECK_EQ(r.tile0, 0); CHECK_EQ(r.tile1, 0); CHECK_EQ(r.fraction, 0);
	r = rdp_lod_select(32, 0, 0, 0, 4, 0, RDP_LOD_LOD);
	CHECK(!r.magnify); CHECK_EQ(r.tile0, 0); CHECK_EQ(r.tile1, 1); CHECK_EQ(r.fraction, 0);
	r = rdp_lod_select(48, 0, 0, 0, 4, 0, RDP_LOD_LOD);
	CHECK_EQ(r.tile0, 0); CHECK_EQ(r.tile1, 1); CHECK_EQ(r.fraction, 128);
	r = rdp_lod_select(128, 0, 0, 0, 2, 0, RDP_LOD_LOD);
	CHECK(r.distant); CHECK_EQ(r.tile0, 2); CHECK_EQ(r.tile1, 2); CHECK_EQ(r.fraction, 255);
}

static void lod_detail_sharpen(void)
{
	struct rdp_lod_result r = rdp_lod_select(8, 0, 0, 0, 5, 128, RDP_LOD_DETAIL);
	CHECK_EQ(r.tile0, 0); CHECK_EQ(r.tile1, 1); CHECK_EQ(r.fraction, 128);
	r = rdp_lod_select(4, 0, 0, 0, 5, 96, RDP_LOD_DETAIL);
	CHECK_EQ(r.fraction, 96);
	r = rdp_lod_select(16, 0, 0, 0, 5, 0, RDP_LOD_SHARPEN);
	CHECK_EQ(r.tile0, 0); CHECK_EQ(r.tile1, 1); CHECK_EQ(r.fraction, 128);
	r = rdp_lod_select(-49, 0, 0, 0, 5, 0, RDP_LOD_LOD);
	CHECK_EQ(r.fraction, 128); /* magnitud RDP: -49 pasa a 48 */
}

void t_suite_lod(void)
{
	RUN(lod_levels);
	RUN(lod_detail_sharpen);
}
