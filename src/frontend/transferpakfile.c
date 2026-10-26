/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Salsa64 — Transfer Pak GB ROM and save-file handling. */
#include "transferpakfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#define TRANSFERPAK_QUIET_US 2000000u
#define TRANSFERPAK_RETRY_US 10000000u

static bool path_for(char *dst, size_t n, const char *source, unsigned port, const char *ext)
{
	int wrote;
	if (port == 0)
		wrote = snprintf(dst, n, "%s%s", source, ext);
	else
		wrote = snprintf(dst, n, "%s.p%u%s", source, port + 1, ext);
	return wrote > 0 && (size_t)wrote < n;
}

static bool load_whole_file(const char *path, uint8_t **data, uint32_t *size, uint32_t max_size)
{
	FILE *in = fopen(path, "rb");
	if (!in)
		return false;
	if (fseek(in, 0, SEEK_END) != 0) {
		fclose(in);
		return false;
	}
	long len = ftell(in);
	if (len < 0 || (uint32_t)len > max_size || fseek(in, 0, SEEK_SET) != 0) {
		fclose(in);
		return false;
	}
	uint8_t *p = (uint8_t *)malloc(len ? (size_t)len : 1u);
	if (!p) {
		fclose(in);
		return false;
	}
	bool ok = fread(p, 1, (size_t)len, in) == (size_t)len;
	fclose(in);
	if (!ok) {
		free(p);
		return false;
	}
	*data = p;
	*size = (uint32_t)len;
	return true;
}

static long read_save(const char *path, uint8_t *data, uint32_t capacity)
{
	FILE *in = fopen(path, "rb");
	if (!in)
		return -1;
	size_t n = fread(data, 1, capacity, in);
	fclose(in);
	return (long)n;
}

static bool write_file(const char *path, const uint8_t *data, uint32_t len)
{
	FILE *out = fopen(path, "wb");
	if (!out)
		return false;
	bool ok = fwrite(data, 1, len, out) == len;
	return fclose(out) == 0 && ok;
}

static bool write_save(struct transferpakfile *f, const uint8_t *data, uint32_t len, uint32_t writes,
		       const char *why)
{
	if (!f->path[0] || !len)
		return true;
	bool ok = write_file(f->tmp, data, len);
	if (ok && rename(f->tmp, f->path) != 0) {
		/* Some filesystems may refuse replacement; retain the fallback,
		 * but do not remove the previous save until the temporary file exists. */
		ok = write_file(f->path, data, len);
		if (ok)
			remove(f->tmp);
	}
	if (!ok) {
		if (!f->failed)
			f->log("Transfer Pak: no se pudo escribir %s; se reintenta", f->path);
		f->failed = true;
		return false;
	}
	f->failed = false;
	f->log("Transfer Pak: guardado %s (%u bytes, %u escrituras) %s", why, len, writes, f->path);
	return true;
}

void transferpakfile_open(struct transferpakfile *f, struct salsa64_pif *pif, unsigned port,
			  const char *source, savefile_log_fn log)
{
	memset(f, 0, sizeof *f);
	f->pif = pif;
	f->port = port;
	f->log = log;
	if (port >= SALSA64_PIF_PORTS || !pif)
		return;
	/* Attach even if no GB cart was found: the N64 must still be able to
	 * read the Transfer Pak's power/status registers and report no cart. */
	pif->transferpak[port] = &f->cart;
	if (!source || !source[0] || !log)
		return;
	FILE *probe = fopen(source, "rb");
	if (!probe)
		return;
	fclose(probe);

	uint32_t rom_size = 0;
	if (!path_for(f->rom_path, sizeof f->rom_path, source, port, ".gb") ||
	    !load_whole_file(f->rom_path, &f->rom_owned, &rom_size, SALSA64_TRANSFERPAK_ROM_MAX)) {
		if (!path_for(f->rom_path, sizeof f->rom_path, source, port, ".gbc") ||
		    !load_whole_file(f->rom_path, &f->rom_owned, &rom_size, SALSA64_TRANSFERPAK_ROM_MAX))
			return;
	}
	if (rom_size < 0x8000 || (rom_size & 0x3FFFu)) {
		f->log("Transfer Pak puerto %u: ROM GB inválida o tamaño no múltiplo de 16 KiB: %s",
		       port + 1, f->rom_path);
		free(f->rom_owned);
		f->rom_owned = NULL;
		return;
	}
	uint32_t ram_size = salsa64_transferpak_ram_size(f->rom_owned, rom_size);
	if (ram_size > SALSA64_TRANSFERPAK_RAM_MAX) {
		f->log("Transfer Pak puerto %u: RAM GB demasiado grande: %s", port + 1, f->rom_path);
		free(f->rom_owned);
		f->rom_owned = NULL;
		return;
	}
	if (ram_size) {
		f->ram_owned = (uint8_t *)malloc(ram_size);
		if (!f->ram_owned) {
			f->log("Transfer Pak puerto %u: no hay memoria para SRAM GB (%u bytes)", port + 1, ram_size);
			free(f->rom_owned);
			f->rom_owned = NULL;
			return;
		}
		memset(f->ram_owned, 0xFF, ram_size);
		if (!path_for(f->path, sizeof f->path, source, port, ".sav")) {
			f->path[0] = '\0';
		} else {
			long n = read_save(f->path, f->ram_owned, ram_size);
			if (n >= 0)
				f->log("Transfer Pak puerto %u: leído guardado GB (%ld/%u bytes) %s",
				       port + 1, n, ram_size, f->path);
		}
		snprintf(f->tmp, sizeof f->tmp, "%s.tmp", f->path);
	}
	if (!salsa64_transferpak_cart_init(&f->cart, f->rom_owned, rom_size, f->ram_owned, ram_size)) {
		f->log("Transfer Pak puerto %u: cartucho GB no compatible (tipo 0x%02X): %s",
		       port + 1, f->rom_owned[0x147], f->rom_path);
		free(f->ram_owned);
		free(f->rom_owned);
		f->ram_owned = f->rom_owned = NULL;
		memset(&f->cart, 0, sizeof f->cart);
		return;
	}
	f->log("Transfer Pak puerto %u: ROM GB cargada (%u KiB, tipo 0x%02X, SRAM %u bytes): %s",
	       port + 1, rom_size >> 10, f->cart.type, ram_size, f->rom_path);
}

void transferpakfile_stage(struct transferpakfile *f, uint64_t now_us)
{
	if (!f || !f->cart.ram || !f->cart.ram_size || !f->path[0])
		return;
	if (f->cart.writes != f->seen_writes) {
		f->seen_writes = f->cart.writes;
		f->last_write_us = now_us;
	}
	if (f->pending || !f->cart.dirty || now_us < f->last_write_us ||
	    now_us - f->last_write_us < TRANSFERPAK_QUIET_US)
		return;
	memcpy(f->stage, f->cart.ram, f->cart.ram_size);
	f->stage_len = f->cart.ram_size;
	f->stage_writes = f->cart.writes - f->saved_writes;
	f->cart.dirty = 0;
	f->saved_writes = f->cart.writes;
	__sync_synchronize();
	f->pending = 1;
}

void transferpakfile_write_staged(struct transferpakfile *f, uint64_t now_us)
{
	if (!f || !f->pending || now_us < f->retry_us)
		return;
	__sync_synchronize();
	if (!write_save(f, f->stage, f->stage_len, f->stage_writes, "diferido")) {
		f->retry_us = now_us + TRANSFERPAK_RETRY_US;
		return;
	}
	f->retry_us = 0;
	__sync_synchronize();
	f->pending = 0;
}

void transferpakfile_close(struct transferpakfile *f)
{
	if (!f)
		return;
	/* The game/worker is stopped before close. Prefer current RAM to any older
	 * staged copy, so writes after the last stage are not lost. */
	if (f->cart.ram && f->cart.ram_size && (f->cart.dirty || f->pending) && f->path[0]) {
		(void)write_save(f, f->cart.ram, f->cart.ram_size, f->cart.writes - f->saved_writes, "al salir");
		f->cart.dirty = 0;
	}
	if (f->pif && f->port < SALSA64_PIF_PORTS && f->pif->transferpak[f->port] == &f->cart)
		f->pif->transferpak[f->port] = NULL;
	free(f->ram_owned);
	free(f->rom_owned);
	memset(f, 0, sizeof *f);
}
