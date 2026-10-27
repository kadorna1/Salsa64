/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal, bounded ZIP reader for N64 ROMs. The central directory supplies
 * sizes even when a local header uses a data descriptor. No archive paths are
 * written to disk: only a single cartridge image is extracted into RAM.
 */
#include "ziprom.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define ZIP_EOCD_SIG 0x06054B50u
#define ZIP_CDIR_SIG 0x02014B50u
#define ZIP_LOCAL_SIG 0x04034B50u
#define ZIP_EOCD_FIXED 22u
#define ZIP_EOCD_MAX (ZIP_EOCD_FIXED + 65535u)
#define ZIP_CDIR_FIXED 46u
#define ZIP_LOCAL_FIXED 30u
#define ZIP_CDIR_MAX (16u << 20)
#define ZIP_ENTRIES_MAX 4096u
#define ZIP_IO_CHUNK 16384u
#define ZIP_ENCRYPTED_FLAGS (1u | 0x40u | 0x2000u)

struct ziprom_entry {
	uint32_t compressed_size;
	uint32_t uncompressed_size;
	uint32_t crc;
	uint32_t local_offset;
	uint16_t method;
	uint16_t flags;
	uint16_t name_len;
	char name[256];
	long data_offset;
};

static uint16_t zip_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t zip_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned zip_lower(unsigned c)
{
	return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static bool zip_suffix(const char *name, size_t len, const char *suffix)
{
	size_t n = strlen(suffix);
	if (len < n)
		return false;
	for (size_t i = 0; i < n; i++)
		if (zip_lower((unsigned char)name[len - n + i]) != (unsigned char)suffix[i])
			return false;
	return true;
}

static bool zip_name_is_rom(const char *name, size_t len)
{
	if (!len || name[len - 1] == '/' || name[len - 1] == '\\')
		return false;
	return zip_suffix(name, len, ".z64") || zip_suffix(name, len, ".v64") || zip_suffix(name, len, ".n64");
}

bool salsa64_ziprom_is_path(const char *path)
{
	if (!path)
		return false;
	return zip_suffix(path, strlen(path), ".zip");
}

/* Find the first supported N64 entry in the central directory and resolve
 * its local-data offset. ZIP64 and multi-disk archives are intentionally not
 * accepted; N64 ROMs are capped at 64 MiB. */
static int zip_open_entry(const char *path, FILE **file_out, struct ziprom_entry *entry)
{
	FILE *f = NULL;
	long file_len, eocd_abs;
	size_t tail_len;
	uint8_t *tail = NULL;
	int result = -1;

	if (!path || !file_out || !entry)
		return -1;
	*file_out = NULL;
	f = fopen(path, "rb");
	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0 || (file_len = ftell(f)) < (long)ZIP_EOCD_FIXED)
		goto done;
	tail_len = (size_t)(file_len < (long)ZIP_EOCD_MAX ? file_len : (long)ZIP_EOCD_MAX);
	tail = (uint8_t *)malloc(tail_len);
	if (!tail || fseek(f, file_len - (long)tail_len, SEEK_SET) != 0 ||
	    fread(tail, 1, tail_len, f) != tail_len)
		goto done;

	size_t eocd_i = tail_len;
	for (size_t i = tail_len - ZIP_EOCD_FIXED + 1; i-- > 0;) {
		if (zip_le32(tail + i) != ZIP_EOCD_SIG)
			continue;
		uint16_t comment_len = zip_le16(tail + i + 20);
		if (i + ZIP_EOCD_FIXED + comment_len == tail_len) {
			eocd_i = i;
			break;
		}
	}
	if (eocd_i == tail_len)
		goto done;

	const uint8_t *eocd = tail + eocd_i;
	uint16_t disk = zip_le16(eocd + 4);
	uint16_t cd_disk = zip_le16(eocd + 6);
	uint16_t entries_disk = zip_le16(eocd + 8);
	uint16_t entries_total = zip_le16(eocd + 10);
	uint32_t cd_size = zip_le32(eocd + 12);
	uint32_t cd_offset = zip_le32(eocd + 16);
	eocd_abs = file_len - (long)tail_len + (long)eocd_i;

	if (disk || cd_disk || entries_disk != entries_total || !entries_total ||
	    entries_total > ZIP_ENTRIES_MAX || cd_size > ZIP_CDIR_MAX ||
	    (uint64_t)cd_offset + cd_size > (uint64_t)eocd_abs)
		goto done;

	if (fseek(f, (long)cd_offset, SEEK_SET) != 0)
		goto done;
	for (uint32_t i = 0; i < entries_total; i++) {
		uint8_t ch[ZIP_CDIR_FIXED];
		long current = ftell(f);
		uint16_t name_len, extra_len, comment_len, disk_start;
		char name[256];
		bool candidate;
		if (current < 0 || (uint64_t)current + ZIP_CDIR_FIXED > (uint64_t)cd_offset + cd_size ||
		    fread(ch, 1, sizeof ch, f) != sizeof ch || zip_le32(ch) != ZIP_CDIR_SIG)
			goto done;
		name_len = zip_le16(ch + 28);
		extra_len = zip_le16(ch + 30);
		comment_len = zip_le16(ch + 32);
		disk_start = zip_le16(ch + 34);
		if ((uint64_t)current + ZIP_CDIR_FIXED + name_len + extra_len + comment_len >
		    (uint64_t)cd_offset + cd_size)
			goto done;
		name[0] = '\0';
		if (name_len <= sizeof name - 1) {
			if (fread(name, 1, name_len, f) != name_len)
				goto done;
			name[name_len] = '\0';
		} else if (fseek(f, current + ZIP_CDIR_FIXED + name_len, SEEK_SET) != 0) {
			goto done;
		}
		candidate = name_len <= sizeof name - 1 && zip_name_is_rom(name, name_len);
		if (candidate && !disk_start && !(zip_le16(ch + 8) & ZIP_ENCRYPTED_FLAGS)) {
			uint16_t flags = zip_le16(ch + 8);
			uint16_t method = zip_le16(ch + 10);
			uint32_t compressed = zip_le32(ch + 20);
			uint32_t uncompressed = zip_le32(ch + 24);
			uint32_t local_offset = zip_le32(ch + 42);
			if ((method == 0 || method == 8) && uncompressed >= 0x1000u &&
			    uncompressed <= SALSA64_ZIPROM_MAX_SIZE &&
			    compressed <= SALSA64_ZIPROM_MAX_SIZE && local_offset != 0xFFFFFFFFu &&
			    compressed != 0 && (method != 0 || compressed == uncompressed)) {
				memset(entry, 0, sizeof *entry);
				entry->compressed_size = compressed;
				entry->uncompressed_size = uncompressed;
				entry->crc = zip_le32(ch + 16);
				entry->local_offset = local_offset;
				entry->method = method;
				entry->flags = flags;
				entry->name_len = name_len;
				memcpy(entry->name, name, name_len + 1);
				result = 0;
				break;
			}
		}
		if (fseek(f, current + ZIP_CDIR_FIXED + (long)name_len + extra_len + comment_len, SEEK_SET) != 0)
			goto done;
	}
	if (result != 0)
		goto done;

	/* Resolve the data location through the local header. Do not trust its
	 * size fields: bit 3 permits data descriptors, so central-directory sizes
	 * are authoritative. */
	{
		uint8_t lh[ZIP_LOCAL_FIXED];
		uint16_t local_name_len, local_extra_len;
		uint64_t data_offset, data_end;
		if ((uint64_t)entry->local_offset + ZIP_LOCAL_FIXED > (uint64_t)file_len ||
		    fseek(f, (long)entry->local_offset, SEEK_SET) != 0 ||
		    fread(lh, 1, sizeof lh, f) != sizeof lh || zip_le32(lh) != ZIP_LOCAL_SIG ||
		    zip_le16(lh + 8) != entry->method || (zip_le16(lh + 6) & ZIP_ENCRYPTED_FLAGS))
			goto done;
		local_name_len = zip_le16(lh + 26);
		local_extra_len = zip_le16(lh + 28);
		data_offset = (uint64_t)entry->local_offset + ZIP_LOCAL_FIXED + local_name_len + local_extra_len;
		data_end = data_offset + entry->compressed_size;
		if (data_end > (uint64_t)file_len || data_offset > LONG_MAX ||
		    fseek(f, (long)data_offset, SEEK_SET) != 0)
			goto done;
		entry->data_offset = (long)data_offset;
	}
	free(tail);
	*file_out = f;
	return 0;

done:
	free(tail);
	if (f)
		fclose(f);
	return -1;
}

static void zip_copy_entry_name(const struct ziprom_entry *entry, char *dst, size_t size)
{
	if (!dst || !size)
		return;
	size_t n = strlen(entry->name);
	if (n >= size)
		n = size - 1;
	memcpy(dst, entry->name, n);
	dst[n] = '\0';
}

/* Read 64 bytes for a menu peek, or a complete image. A full extraction must
 * reach the end of the Deflate stream and pass the stored CRC-32. */
static int zip_read_data(FILE *f, const struct ziprom_entry *entry, uint8_t *dst,
                         uint32_t out_cap, bool full)
{
	uint8_t in[ZIP_IO_CHUNK];
	uint32_t remaining = entry->compressed_size;
	uint32_t expected = full ? entry->uncompressed_size : SALSA64_ROM_HEADER_SIZE;
	uLong crc = crc32(0L, Z_NULL, 0);

	if (!f || !entry || !dst || out_cap < expected || fseek(f, entry->data_offset, SEEK_SET) != 0)
		return -1;
	if (entry->method == 0) {
		uint32_t done = 0;
		while (done < expected) {
			uint32_t chunk = expected - done;
			if (chunk > ZIP_IO_CHUNK)
				chunk = ZIP_IO_CHUNK;
			if (chunk > remaining)
				return -1;
			if (fread(dst + done, 1, chunk, f) != chunk)
				return -1;
			if (full)
				crc = crc32(crc, dst + done, chunk);
			done += chunk;
			remaining -= chunk;
		}
		if (full && (remaining || (uint32_t)crc != entry->crc))
			return -1;
		return 0;
	}
	if (entry->method != 8)
		return -1;

	z_stream zs;
	uint8_t overflow_byte;
	memset(&zs, 0, sizeof zs);
	if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
		return -1;
	int ret = Z_OK;
	while (ret != Z_STREAM_END) {
		if (!zs.avail_in) {
			if (!remaining) {
				ret = Z_DATA_ERROR;
				break;
			}
			uInt n = remaining > sizeof in ? (uInt)sizeof in : (uInt)remaining;
			if (fread(in, 1, n, f) != n) {
				ret = Z_DATA_ERROR;
				break;
			}
			remaining -= n;
			zs.next_in = in;
			zs.avail_in = n;
		}
		uint32_t produced = (uint32_t)zs.total_out;
		if (produced < expected) {
			zs.next_out = dst + produced;
			zs.avail_out = (uInt)(expected - produced);
		} else {
			/* Allow zlib to consume an exact-length end marker without giving
			 * it access to memory after the expected output. Any extra output
			 * lands in this byte and makes the final size check fail. */
			zs.next_out = &overflow_byte;
			zs.avail_out = 1;
		}
		uLong before_in = zs.total_in, before_out = zs.total_out;
		ret = inflate(&zs, Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END)
			break;
		if (zs.total_in == before_in && zs.total_out == before_out) {
			ret = Z_DATA_ERROR;
			break;
		}
		if (!full && zs.total_out >= SALSA64_ROM_HEADER_SIZE)
			break;
		if (full && zs.total_out > entry->uncompressed_size) {
			ret = Z_DATA_ERROR;
			break;
		}
	}
	bool ok;
	if (full) {
		ok = ret == Z_STREAM_END && zs.total_out == entry->uncompressed_size &&
		     remaining == 0 && zs.avail_in == 0;
		if (ok) {
			uint32_t done = 0;
			while (done < entry->uncompressed_size) {
				uint32_t chunk = entry->uncompressed_size - done;
				if (chunk > ZIP_IO_CHUNK)
					chunk = ZIP_IO_CHUNK;
				crc = crc32(crc, dst + done, chunk);
				done += chunk;
			}
			ok = (uint32_t)crc == entry->crc;
		}
	} else {
		ok = zs.total_out == SALSA64_ROM_HEADER_SIZE &&
		     (ret == Z_OK || ret == Z_STREAM_END);
	}
	inflateEnd(&zs);
	return ok ? 0 : -1;
}

int salsa64_ziprom_read_header(const char *path, uint8_t header[SALSA64_ROM_HEADER_SIZE],
                               uint32_t *rom_size, char *entry_name, size_t entry_name_size)
{
	FILE *f = NULL;
	struct ziprom_entry entry;
	int rc;
	if (!header || !rom_size || zip_open_entry(path, &f, &entry) != 0)
		return -1;
	rc = zip_read_data(f, &entry, header, SALSA64_ROM_HEADER_SIZE, false);
	fclose(f);
	if (rc != 0)
		return -1;
	*rom_size = entry.uncompressed_size;
	zip_copy_entry_name(&entry, entry_name, entry_name_size);
	return 0;
}

int salsa64_ziprom_extract(const char *path, uint8_t *dst, uint32_t dst_cap,
                           uint32_t *rom_size, char *entry_name, size_t entry_name_size)
{
	FILE *f = NULL;
	struct ziprom_entry entry;
	int rc;
	if (!dst || !rom_size || zip_open_entry(path, &f, &entry) != 0)
		return -1;
	if (entry.uncompressed_size > dst_cap) {
		fclose(f);
		return -1;
	}
	rc = zip_read_data(f, &entry, dst, dst_cap, true);
	fclose(f);
	if (rc != 0)
		return -1;
	*rom_size = entry.uncompressed_size;
	zip_copy_entry_name(&entry, entry_name, entry_name_size);
	return 0;
}
