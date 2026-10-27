/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — vista previa del fondo «Seda» del menú, en la Mac.
 *
 * Genera la malla con el mismo código C que corre en el SPE y en el PPE
 * (src/frontend/seda_calc.c) y la rasteriza por software con la misma
 * cuenta que los fragment programs (src/frontend/seda/, los .fcg): degradado de
 * la hora, la cinta con mezcla aditiva y los destellos. Escribe un PNG.
 *
 *   seda-vista ANCHO ALTO SEGUNDOS HORA SALIDA.png
 *
 * HORA en horas (0..24). Para varios instantes y horas: tools/seda-vista.sh.
 * Compilar: cc -O2 -I src -o build/seda-vista tools/seda-vista.c -lm
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/frontend/seda_calc.h"
#include "../src/frontend/seda_calc.c"

/* Constantes de seda_cinta.fcg. */
#define BASE 0.16f
#define FRES 1.7f
#define SPEC 0.9f

static int W, H;
static float *img;  /* RGB en flotante */

static float sat(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

/* Rampa de color de 16 texeles RGBA8 (lo que escribe seda.c), con filtro lineal. */
static uint8_t ramp[4][16][3];

static void make_ramps(const struct seda_colors *c)
{
	for (int l = 0; l < 4; l++)
		for (int i = 0; i < 16; i++) {
			float t = i / 15.0f;
			float g = l < SEDA_LAYERS ? seda_layer_gain(l) : 0.9f * c->stars + 0.25f;
			for (int k = 0; k < 3; k++) {
				float v = c->tint_a[k] + (c->tint_b[k] - c->tint_a[k]) * t;
				if (l == SEDA_LAYERS)
					v = 0.5f + 0.5f * v;  /* destellos más blancos */
				ramp[l][i][k] = (uint8_t)(sat(v * g) * 255.0f + 0.5f);
			}
		}
}

static void sample_ramp(int l, float hue, float *rgb)
{
	float x = hue * 15.0f;
	int i = (int)x;
	if (i > 14)
		i = 14;
	float f = x - (float)i;
	for (int k = 0; k < 3; k++)
		rgb[k] = ((1.0f - f) * ramp[l][i][k] + f * ramp[l][i + 1][k]) / 255.0f;
}

static void background(const struct seda_colors *c)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float tx = x + 0.5f, ty = y + 0.5f, u = tx / W, v = ty / H;
			float dx = (u - 0.60f) * ((float)W / H), dy = v - 0.48f;
			float g = 1.0f / (1.0f + (dx * dx + dy * dy) * 6.0f);
			float ex = u - 0.5f, ey = v - 0.5f;
			float vig = 1.0f - 0.45f * (ex * ex + ey * ey);
			float n0 = 0.06711056f * tx + 0.00583715f * ty;
			n0 -= floorf(n0);
			float n = 52.9829189f * n0;
			n -= floorf(n);
			for (int k = 0; k < 3; k++) {
				float base = c->top[k] + (c->bottom[k] - c->top[k]) * v;
				img[(y * W + x) * 3 + k] = (base + 0.9f * c->glow[k] * g) * vig + (n - 0.5f) / 255.0f;
			}
		}
}

/* Triángulo con interpolación con corrección de perspectiva; `prog` 0 la
 * cinta (capa `layer`) y 1 el destello. */
static void tri(const seda_vertex *a, const seda_vertex *b, const seda_vertex *c, int prog, int layer)
{
	const seda_vertex *v[3] = { a, b, c };
	float sx[3], sy[3], iw[3];
	for (int i = 0; i < 3; i++) {
		iw[i] = 1.0f / v[i]->pos[3];
		sx[i] = (v[i]->pos[0] * iw[i] * 0.5f + 0.5f) * W;
		sy[i] = (0.5f - v[i]->pos[1] * iw[i] * 0.5f) * H;
	}
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
	if (fabsf(area) < 1e-6f)
		return;
	int x0 = (int)floorf(fminf(sx[0], fminf(sx[1], sx[2]))), x1 = (int)ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2])));
	int y0 = (int)floorf(fminf(sy[0], fminf(sy[1], sy[2]))), y1 = (int)ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2])));
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > W) x1 = W;
	if (y1 > H) y1 = H;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
			float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
			float w2 = 1.0f - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0)
				continue;
			float p0 = w0 * iw[0], p1 = w1 * iw[1], p2 = w2 * iw[2], s = p0 + p1 + p2;
			float t[4];
			for (int k = 0; k < 4; k++)
				t[k] = (p0 * v[0]->t0[k] + p1 * v[1]->t0[k] + p2 * v[2]->t0[k]) / s;
			float rgb[3], tint[3];
			if (prog == 0) {
				float r = 1.0f - sat(t[0]);
				float fres = r * r * r;
				float sp = sat(t[1]);
				sp *= sp, sp *= sp, sp *= sp, sp *= sp;
				sample_ramp(layer, sat(t[3]), tint);
				for (int k = 0; k < 3; k++) {
					float sheen = tint[k] + (1.0f - tint[k]) * 0.45f;
					rgb[k] = (tint[k] * (BASE + FRES * fres) + sheen * (SPEC * sp)) * t[2];
				}
			} else {
				float d2 = t[0] * t[0] + t[1] * t[1];
				float a2 = sat(1.0f - d2);
				float core = a2 * a2 * a2;
				float glint = sat(1.0f - 8.0f * fabsf(t[0] * t[1])) * a2;
				sample_ramp(SEDA_LAYERS, sat(t[3]), tint);
				for (int k = 0; k < 3; k++)
					rgb[k] = tint[k] * ((core + 0.6f * glint) * t[2]);
			}
			for (int k = 0; k < 3; k++)
				img[(y * W + x) * 3 + k] += rgb[k];
		}
}

/* ---- PNG sin compresión ------------------------------------------------------------- */

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t v = i;
			for (int k = 0; k < 8; k++)
				v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
			crc_table[i] = v;
		}
	c = ~c;
	while (n--)
		c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
	return ~c;
}

static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
	uint8_t hdr[8] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n, type[0], type[1], type[2], type[3] };
	fwrite(hdr, 1, 8, f);
	if (n)
		fwrite(d, 1, n, f);
	uint32_t c = crc(0, (const uint8_t *)type, 4);
	c = crc(c, d, n);
	uint8_t e[4] = { (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
	fwrite(e, 1, 4, f);
}

static int write_png(const char *name, const uint8_t *rgb, int w, int h)
{
	size_t raw = (size_t)h * (1 + (size_t)w * 3);
	uint8_t *z = malloc(raw + raw / 65535 * 5 + 64);
	uint8_t *line = malloc(raw);
	for (int y = 0; y < h; y++) {
		line[(size_t)y * (1 + w * 3)] = 0;
		memcpy(line + (size_t)y * (1 + w * 3) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
	}
	size_t n = 0;
	z[n++] = 0x78, z[n++] = 0x01;
	for (size_t o = 0; o < raw;) {
		size_t len = raw - o > 65535 ? 65535 : raw - o;
		z[n++] = o + len == raw;
		z[n++] = (uint8_t)len, z[n++] = (uint8_t)(len >> 8), z[n++] = (uint8_t)~len, z[n++] = (uint8_t)(~len >> 8);
		memcpy(z + n, line + o, len);
		n += len, o += len;
	}
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < raw; i++) {
		a = (a + line[i]) % 65521u;
		b = (b + a) % 65521u;
	}
	uint32_t ad = b << 16 | a;
	z[n++] = (uint8_t)(ad >> 24), z[n++] = (uint8_t)(ad >> 16), z[n++] = (uint8_t)(ad >> 8), z[n++] = (uint8_t)ad;
	FILE *f = fopen(name, "wb");
	if (!f)
		return -1;
	static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	fwrite(sig, 1, 8, f);
	uint8_t ihdr[13] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
			     (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0 };
	chunk(f, "IHDR", ihdr, 13);
	chunk(f, "IDAT", z, (uint32_t)n);
	chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(z);
	free(line);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 6) {
		fprintf(stderr, "uso: %s ANCHO ALTO SEGUNDOS HORA SALIDA.png\n", argv[0]);
		return 1;
	}
	W = atoi(argv[1]), H = atoi(argv[2]);
	double t = atof(argv[3]), hour = atof(argv[4]);
	float aspect = (float)W / (float)H, tf = seda_phase(t);
	img = calloc((size_t)W * H * 3, sizeof *img);
	struct seda_colors col;
	seda_colors(hour * 3600.0, &col);
	make_ramps(&col);
	background(&col);

	static float scratch[SEDA_LAYER_VERTS][4];
	static seda_vertex verts[SEDA_LAYER_VERTS];
	static uint16_t idx[SEDA_INDICES], sidx[SEDA_SPARK_INDICES];
	static seda_vertex spark[SEDA_SPARK_VERTS];
	seda_indices(idx);
	seda_spark_indices(sidx);
	for (int l = 0; l < SEDA_LAYERS; l++) {
		seda_layer(l, aspect, tf, scratch, verts);
		for (int i = 0; i < SEDA_INDICES; i += 3)
			tri(&verts[idx[i]], &verts[idx[i + 1]], &verts[idx[i + 2]], 0, l);
	}
	seda_sparkles(aspect, tf, spark);
	for (int i = 0; i < SEDA_SPARK_INDICES; i += 3)
		tri(&spark[sidx[i]], &spark[sidx[i + 1]], &spark[sidx[i + 2]], 1, 0);

	uint8_t *out = malloc((size_t)W * H * 3);
	for (size_t i = 0; i < (size_t)W * H * 3; i++)
		out[i] = (uint8_t)(sat(img[i]) * 255.0f + 0.5f);
	int rc = write_png(argv[5], out, W, H);
	free(out);
	free(img);
	return rc ? 1 : 0;
}
