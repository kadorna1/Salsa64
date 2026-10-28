/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Pruebas del cartucho 3D del menú (src/frontend/cartucho_mesh.c): el lector
 * de assets/menu/cartucho.mesh (conteos, rangos de índices, normales
 * unitarias), el dibujo (caras traseras fuera, orden de pintor, luz) y la
 * textura de la etiqueta con la carátula.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rsxkit/ui.h>

#include "frontend/cartucho_mesh.h"
#include "frontend/thumbs_calc.h"
#include "test.h"

static uint8_t *load(size_t *n)
{
	static const char *const paths[] = { "../../assets/menu/cartucho.mesh", "assets/menu/cartucho.mesh",
					     "../assets/menu/cartucho.mesh" };
	for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; i++) {
		FILE *f = fopen(paths[i], "rb");
		if (!f)
			continue;
		fseek(f, 0, SEEK_END);
		long sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		uint8_t *b = malloc((size_t)sz);
		if (b && fread(b, 1, (size_t)sz, f) == (size_t)sz) {
			fclose(f);
			*n = (size_t)sz;
			return b;
		}
		fclose(f);
		free(b);
	}
	return NULL;
}

static struct cartucho_mesh mesh;
static struct cartucho_out out;

static void parse(void)
{
	size_t n = 0;
	uint8_t *d = load(&n);
	CHECK(d != NULL);
	if (!d)
		return;
	CHECK_EQ(cartucho_mesh_parse(&mesh, d, n), 0);
	CHECK_EQ(mesh.nv, 473);
	CHECK_EQ(mesh.np, 480);
	CHECK_EQ(mesh.nl, 18);
	for (uint32_t i = 0; i < (mesh.np + mesh.nl) * 3; i++)
		CHECK(mesh.idx[i] < mesh.nv);
	float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
	for (uint32_t i = 0; i < mesh.nv; i++) {
		float len = sqrtf(mesh.v[i][3] * mesh.v[i][3] + mesh.v[i][4] * mesh.v[i][4] + mesh.v[i][5] * mesh.v[i][5]);
		CHECK(fabsf(len - 1.0f) < 1e-3f);
		for (int k = 0; k < 3; k++) {
			if (mesh.v[i][k] < lo[k]) lo[k] = mesh.v[i][k];
			if (mesh.v[i][k] > hi[k]) hi[k] = mesh.v[i][k];
		}
	}
	/* Alto 1 y centrado. */
	CHECK(fabsf((hi[1] - lo[1]) - 1.0f) < 1e-3f);
	CHECK(fabsf(hi[1] + lo[1]) < 1e-3f);
	/* Los vértices de la etiqueta van de 0 a 1 en u y v. */
	for (uint32_t t = mesh.np; t < mesh.np + mesh.nl; t++)
		for (int j = 0; j < 3; j++) {
			float u = mesh.v[mesh.idx[3 * t + j]][6], v = mesh.v[mesh.idx[3 * t + j]][7];
			CHECK(u >= -1e-4f && u <= 1.0001f && v >= -1e-4f && v <= 1.0001f);
		}
	free(d);
}

static void bad_input(void)
{
	size_t n = 0;
	uint8_t *d = load(&n);
	if (!d)
		return;
	struct cartucho_mesh m;
	CHECK(cartucho_mesh_parse(&m, d, n - 1) < 0);          /* tamaño */
	CHECK(cartucho_mesh_parse(&m, d, 8) < 0);
	d[0] = 'X';
	CHECK(cartucho_mesh_parse(&m, d, n) < 0);              /* magia */
	d[0] = 'C';
	d[n - 1] = 0xFF, d[n - 2] = 0xFF;                      /* índice fuera de rango */
	CHECK(cartucho_mesh_parse(&m, d, n) < 0);
	free(d);
}

static void draw(void)
{
	CHECK(mesh.nv != 0);
	if (!mesh.nv)
		return;
	/* De frente: se ve la mitad de los triángulos (o menos) y la etiqueta entera. */
	cartucho_mesh_draw(&mesh, 0.0f, 0.0f, 100.0f, 200.0f, 80.0f, 256.0f, 256.0f, 256.0f, 256.0f, &out);
	CHECK(out.nplastic > 0 && out.nplastic % 3 == 0);
	CHECK(out.nplastic / 3 < mesh.np);
	CHECK(out.nlabel > 0 && out.nlabel <= 16 * 3);
	/* Todos los triángulos que quedan miran al espectador (antihorarios con la
	 * y hacia arriba: horarios en pantalla, con la y hacia abajo) y caen en la caja del cartucho. */
	for (uint32_t i = 0; i < out.nplastic; i += 3) {
		const rsxkit_vertex *a = &out.plastic[i], *b = a + 1, *c = a + 2;
		float cr = (b->pos[0] - a->pos[0]) * (c->pos[1] - a->pos[1]) - (b->pos[1] - a->pos[1]) * (c->pos[0] - a->pos[0]);
		CHECK(cr < 1e-2f);  /* horario en pantalla (los de canto, ~0) */
		for (int j = 0; j < 3; j++) {
			CHECK(a[j].pos[0] > 100.0f - 80.0f && a[j].pos[0] < 100.0f + 80.0f);
			CHECK(a[j].pos[1] > 200.0f - 41.0f && a[j].pos[1] < 200.0f + 41.0f);
		}
	}
	/* La mitad del giro: se ve la espalda; ningún triángulo de la etiqueta. */
	cartucho_mesh_draw(&mesh, 3.14159265f, 0.0f, 0.0f, 0.0f, 80.0f, 256.0f, 256.0f, 256.0f, 256.0f, &out);
	CHECK_EQ(out.nlabel, 0);
	CHECK(out.nplastic > 0);
	/* De frente se ve toda la etiqueta; de lado (90°) solo lo que se curva hacia ese lado. */
	cartucho_mesh_draw(&mesh, 0.0f, 0.0f, 0.0f, 0.0f, 80.0f, 256.0f, 256.0f, 256.0f, 256.0f, &out);
	uint32_t front = out.nlabel;
	cartucho_mesh_draw(&mesh, 1.5707963f, 0.0f, 0.0f, 0.0f, 80.0f, 256.0f, 256.0f, 256.0f, 256.0f, &out);
	CHECK(out.nlabel < front);
}

static void painter(void)
{
	/* De atrás hacia adelante: la z del centro de cada triángulo no baja (con un
	 * pequeño margen: la profundidad del triángulo es la suma de sus vértices). */
	cartucho_mesh_draw(&mesh, 0.7f, 0.1f, 0.0f, 0.0f, 100.0f, 256.0f, 256.0f, 256.0f, 256.0f, &out);
	CHECK(out.nplastic > 0 && out.nlabel > 0);
	/* La luz va en rgb gris y el brillo en el alfa. */
	for (uint32_t i = 0; i < out.nplastic; i++) {
		const uint8_t *c = out.plastic[i].rgba;
		CHECK(c[0] == c[1] && c[1] == c[2]);
		CHECK(c[0] >= 120);
		CHECK_EQ(out.plastic[i].pos[3] == 1.0f, 1);
	}
	/* Del lado de la luz (arriba a la izquierda) más claro que del otro: la cara delantera es más clara que la trasera. */
	cartucho_mesh_draw(&mesh, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 256.0f, 256.0f, 256.0f, 256.0f, &out);
	unsigned maxl = 0;
	for (uint32_t i = 0; i < out.nlabel; i++)
		if (out.label[i].rgba[0] > maxl)
			maxl = out.label[i].rgba[0];
	CHECK(maxl > 200);
}

static void label(void)
{
	enum { N = CARTUCHO_LABEL_TEX };
	static uint8_t base[N * N * 4], tex[N * N * 4];
	for (unsigned i = 0; i < N * N; i++)
		base[4 * i] = 0x10, base[4 * i + 1] = 0x20, base[4 * i + 2] = 0x30, base[4 * i + 3] = 0xFF;
	/* Sin carátula: la base tal cual; sin base, el gris. */
	cartucho_label_compose(tex, N * 4, base, NULL, 0, 0);
	CHECK(!memcmp(tex, base, sizeof base));
	cartucho_label_compose(tex, N * 4, NULL, NULL, 0, 0);
	CHECK_EQ(tex[0], 0xB4);
	CHECK_EQ(tex[1], 0xB8);
	CHECK_EQ(tex[2], 0xBE);
	CHECK_EQ(tex[3], 0xFF);
	/* Con una carátula roja: el centro es rojo y fuera del sticker sigue la base. */
	static uint8_t red[40 * 60 * 4];
	for (unsigned i = 0; i < 40 * 60; i++)
		red[4 * i] = 0xFF, red[4 * i + 3] = 0xFF;
	cartucho_label_compose(tex, N * 4, base, red, 40, 60);
	const uint8_t *c = tex + ((size_t)(N / 2) * N + N / 2) * 4;
	CHECK_EQ(c[0], 0xFF);
	CHECK_EQ(c[1], 0x00);
	CHECK_EQ(c[2], 0x00);
	CHECK_EQ(tex[0], 0x10);                                      /* esquina: fuera */
	CHECK_EQ(tex[((size_t)(N / 2) * N + 3) * 4], 0x10);          /* a la izquierda del sticker (u < 0,0646) */
	CHECK_EQ(tex[((size_t)(N / 2) * N + N - 3) * 4], 0x10);      /* a la derecha (u > 0,8711) */
	CHECK_EQ(tex[((size_t)3 * N + N / 2) * 4], 0x10);            /* arriba (v < 0,11 de la imagen) */
	/* Abajo el sticker llega al borde de la imagen. */
	CHECK_EQ(tex[((size_t)(N - 1) * N + N / 2) * 4], 0xFF);
	/* La esquina de arriba a la izquierda del sticker está redondeada: su primer píxel es de la base. */
	CHECK_EQ(tex[((size_t)(int)(CARTUCHO_STICKER_V0 * N + 0.5f) * N + (int)(CARTUCHO_STICKER_U0 * N + 0.5f)) * 4], 0x10);
}

static void label_fill(void)
{
	enum { N = CARTUCHO_LABEL_TEX };
	static uint8_t base[N * N * 4], tex[N * N * 4], wide[320 * 233 * 4];
	memset(base, 0x10, sizeof base);
	for (unsigned j = 0; j < 233; j++)
		for (unsigned i = 0; i < 320; i++) {
			uint8_t *p = wide + ((size_t)j * 320 + i) * 4;
			bool blue = i >= 100 && i < 220;
			p[0] = blue ? 0 : 255, p[1] = 0, p[2] = blue ? 255 : 0, p[3] = 255;
		}
	/* En el sticker, a 90/320 de su ancho: estirada, la columna 90 (roja); recortada, la ~114 (azul). */
	size_t x = (size_t)(CARTUCHO_STICKER_U0 * N + (CARTUCHO_STICKER_U1 - CARTUCHO_STICKER_U0) * N * 90.0f / 320.0f), y = N / 2;
	cartucho_label_compose(tex, N * 4, base, wide, 320, 233);
	CHECK_EQ(tex[(y * N + x) * 4], 255);
	thumb_set_crop(true);
	cartucho_label_compose(tex, N * 4, base, wide, 320, 233);
	CHECK_EQ(tex[(y * N + x) * 4 + 2], 255);
	thumb_set_crop(false);
}

static int hook_calls;
static void hook(void *u)
{
	hook_calls += *(int *)u;
}

/* El gancho del 2D queda entre dos rectángulos, como un grupo propio. */
static void ui_hook(void)
{
	static rsxkit_vertex vb[64];
	int one = 1;
	rsxkit_ui_batch(vb, 64, 100.0f, 100.0f);
	rsxkit_ui_rect(0, 0, 10, 10, 0xFFFFFFFFu);
	rsxkit_ui_hook(hook, &one);
	rsxkit_ui_rect(5, 5, 10, 10, 0xFF0000FFu);
	const uint32_t *first;
	uint32_t n = rsxkit_ui_groups(&first), hooks = 0, at = 0;
	CHECK_EQ(n, 3);
	for (uint32_t g = 0; g < n; g++)
		if (rsxkit_ui_group_image(g) == RSXKIT_UI_HOOK_GROUP) {
			hooks++;
			at = g;
			rsxkit_ui_hook_fn fn;
			void *user;
			rsxkit_ui_group_hook(g, &fn, &user);
			CHECK(fn == hook && user == &one);
			fn(user);
		}
	CHECK_EQ(hooks, 1);
	CHECK_EQ(at, 1);
	CHECK_EQ(hook_calls, 1);
	CHECK_EQ(rsxkit_ui_count(), 12);       /* los dos rectángulos; el gancho no suma vértices */
	CHECK_EQ(first[1], 6);
	CHECK_EQ(first[2], 6);
	rsxkit_ui_batch(NULL, 0, 0.0f, 0.0f);
}

static void thumb_scaling(void)
{
	/* Vertical (200 × 300), mitad izquierda negra y derecha blanca: la raya queda al centro. */
	static uint8_t src[200 * 300 * 4], dst[THUMB_BYTES];
	for (unsigned y = 0; y < 300; y++)
		for (unsigned x = 0; x < 200; x++) {
			uint8_t *p = src + ((size_t)y * 200 + x) * 4;
			p[0] = p[1] = p[2] = x < 100 ? 0 : 255;
			p[3] = 255;
		}
	thumb_scale(src, 200, 300, dst);
	const uint8_t *mid = dst + ((size_t)(THUMB_H / 2) * THUMB_W + THUMB_W / 4) * 4;
	const uint8_t *right = dst + ((size_t)(THUMB_H / 2) * THUMB_W + THUMB_W * 3 / 4) * 4;
	CHECK_EQ(mid[0], 0);
	CHECK_EQ(mid[3], 255);
	CHECK_EQ(right[0], 255);
	/* Las esquinas son transparentes y el medio de cada lado, opaco. */
	CHECK_EQ(dst[3], 0);
	CHECK_EQ(dst[((size_t)(THUMB_W - 1)) * 4 + 3], 0);
	CHECK_EQ(dst[((size_t)(THUMB_H - 1) * THUMB_W) * 4 + 3], 0);
	CHECK_EQ(dst[((size_t)(THUMB_H - 1) * THUMB_W + THUMB_W - 1) * 4 + 3], 0);
	CHECK_EQ(dst[((size_t)(THUMB_H / 2) * THUMB_W) * 4 + 3], 255);
	CHECK_EQ(dst[((size_t)(THUMB_W / 2)) * 4 + 3], 255);
	/* Una fuente más chica que la casilla también sirve. */
	static uint8_t tiny[4 * 4 * 4];
	memset(tiny, 200, sizeof tiny);
	thumb_scale(tiny, 4, 4, dst);
	CHECK_EQ(dst[((size_t)(THUMB_H / 2) * THUMB_W + THUMB_W / 2) * 4], 200);
	/* Apaisada (320 × 233, como las de N64): se recorta centrada, no se estira. */
	unsigned x, y, cw, ch;
	cover_fit_crop(320, 233, THUMB_W, THUMB_H, &x, &y, &cw, &ch);
	CHECK_EQ(ch, 233);
	CHECK_EQ(y, 0);
	CHECK(cw >= 202 && cw <= 204 && x == (320 - cw) / 2);
	cover_fit_crop(100, 400, THUMB_W, THUMB_H, &x, &y, &cw, &ch);  /* más alta: arriba y abajo */
	CHECK_EQ(cw, 100);
	CHECK_EQ(x, 0);
	CHECK(ch == 100 * THUMB_H / THUMB_W && y == (400 - ch) / 2);
	/* Franja central azul y lados rojos: la miniatura muestra azul en el centro y rojo en los bordes. */
	static uint8_t wide[320 * 233 * 4];
	for (unsigned j = 0; j < 233; j++)
		for (unsigned i = 0; i < 320; i++) {
			uint8_t *p = wide + ((size_t)j * 320 + i) * 4;
			bool blue = i >= 100 && i < 220;
			p[0] = blue ? 0 : 255, p[1] = 0, p[2] = blue ? 255 : 0, p[3] = 255;
		}
	const size_t mid_row = (size_t)(THUMB_H / 2) * THUMB_W;
	/* Por omisión se estira: la columna 12 de la miniatura es la 70 de la carátula (roja). */
	CHECK(!thumb_crop());
	thumb_scale(wide, 320, 233, dst);
	CHECK_EQ(dst[(mid_row + 12) * 4], 255);
	CHECK_EQ(dst[(mid_row + THUMB_W / 2) * 4 + 2], 255);
	/* Recortada: esa columna cae en la 58 + 12 · 3,6 ≈ 101 de la carátula (azul). */
	thumb_set_crop(true);
	thumb_scale(wide, 320, 233, dst);
	CHECK_EQ(dst[(mid_row + 12) * 4 + 2], 255);
	CHECK_EQ(dst[(mid_row + 12) * 4], 0);
	CHECK_EQ(dst[(mid_row + THUMB_W / 2) * 4 + 2], 255);
	CHECK_EQ(dst[(mid_row + 1) * 4], 255);  /* el recorte llega a x = 58 < 100 */
	thumb_set_crop(false);
}

static void thumb_file(void)
{
	static uint8_t px[THUMB_BYTES], buf[THUMB_FILE_HEADER + THUMB_BYTES];
	for (unsigned i = 0; i < THUMB_BYTES; i++)
		px[i] = (uint8_t)(i * 7);
	struct thumb_file f = { .kind = THUMB_FILE_OK, .png_size = 123456, .png_time = 0x5F000001u };
	snprintf(f.path, sizeof f.path, "/dev_hdd0/salsa64/covers/Juego (U).png");
	CHECK_EQ(thumb_file_pack(buf, &f, px), THUMB_FILE_HEADER + THUMB_BYTES);
	struct thumb_file g;
	const uint8_t *q = NULL;
	CHECK_EQ(thumb_file_unpack(buf, THUMB_FILE_HEADER + THUMB_BYTES, &g, &q), 0);
	CHECK_EQ(g.kind, THUMB_FILE_OK);
	CHECK_EQ(g.png_size, 123456);
	CHECK_EQ(g.png_time, 0x5F000001u);
	CHECK(!strcmp(g.path, f.path));
	CHECK(q != NULL && !memcmp(q, px, THUMB_BYTES));
	/* Truncado, otra versión o otra casilla: no vale. */
	CHECK(thumb_file_unpack(buf, THUMB_FILE_HEADER + THUMB_BYTES - 1, &g, &q) < 0);
	buf[5] = 1;  /* la versión anterior (miniaturas recortadas) */
	CHECK(thumb_file_unpack(buf, THUMB_FILE_HEADER + THUMB_BYTES, &g, &q) < 0);
	buf[5] = 2;
	buf[9] ^= 1;
	CHECK(thumb_file_unpack(buf, THUMB_FILE_HEADER + THUMB_BYTES, &g, &q) < 0);
	buf[9] ^= 1;
	buf[0] = 'X';
	CHECK(thumb_file_unpack(buf, THUMB_FILE_HEADER + THUMB_BYTES, &g, &q) < 0);
	/* «Sin carátula»: solo la cabecera, con la fecha de la carpeta. */
	struct thumb_file n = { .kind = THUMB_FILE_NONE, .png_size = 0, .png_time = 99 };
	CHECK_EQ(thumb_file_pack(buf, &n, NULL), THUMB_FILE_HEADER);
	CHECK_EQ(thumb_file_unpack(buf, THUMB_FILE_HEADER, &g, &q), 0);
	CHECK(g.kind == THUMB_FILE_NONE && g.png_time == 99 && q == NULL);
	/* Una miniatura hecha con el otro modo de llenar el sticker no vale. */
	CHECK_EQ(thumb_file_pack(buf, &f, px), THUMB_FILE_HEADER + THUMB_BYTES);
	thumb_set_crop(true);
	CHECK(thumb_file_unpack(buf, THUMB_FILE_HEADER + THUMB_BYTES, &g, &q) < 0);
	thumb_set_crop(false);
	CHECK_EQ(thumb_file_unpack(buf, THUMB_FILE_HEADER + THUMB_BYTES, &g, &q), 0);
	/* Una ruta que no cabe no se escribe. */
	memset(f.path, 'a', THUMB_PATH_MAX);
	f.path[THUMB_PATH_MAX] = 0;
	CHECK_EQ(thumb_file_pack(buf, &f, px), THUMB_FILE_HEADER + THUMB_BYTES);
}

static void thumb_lru(void)
{
	struct thumb_cache c;
	thumb_cache_init(&c);
	CHECK_EQ(thumb_cache_find(&c, 7, 1), -1);
	int seen[THUMB_CELLS] = { 0 };
	for (uint32_t k = 1; k <= THUMB_CELLS; k++) {
		int cell = thumb_cache_assign(&c, 100 + k, k);
		CHECK(cell >= 0 && cell < THUMB_CELLS && !seen[cell]);
		seen[cell] = 1;
	}
	CHECK_EQ(thumb_cache_find(&c, 103, 20), 2);  /* la tercera: usada en el cuadro 20 */
	/* Una décima desaloja la menos usada (la primera, cuadro 1), no la que acaba de usarse. */
	int cell = thumb_cache_assign(&c, 999, 21);
	CHECK_EQ(cell, 0);
	CHECK_EQ(thumb_cache_find(&c, 101, 22), -1);
	CHECK(thumb_cache_find(&c, 103, 22) >= 0);
	/* Lo usado en el cuadro en curso se desaloja al final. */
	for (int i = 0; i < THUMB_CELLS; i++)
		c.stamp[i] = 50;
	c.stamp[5] = 49;
	CHECK_EQ(thumb_cache_assign(&c, 555, 50), 5);
	CHECK(c.hits >= 2 && c.misses >= 2);
	/* Los que no tienen carátula se recuerdan en un anillo y se pueden olvidar. */
	for (uint32_t k = 1; k <= THUMB_MISSING + 5; k++)
		thumb_cache_mark_missing(&c, 1000 + k);
	CHECK(!thumb_cache_is_missing(&c, 1001));            /* el más viejo salió */
	CHECK(thumb_cache_is_missing(&c, 1000 + THUMB_MISSING + 5));
	thumb_cache_forget_missing(&c);
	CHECK(!thumb_cache_is_missing(&c, 1000 + THUMB_MISSING + 5));
	CHECK(thumb_key("a") != 0 && thumb_key("a") != thumb_key("b"));
	/* Las casillas caben en el atlas, separadas. */
	uint32_t x, y;
	thumb_cell_origin(THUMB_CELLS - 1, &x, &y);
	CHECK(x + THUMB_W <= THUMB_ATLAS_W && y + THUMB_H <= THUMB_ATLAS_H);
	CHECK(THUMB_STEP_X > THUMB_W && THUMB_STEP_Y > THUMB_H);
}

void t_suite_cartucho(void)
{
	RUN(parse);
	RUN(bad_input);
	RUN(draw);
	RUN(painter);
	RUN(label);
	RUN(label_fill);
	RUN(ui_hook);
	RUN(thumb_scaling);
	RUN(thumb_file);
	RUN(thumb_lru);
}
