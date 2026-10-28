/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Ver thumbs.h. */
#include "frontend/thumbs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/systime.h>
#include <sys/thread.h>

#include <rsxkit/ui.h>

#include "frontend/cover.h"
#include "frontend/thumbs_calc.h"
#include "platform/ps3/log.h"

#define THUMB_PRIO  1600
#define THUMB_STACK (128u << 10)

/* RUNNING lo hace el hilo; DONE_* lo recoge thumbs_frame; BIGWAIT: la carátula
 * completa está en `src` esperando a thumbs_big_poll. */
enum { JOB_IDLE, JOB_RUNNING, JOB_DONE_OK, JOB_DONE_NONE, JOB_DONE_ERR, JOB_BIGWAIT };
enum { SRC_NONE, SRC_DISK, SRC_PNG };

struct request {
	bool set;
	uint32_t key;
	char name[ROMLIST_NAME_MAX + 1], title[ROMLIST_TITLE_MAX + 1], code[8];
};

static struct {
	bool ready;
	volatile int stop;
	const char *dir;
	struct thumb_cache cache;
	uint8_t px[THUMB_CELLS][THUMB_BYTES];  /* copias de las casillas del atlas */
	uint32_t frame;
	/* Lo que pidieron las filas en este cuadro: lo más cercano. */
	struct request want;
	float want_dist;
	/* La carátula completa pedida. */
	struct request big_want;
	int big_token;
	/* El trabajo en el hilo. */
	volatile int job;
	bool job_big;
	int job_token;
	struct request req;
	volatile int src;                /* de dónde salió el resultado (SRC_*) */
	uint8_t *srcbuf;                 /* COVER_RGBA_BYTES, del hilo: la carátula decodificada */
	uint8_t thumb[THUMB_BYTES];      /* la miniatura lista */
	uint8_t fbuf[THUMB_FILE_HEADER + THUMB_BYTES];  /* la caché en disco */
	uint64_t us_find, us_read, us_dec, us_scale, us_disk;
	size_t png_bytes;
	unsigned w, h;
	uint32_t uploaded, failed, disk_hits, decoded, rows_found, rows_missing;
	bool mkdir_done;
	sys_ppu_thread_t tid;
	sys_sem_t sem;
} th;

static inline void lwsync(void) { __asm__ volatile("lwsync" ::: "memory"); }

static uint64_t now_us(void)
{
	return sysGetSystemTime();
}

/* covers/.mini/<nombre sin extensión>.mini */
static bool mini_path(const char *name, char *out, size_t size)
{
	const char *dot = strrchr(name, '.');
	size_t stem = dot && dot > name ? (size_t)(dot - name) : strlen(name);
	if (stem == 0 || stem > 200)
		return false;
	int n = snprintf(out, size, "%s/.mini/", th.dir);
	if (n < 0 || (size_t)n + stem + 6 >= size)
		return false;
	for (size_t i = 0; i < stem; i++)
		out[n + i] = name[i] == '/' || name[i] == '\\' ? '_' : name[i];
	memcpy(out + n + stem, ".mini", 6);
	return true;
}

static bool stat_png(const char *path, uint32_t *size, uint32_t *mtime)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return false;
	*size = (uint32_t)st.st_size;
	*mtime = (uint32_t)st.st_mtime;
	return true;
}

static void write_cache(const char *cpath, const struct thumb_file *f, const uint8_t *px)
{
	if (!th.mkdir_done) {
		char d[300];
		snprintf(d, sizeof d, "%s/.mini", th.dir);
		mkdir(d, 0777);
		th.mkdir_done = true;
	}
	size_t n = thumb_file_pack(th.fbuf, f, px);
	if (!n)
		return;
	FILE *o = fopen(cpath, "wb");
	if (!o)
		return;
	fwrite(th.fbuf, 1, n, o);
	fclose(o);
}

/* Lee la caché en disco. 1: miniatura válida en th.thumb (y `png` con la ruta del
 * PNG); 2: «sin carátula» vigente; 0: no hay o ya no vale. */
static int read_cache(const char *cpath, char *png)
{
	FILE *f = fopen(cpath, "rb");
	if (!f)
		return 0;
	size_t n = fread(th.fbuf, 1, sizeof th.fbuf, f);
	fclose(f);
	struct thumb_file tf;
	const uint8_t *px;
	if (thumb_file_unpack(th.fbuf, n, &tf, &px) != 0)
		return 0;
	uint32_t size, mtime;
	if (tf.kind == THUMB_FILE_NONE) {
		/* La carpeta de carátulas no cambió desde que se buscó. */
		return stat_png(th.dir, &size, &mtime) && mtime == tf.png_time ? 2 : 0;
	}
	if (!stat_png(tf.path, &size, &mtime) || size != tf.png_size || mtime != tf.png_time)
		return 0;
	memcpy(th.thumb, px, THUMB_BYTES);
	snprintf(png, 256, "%s", tf.path);
	return 1;
}

static void run_job(void)
{
	char cpath[300], png[256] = "";
	bool have_cpath = mini_path(th.req.name, cpath, sizeof cpath);
	th.us_find = th.us_read = th.us_dec = th.us_scale = th.us_disk = 0;
	th.png_bytes = 0;
	th.src = SRC_NONE;
	th.w = th.h = 0;
	int res = JOB_DONE_ERR;
	bool thumb_ready = false, known_path = false;
	uint64_t t0 = now_us();
	if (have_cpath) {
		int c = read_cache(cpath, png);
		th.us_disk = now_us() - t0;
		if (c == 2) {
			th.src = SRC_DISK;
			th.job = JOB_DONE_NONE;
			return;
		}
		if (c == 1) {
			thumb_ready = true;
			known_path = true;
			if (!th.job_big) {  /* la miniatura basta */
				th.src = SRC_DISK;
				th.job = JOB_DONE_OK;
				return;
			}
		}
	}
	/* Buscar el PNG (la carátula completa con la ruta de la caché no necesita buscar). */
	bool found = known_path;
	if (!found) {
		uint64_t t1 = now_us();
		found = cover_find(th.dir, th.req.name, th.req.title, th.req.code, png, sizeof png);
		th.us_find = now_us() - t1;
	}
	uint32_t psize = 0, pmtime = 0;
	if (!found) {
		uint32_t dsize;
		if (have_cpath && stat_png(th.dir, &dsize, &pmtime)) {
			struct thumb_file tf = { .kind = THUMB_FILE_NONE, .png_size = 0, .png_time = pmtime };
			write_cache(cpath, &tf, NULL);
		}
		th.src = SRC_PNG;
		th.job = JOB_DONE_NONE;
		return;
	}
	/* Leer el archivo y decodificarlo (lo que cover_load_png hace junto, por partes para medirlo). */
	size_t n = 0;
	uint8_t *buf = NULL;
	uint64_t t2 = now_us();
	FILE *f = fopen(png, "rb");
	if (f) {
		long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
		rewind(f);
		if (size > 0 && (unsigned long)size <= COVER_MAX_FILE_BYTES && (buf = malloc((size_t)size)))
			n = fread(buf, 1, (size_t)size, f);
		fclose(f);
	}
	uint64_t t3 = now_us();
	th.us_read = t3 - t2;
	th.png_bytes = n;
	/* La primera carátula de cada arranque se decodifica con puff y con zlib, para medir la diferencia. */
	static bool compared;
	if (n && !compared) {
		compared = true;
		unsigned pw, ph;
		uint64_t tp = now_us();
		cover_use_puff(true);
		enum cover_result rp = cover_decode_png(buf, n, th.srcbuf, &pw, &ph);
		uint64_t tpe = now_us();
		cover_use_puff(false);
		enum cover_result rz = cover_decode_png(buf, n, th.srcbuf, &pw, &ph);
		uint64_t tze = now_us();
		salsa64_log("miniaturas: decodificar %s (%ux%u): puff %.1f ms (%s), zlib %.1f ms (%s)", th.req.name, pw, ph,
			    (double)(tpe - tp) / 1000.0, cover_result_name(rp), (double)(tze - tpe) / 1000.0, cover_result_name(rz));
	}
	uint64_t t3b = now_us();
	enum cover_result r = n ? cover_decode_png(buf, n, th.srcbuf, &th.w, &th.h) : COVER_NO_FILE;
	free(buf);
	uint64_t t4 = now_us();
	t3 = t3b;
	th.us_dec = t4 - t3;
	if (r == COVER_OK) {
		if (!thumb_ready) {
			thumb_scale(th.srcbuf, th.w, th.h, th.thumb);
			th.us_scale = now_us() - t4;
			if (have_cpath && stat_png(png, &psize, &pmtime)) {
				struct thumb_file tf = { .kind = THUMB_FILE_OK, .png_size = psize, .png_time = pmtime };
				snprintf(tf.path, sizeof tf.path, "%s", png);
				write_cache(cpath, &tf, th.thumb);
			}
		}
		th.src = SRC_PNG;
		res = JOB_DONE_OK;
	}
	th.job = res;
}

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
		run_job();
		lwsync();
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
	th.srcbuf = malloc(COVER_RGBA_BYTES);
	if (!th.srcbuf)
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
		free(th.srcbuf);
		th.srcbuf = NULL;
		return false;
	}
	if (sysThreadCreate(&th.tid, thumb_thread, NULL, THUMB_PRIO, THUMB_STACK, THREAD_JOINABLE,
			    (char *)"salsa64_thumbs") != 0) {
		salsa64_log("miniaturas: no se pudo crear el hilo");
		sysSemDestroy(th.sem);
		free(th.srcbuf);
		th.srcbuf = NULL;
		return false;
	}
	th.ready = true;
	salsa64_log("miniaturas: %d casillas de %dx%d en la ranura %d del 2D, %u KB de copias, hilo de baja prioridad, "
		    "caché en disco en %s/.mini",
		    THUMB_CELLS, THUMB_W, THUMB_H, THUMBS_SLOT, (unsigned)(sizeof th.px >> 10), dir);
	return true;
}

bool thumbs_ready(void)
{
	return th.ready;
}

static void fill_request(struct request *r, const struct rom_entry *e)
{
	r->set = true;
	r->key = thumb_key(romlist_name(e));
	snprintf(r->name, sizeof r->name, "%s", romlist_name(e));
	snprintf(r->title, sizeof r->title, "%s", romlist_display_title(e));
	snprintf(r->code, sizeof r->code, "%s", e->code);
}

/* La casilla de `e` sin contar aciertos; −1 si no está. */
static int cell_of(uint32_t key)
{
	for (int i = 0; i < THUMB_CELLS; i++)
		if (th.cache.key[i] == key)
			return i;
	return -1;
}

static void want(const struct rom_entry *e, float dist)
{
	if (!th.want.set || dist < th.want_dist) {
		fill_request(&th.want, e);
		th.want_dist = dist;
	}
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
		th.rows_found++;
		return true;
	}
	if (!thumb_cache_is_missing(&th.cache, key)) {
		th.rows_missing++;
		want(e, dist);
	}
	return false;
}

const uint8_t *thumbs_pixels(const struct rom_entry *e)
{
	if (!th.ready || e->kind != ROMLIST_ROM)
		return NULL;
	uint32_t key = thumb_key(romlist_name(e));
	int cell = thumb_cache_find(&th.cache, key, th.frame);
	if (cell >= 0)
		return th.px[cell];
	if (!thumb_cache_is_missing(&th.cache, key))
		want(e, -1.0f);
	return NULL;
}

void thumbs_big_request(const struct rom_entry *e, int token)
{
	if (!th.ready)
		return;
	fill_request(&th.big_want, e);
	th.big_token = token;
}

int thumbs_big_ready(void)
{
	lwsync();
	return th.ready && th.job == JOB_BIGWAIT ? th.job_token : -1;
}

bool thumbs_big_poll(uint8_t *dst, int *token, bool *ok, unsigned *w, unsigned *h)
{
	lwsync();
	if (!th.ready || th.job != JOB_BIGWAIT)
		return false;
	*token = th.job_token;
	*ok = th.w && th.h;
	*w = th.w;
	*h = th.h;
	if (*ok && dst)
		memcpy(dst, th.srcbuf, (size_t)th.w * th.h * 4);
	lwsync();
	th.job = JOB_IDLE;
	return true;
}

static const char *src_name(int s)
{
	return s == SRC_DISK ? "de la caché en disco" : "del PNG";
}

void thumbs_frame(void)
{
	if (!th.ready)
		return;
	th.frame++;
	lwsync();
	int j = th.job;
	if (j == JOB_DONE_OK || j == JOB_DONE_NONE || j == JOB_DONE_ERR) {
		bool big = th.job_big;
		if (j == JOB_DONE_OK) {
			int cell = thumb_cache_assign(&th.cache, th.req.key, th.frame);
			uint32_t x, y;
			thumb_cell_origin(cell, &x, &y);
			memcpy(th.px[cell], th.thumb, THUMB_BYTES);
			int rc = rsxkit_ui_image_update(THUMBS_SLOT, x, y, THUMB_W, THUMB_H, th.thumb);
			if (rc != RSXKIT_OK) {
				th.cache.key[cell] = 0;
				salsa64_log("miniaturas: no se pudo subir la casilla (%d)", rc);
			}
			th.uploaded++;
			if (th.src == SRC_DISK) {
				th.disk_hits++;
				salsa64_log("miniaturas: %s: %s en %.1f ms; %u de disco, %u decodificadas, filas con/sin miniatura %u/%u",
					    th.req.name, src_name(th.src), (double)th.us_disk / 1000.0, th.disk_hits, th.decoded,
					    th.rows_found, th.rows_missing);
			} else {
				th.decoded++;
				salsa64_log("miniaturas: %s%s: caché %.1f ms, buscar %.1f ms, leer %.1f ms (%u bytes), decodificar %ux%u "
					    "%.1f ms, reducir %.1f ms; %u de disco, %u decodificadas, filas con/sin miniatura %u/%u",
					    th.req.name, big ? " (completa)" : "", (double)th.us_disk / 1000.0, (double)th.us_find / 1000.0,
					    (double)th.us_read / 1000.0, (unsigned)th.png_bytes, th.w, th.h, (double)th.us_dec / 1000.0,
					    (double)th.us_scale / 1000.0, th.disk_hits, th.decoded, th.rows_found, th.rows_missing);
			}
			th.rows_found = th.rows_missing = 0;
		} else if (j == JOB_DONE_NONE) {
			salsa64_log("miniaturas: %s: sin carátula, %s (caché %.1f ms, buscar %.1f ms)", th.req.name, src_name(th.src),
				    (double)th.us_disk / 1000.0, (double)th.us_find / 1000.0);
			thumb_cache_mark_missing(&th.cache, th.req.key);
		} else {
			th.failed++;
			thumb_cache_mark_missing(&th.cache, th.req.key);
			salsa64_log("miniaturas: %s: la carátula no se pudo leer", th.req.name);
		}
		if (big) {
			if (j != JOB_DONE_OK)
				th.w = th.h = 0;
			th.job = JOB_BIGWAIT;  /* la recoge thumbs_big_poll */
			j = JOB_BIGWAIT;
		} else {
			th.job = JOB_IDLE;
			j = JOB_IDLE;
		}
	}
	/* Lo que una fila pidió y ya llegó entretanto no se vuelve a pedir. */
	if (th.want.set && (cell_of(th.want.key) >= 0 || thumb_cache_is_missing(&th.cache, th.want.key)))
		th.want.set = false;
	if (j == JOB_IDLE && (th.big_want.set || th.want.set)) {
		bool big = th.big_want.set;
		th.req = big ? th.big_want : th.want;
		th.job_big = big;
		th.job_token = big ? th.big_token : 0;
		if (big)
			th.big_want.set = false;
		lwsync();
		th.job = JOB_RUNNING;
		sysSemPost(th.sem, 1);
	}
	th.want.set = false;
}

void thumbs_forget_missing(void)
{
	if (th.ready)
		thumb_cache_forget_missing(&th.cache);
}
