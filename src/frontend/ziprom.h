/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SALSA64_FRONTEND_ZIPROM_H
#define SALSA64_FRONTEND_ZIPROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/rom.h"

/* ZIP archives are containers, not ROM data. A supported archive has one
 * usable .z64/.v64/.n64 entry, uses STORE or DEFLATE, and is not encrypted
 * or ZIP64. Extraction is bounded by SALSA64_ZIPROM_MAX_SIZE and verifies
 * the uncompressed size and CRC-32 before a game is started. */
#define SALSA64_ZIPROM_MAX_SIZE (64u << 20)
#define SALSA64_ZIPROM_MIN_SIZE 22u

bool salsa64_ziprom_is_path(const char *path);

/* Read only the cartridge header so the ROM browser can show its game title
 * without allocating/decompressing the entire ROM. Returns 0 on success. */
int salsa64_ziprom_read_header(const char *path,
                               uint8_t header[SALSA64_ROM_HEADER_SIZE],
                               uint32_t *rom_size,
                               char *entry_name, size_t entry_name_size);

/* Extract and validate the selected ROM into caller-owned memory. dst_cap
 * must fit the uncompressed ROM. Returns 0 only after the ZIP CRC checks. */
int salsa64_ziprom_extract(const char *path, uint8_t *dst, uint32_t dst_cap,
                           uint32_t *rom_size,
                           char *entry_name, size_t entry_name_size);

#endif
