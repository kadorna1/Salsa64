/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Salsa64 — Transfer Pak GB ROM/SRAM files and deferred persistence. */
#ifndef SALSA64_FRONTEND_TRANSFERPAKFILE_H
#define SALSA64_FRONTEND_TRANSFERPAKFILE_H

#include "core/pif.h"
#include "core/transferpak.h"
#include "savefile.h"

#define TRANSFERPAKFILE_PATH_MAX 320

struct transferpakfile {
	struct salsa64_transferpak_cart cart;
	struct salsa64_pif *pif;
	savefile_log_fn log;
	unsigned port;
	uint8_t *rom_owned, *ram_owned;
	char rom_path[TRANSFERPAKFILE_PATH_MAX];
	char path[TRANSFERPAKFILE_PATH_MAX];
	char tmp[TRANSFERPAKFILE_PATH_MAX + 4];
	uint64_t retry_us, last_write_us;
	bool failed;
	volatile uint32_t pending;
	uint32_t seen_writes, saved_writes, stage_writes, stage_len;
	uint8_t stage[SALSA64_TRANSFERPAK_RAM_MAX] __attribute__((aligned(16)));
};

/* Uses <n64-rom>.gb/.sav for port 1, and <n64-rom>.p2.gb/.p2.sav etc.
 * for ports 2–4. Missing GB ROMs leave an empty Transfer Pak connected. */
void transferpakfile_open(struct transferpakfile *f, struct salsa64_pif *pif, unsigned port,
			  const char *n64_source, savefile_log_fn log);
void transferpakfile_stage(struct transferpakfile *f, uint64_t now_us);
void transferpakfile_write_staged(struct transferpakfile *f, uint64_t now_us);
/* Call after the emulation thread has stopped; flushes the latest SRAM and
 * disconnects/frees this port's GB cartridge. */
void transferpakfile_close(struct transferpakfile *f);

#endif
