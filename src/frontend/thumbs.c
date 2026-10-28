/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Ver thumbs.h. */
#include "frontend/thumbs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sem.h>
#include <sys/systime.h>
#include <sys/thread.h>

#include <rsxkit/ui.h>

#include "frontend/cover.h"
#include "frontend/thumbs_calc.h"
#include "platform/ps3/log.h"

#define THUMB_PRIO  1600
#define THUMB_STACK (128u << 10)

enum { JOB_IDLE, JOB_RUNNING, JOB_DONE_OK, JOB_DONE_NONE, JOB_DONE_ERR };

static struct {
	bool ready;
	volatile int stop;
	const char *dir;
	struct thumb_cache cache;
	uint32_t frame;
	/* Lo que pidieron las filas en este cuadro: lo más cercano. */
	bool want;
	float want_dist;
	uint32_t want_key;
	char want_name[ROMLIST_NAME_MAX + 1], want_title[ROMLIST_TITLE_MAX + 1], want_code[8];
	/* El trabajo en el hilo. */
	volatile int job;
	uint32_t job_key;
	char name[ROMLIST_NAME_MAX + 1], title[ROMLIST_TITLE_MAX + 1], code[8];
	uint8_t *src;                    /* COVER_RGBA_BYTES, del hilo */
	uint8_t thumb[THUMB_BYTES];      /* la miniatura lista */
	uint64_t us_find, us_read, us_dec, us_scale;  /* buscar, leer el archivo, decodificar y reducir */
	size_t png_bytes;
	unsigned w, h;
	uint32_t uploaded, failed;
	sys_ppu_thread_t tid;
	sys_sem_t sem;
} th;

static inline void lwsync(void) { __asm__ volatile("lwsync" ::: "memory"); }

static void thumb_thread(void *unused)
{
	(void)unused;
	while (!th.stop) {
		sysSemWait(th.sem, 1000000);
		if (th.stop)
			break;
		if (th.job != JOB_RUNNING)
			continue;
		lwsync();
		char path[256];
		int res;
		th.us_find = th.us_read = th.us_dec = th.us_scale = 0;
		th.png_bytes = 0;
		uint64_t t0 = sysGetSystemTime();
		bool found = cover_find(th.dir, th.name, th.title, th.code, path, sizeof path);
		th.us_find = sysGetSystemTime() - t0;
		if (!found) {
			res = JOB_DONE_NONE;
		} else {
			/* Lo que cover_load_png hace junto, por partes para medirlas. */
			size_t n = 0;
			uint8_t *buf = NULL;
			uint64_t t1 = sysGetSystemTime();
			FILE *f = fopen(path, "rb");
			if (f) {
				long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
				rewind(f);
				if (size > 0 && (unsigned long)size <= COVER_MAX_FILE_BYTES && (buf = malloc((size_t)size))) {
					n = fread(buf, 1, (size_t)size, f);
				}
				fclose(f);
			}
			uint64_t t2 = sysGetSystemTime();
			th.us_read = t2 - t1;
			th.png_bytes = n;
			enum cover_result r = n ? cover_decode_png(buf, n, th.src, &th.w, &th.h) : COVER_NO_FILE;
			free(buf);
			uint64_t t3 = sysGetSystemTime();
			th.us_dec = t3 - t2;
			if (r == COVER_OK) {
				thumb_scale(th.src, th.w, th.h, th.thumb);
				th.us_scale = sysGetSystemTime() - t3;
				res = JOB_DONE_OK;
			} else {
				res = JOB_DONE_ERR;
			}
		}
		lwsync();
		th.job = res;
	}
	sysThreadExit(0);
}

bool thumbs_init(const char *dir)
{
	if (th.ready)
		return true;
	memset(&th, 0, sizeof th);
	th.dir = dir;
	thumb_cache_init(&th.cache);
	th.src = malloc(COVER_RGBA_BYTES);
	if (!th.src)
		return false;
	/* El atlas, transparente. */
	uint8_t *zero = calloc(1, (size_t)THUMB_ATLAS_W * THUMB_ATLAS_H * 4);
	int rc = zero ? rsxkit_ui_image_set(THUMBS_SLOT, zero, THUMB_ATLAS_W, THUMB_ATLAS_H) : -1;
	free(zero);
	sys_sem_attr_t attr;
	memset(&attr, 0, sizeof attr);
	attr.attr_protocol = SYS_SEM_ATTR_PROTOCOL;
	attr.attr_pshared = SYS_SEM_ATTR_PSHARED;
	if (rc != RSXKIT_OK || sysSemCreate(&th.sem, &attr, 0, 1) != 0) {
		salsa64_log("miniaturas: no se pudo preparar el atlas (%d)", rc);
		free(th.src);
		th.src = NULL;
		return false;
	}
	if (sysThreadCreate(&th.tid, thumb_thread, NULL, THUMB_PRIO, THUMB_STACK, THREAD_JOINABLE,
			    (char *)"salsa64_thumbs") != 0) {
		salsa64_log("miniaturas: no se pudo crear el hilo");
		sysSemDestroy(th.sem);
		free(th.src);
		th.src = NULL;
		return false;
	}
	th.ready = true;
	salsa64_log("miniaturas: %d casillas de %dx%d en la ranura %d del 2D, hilo de baja prioridad", THUMB_CELLS, THUMB_W,
		    THUMB_H, THUMBS_SLOT);
	return true;
}

bool thumbs_ready(void)
{
	return th.ready;
}

bool thumbs_get(void *user, const struct rom_entry *e, float dist, float rect[4])
{
	(void)user;
	if (!th.ready || e->kind != ROMLIST_ROM)
		return false;
	uint32_t key = thumb_key(romlist_name(e));
	int cell = thumb_cache_find(&th.cache, key, th.frame);
	if (cell >= 0) {
		uint32_t x, y;
		thumb_cell_origin(cell, &x, &y);
		rect[0] = (float)x, rect[1] = (float)y, rect[2] = THUMB_W, rect[3] = THUMB_H;
		return true;
	}
	if (thumb_cache_is_missing(&th.cache, key))
		return false;
	if (!th.want || dist < th.want_dist) {
		th.want = true;
		th.want_dist = dist;
		th.want_key = key;
		snprintf(th.want_name, sizeof th.want_name, "%s", romlist_name(e));
		snprintf(th.want_title, sizeof th.want_title, "%s", romlist_display_title(e));
		snprintf(th.want_code, sizeof th.want_code, "%s", e->code);
	}
	return false;
}

void thumbs_frame(void)
{
	if (!th.ready)
		return;
	th.frame++;
	lwsync();
	int j = th.job;
	if (j >= JOB_DONE_OK) {
		if (j == JOB_DONE_OK) {
			int cell = thumb_cache_assign(&th.cache, th.job_key, th.frame);
			uint32_t x, y;
			thumb_cell_origin(cell, &x, &y);
			int rc = rsxkit_ui_image_update(THUMBS_SLOT, x, y, THUMB_W, THUMB_H, th.thumb);
			if (rc != RSXKIT_OK) {
				th.cache.key[cell] = 0;
				salsa64_log("miniaturas: no se pudo subir la casilla (%d)", rc);
			}
			th.uploaded++;
			salsa64_log("miniaturas: %s: buscar %.1f ms, leer %.1f ms (%zu bytes), decodificar %ux%u %.1f ms, reducir %.1f ms; "
				    "%u subidas, %u aciertos, %u fallos de caché",
				    th.name, (double)th.us_find / 1000.0, (double)th.us_read / 1000.0, th.png_bytes, th.w, th.h,
				    (double)th.us_dec / 1000.0, (double)th.us_scale / 1000.0, th.uploaded, th.cache.hits, th.cache.misses);
		} else if (j == JOB_DONE_NONE) {
			salsa64_log("miniaturas: %s: sin carátula (buscar %.1f ms)", th.name, (double)th.us_find / 1000.0);
			thumb_cache_mark_missing(&th.cache, th.job_key);
		} else {
			th.failed++;
			thumb_cache_mark_missing(&th.cache, th.job_key);
			salsa64_log("miniaturas: %s: la carátula no se pudo leer", th.name);
		}
		th.job = JOB_IDLE;
		j = JOB_IDLE;
	}
	/* Lo pedido antes de que llegara el resultado de esta misma carátula. */
	for (int i = 0; i < THUMB_CELLS && th.want; i++)
		if (th.cache.key[i] == th.want_key)
			th.want = false;
	if (j == JOB_IDLE && th.want) {
		th.job_key = th.want_key;
		memcpy(th.name, th.want_name, sizeof th.name);
		memcpy(th.title, th.want_title, sizeof th.title);
		memcpy(th.code, th.want_code, sizeof th.code);
		lwsync();
		th.job = JOB_RUNNING;
		sysSemPost(th.sem, 1);
	}
	th.want = false;
}

void thumbs_forget_missing(void)
{
	if (th.ready)
		thumb_cache_forget_missing(&th.cache);
}
