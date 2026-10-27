/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — referencia del fondo «Shader» del menú.
 *
 * Basado en el shader «XdGfRR» de Shadertoy, de int_45h
 * (https://www.shadertoy.com/view/XdGfRR): una ola translúcida hecha con
 * raymarching, polvo de estrellas y un degradado de día o de noche. El autor
 * permite hacer cualquier cosa con él y pide un comentario de crédito.
 *
 *   fondo-ref original ANCHO ALTO ITIME HORA SALIDA.ppm
 *       el shader original píxel por píxel en flotante de 32 bits (con su AA
 *       de 2 × 2 y sus 100 pasos de raymarching); HORA en horas (0..24)
 *   fondo-ref rsx ANCHO ALTO ITIME HORA SALIDA.ppm
 *       la versión del RSX (src/frontend/fondo.c y fondo_*.fcg) con la misma
 *       lógica que sus fragment programs: tablas del hash, pasada de la ola a
 *       resolución reducida y pasada final
 *   fondo-ref ola ANCHO ALTO ITIME fondo-ola.f16 [fondo-low.f16]
 *       compara lo que volcó la consola (archivo fondo-volcado) con la
 *       emulación en el mismo iTime
 *   fondo-ref compara A.ppm B.ppm [DIFERENCIA.ppm]
 *       PSNR, promedio, percentil 99,9 y máximo de la diferencia por canal;
 *       la imagen de diferencia va ×8
 *
 * Compilar: cc -O2 -o build/fondo-ref tools/fondo-ref.c -lm -lpthread
 */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/frontend/fondo_calc.h"
#include "../src/frontend/fondo_calc.c"

/* ---- Imagen ---------------------------------------------------------------- */

static int W, H;
static float itime, iday;
static float *img;   /* RGB en flotante */

static void write_ppm(const char *path)
{
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror(path);
		exit(1);
	}
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			for (int c = 0; c < 3; c++) {
				/* El píxel (x, y) de Shadertoy tiene la y hacia arriba. */
				float v = img[((size_t)(H - 1 - y) * W + x) * 3 + c];
				v = v < 0 ? 0 : v > 1 ? 1 : v;
				fputc((int)(v * 255.0f + 0.5f), f);
			}
	fclose(f);
}

static unsigned char *read_ppm(const char *path, int *w, int *h)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	int maxv;
	if (fscanf(f, "P6 %d %d %d", w, h, &maxv) != 3 || maxv != 255) {
		fclose(f);
		return NULL;
	}
	fgetc(f);
	unsigned char *p = malloc((size_t)*w * *h * 3);
	if (fread(p, 3, (size_t)*w * *h, f) != (size_t)*w * *h) {
		fclose(f);
		free(p);
		return NULL;
	}
	fclose(f);
	return p;
}

/* ---- El original ------------------------------------------------------------- */

typedef struct {
	float x, y;
} f2;

static float o_value2d(float px, float py)
{
	float gx = floorf(px), gy = floorf(py), cx = px - gx, cy = py - gy;
	cx *= cx * (3.0f - 2.0f * cx);
	cy *= cy * (3.0f - 2.0f * cy);
	float a = fondo_hash12(gx, gy), b = fondo_hash12(gx + 1, gy);
	float c = fondo_hash12(gx, gy + 1), d = fondo_hash12(gx + 1, gy + 1);
	float m0 = a + (b - a) * cx, m1 = c + (d - c) * cx;
	return m0 + (m1 - m0) * cy;
}

static float smoothstepf(float a, float b, float x)
{
	float t = (x - a) / (b - a);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return t * t * (3.0f - 2.0f * t);
}

static float o_get_stars(float px, float py, float a, float t)
{
	float gx = floorf(px), gy = floorf(py), cx = px - gx, cy = py - gy;
	cx *= cx * (3.0f - 2.0f * cx);
	cy *= cy * (3.0f - 2.0f * cy);
	float s00 = fondo_star_rough(gx, gy), s10 = fondo_star_rough(gx + 1, gy);
	float s01 = fondo_star_rough(gx, gy + 1), s11 = fondo_star_rough(gx + 1, gy + 1);
	float m0 = s00 + (s10 - s00) * cx, m1 = s01 + (s11 - s01) * cx;
	float s = m0 + (m1 - m0) * cy;
	return smoothstepf(a, a + t, s) * powf(o_value2d(px * 0.1f + itime, py * 0.1f + itime) * 0.5f + 0.5f, 8.3f);
}

static float o_get_dust(float ux, float uy, float f)
{
	float ar = (float)W / (float)H;
	float ppx = ux * 2000.0f * ar, ppy = uy * 2000.0f;
	return powf(0.64f + 0.46f * cosf(ux * 6.28f), 1.7f) * f *
	       (o_get_stars(0.1f * ppx + itime * 20.0f, 0.1f * ppy + itime * -10.1f, 0.11f, 0.71f) * 4.0f +
		o_get_stars(0.2f * ppx + itime * 30.0f, 0.2f * ppy + itime * -10.1f, 0.1f, 0.31f) * 5.0f +
		o_get_stars(0.32f * ppx + itime * 40.0f, 0.32f * ppy + itime * -10.1f, 0.1f, 0.91f) * 2.0f);
}

static float o_sdf(float px, float py, float pz)
{
	px *= 2.0f;
	py *= 2.0f;
	pz *= 2.0f;
	float o = 8.2f * sinf(0.05f * px + itime * 0.25f) +
		  (0.04f * pz) * sinf(px * 0.11f + itime) * 2.0f * sinf(pz * 0.2f + itime) *
			  o_value2d(0.03f * px + itime * 0.5f, 0.4f * pz);
	/* normalize(vec3(0, 1, 0.05)) */
	const float ny = 0.99875234f, nz = 0.049937617f;
	return fabsf(py * ny + pz * nz + 2.5f + o * 0.5f);
}

/* (a, 1 − g/3) del raymarching; *steps_out: pasos dados. */
static f2 o_raymarch(float dx, float dy, float jitter, int *steps_out)
{
	float t = jitter * 2.0f, a = 0.0f, g = FONDO_MAX_DIST;
	int dr = 0, i;
	for (i = 0; i < 100; i++) {
		float pz = t, px = dx * t, py = dy * t;
		float ndt = o_sdf(px, py, pz);
		g = t > 10.0f ? fminf(g, fabsf(ndt)) : FONDO_MAX_DIST;
		if (t >= FONDO_MAX_DIST)
			break;
		if (fabsf(ndt) < FONDO_MIN_DIST) {
			if (dr > FONDO_MAX_DRAWS)
				break;
			dr++;
			float f = smoothstepf(0.0f, 0.3f, (pz * 0.9f) / 100.0f);
			a += 0.015f * f;
			t += 0.05f;
		} else
			t += fabsf(ndt) * 0.8f;
	}
	if (steps_out)
		*steps_out = i;
	g /= 3.0f;
	return (f2){ a, fmaxf(1.0f - g, 0.0f) };
}

static void o_render(float Ux, float Uy, float out[3])
{
	float ux = Ux / W, uy = Uy / H;
	float dx = (Ux - 0.5f * W) / H, dy = (Uy - 0.5f * H) / H;
	f2 mg = o_raymarch(dx, dy, fondo_dither(Ux, Uy), NULL);
	float c[3];
	fondo_gradient(iday, ux, uy, c);
	float m = mg.x < 0 ? 0 : mg.x > 1 ? 1 : mg.x;
	float dust = o_get_dust(ux, uy, mg.y) * 0.3f;
	for (int k = 0; k < 3; k++)
		out[k] = c[k] + (1.0f - c[k]) * m + dust;
}

static void o_pixel(int x, int y)
{
	float tot[3] = { 0, 0, 0 };
	float Ux = x + 0.5f, Uy = y + 0.5f;  /* gl_FragCoord: centro del píxel */
	for (int m = 0; m < 2; m++)
		for (int n = 0; n < 2; n++) {
			float c[3];
			o_render(Ux + m * 0.5f - 0.5f, Uy + n * 0.5f - 0.5f, c);
			for (int k = 0; k < 3; k++)
				tot[k] += c[k];
		}
	float d = (fondo_dither(Ux, Uy) - 0.5f) / 255.0f;
	for (int k = 0; k < 3; k++)
		img[((size_t)y * W + x) * 3 + k] = tot[k] / 4.0f + d;
}

/* ---- Hilos --------------------------------------------------------------------- */

static void (*row_fn)(int y);
static int next_row;
static pthread_mutex_t row_lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
	(void)arg;
	for (;;) {
		pthread_mutex_lock(&row_lock);
		int y = next_row++;
		pthread_mutex_unlock(&row_lock);
		if (y >= H)
			return NULL;
		row_fn(y);
	}
}

static void run_rows(void (*fn)(int y))
{
	row_fn = fn;
	next_row = 0;
	pthread_t th[16];
	for (int i = 0; i < 16; i++)
		pthread_create(&th[i], NULL, worker, NULL);
	for (int i = 0; i < 16; i++)
		pthread_join(th[i], NULL);
}

static void o_row(int y)
{
	for (int x = 0; x < W; x++)
		o_pixel(x, y);
}

/* ---- La versión del RSX ----------------------------------------------------------
 *
 * Las mismas cuentas que src/frontend/fondo/, en flotante de 32 bits,
 * sobre lo que prepara src/frontend/fondo_calc.c. Las texturas se leen como
 * en el RSX: RGBA8 normalizado con filtro bilineal (pesos de 8 bits) y fp16
 * para la ola y el polvo; la malla se rasteriza con la regla de arriba a la
 * izquierda, con la t corregida por perspectiva y la suma redondeada a fp16
 * en cada fragmento, como la mezcla del RSX en una superficie fp16. */

static struct fondo_view vw;
static struct fondo_tex tx;
static struct fondo_frame fr;
static uint16_t *glow;       /* gw × gh, fp16 */
static uint32_t gw, gh;
static uint16_t *htab;       /* lw / 4 filas × FONDO_NT × 4, fp16 */
static float *low;           /* lw × lh × 2: t de parada y polvo (fp16) */
static uint32_t frags;
static float texf2(const float *t, uint32_t tw, uint32_t th, float u, float v, int ch);

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float fracf(float x) { return x - floorf(x); }

/* Lectura bilineal de un canal de una textura RGBA8 que se repite. */
static float tex8(const uint8_t *t, uint32_t tw, uint32_t th, float u, float v, int ch)
{
	float x = u * tw - 0.5f, y = v * th - 0.5f;
	float fx = floorf(x), fy = floorf(y);
	/* El RSX filtra con 8 bits de fracción. */
	float wx = floorf((x - fx) * 256.0f) / 256.0f, wy = floorf((y - fy) * 256.0f) / 256.0f;
	int64_t ix = (int64_t)fx, iy = (int64_t)fy;
	float s[2][2];
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++)
			s[j][i] = t[(((uint64_t)(iy + j) & (th - 1)) * tw + ((uint64_t)(ix + i) & (tw - 1))) * 4 + ch] /
				  255.0f;
	float a = s[0][0] + (s[0][1] - s[0][0]) * wx, b = s[1][0] + (s[1][1] - s[1][0]) * wx;
	return a + (b - a) * wy;
}

/* Lectura bilineal de un flotante con el borde repetido. */
static float texf(const float *t, uint32_t tw, uint32_t th, float u, float v)
{
	float x = u * tw - 0.5f, y = v * th - 0.5f;
	float fx = floorf(x), fy = floorf(y);
	float wx = floorf((x - fx) * 256.0f) / 256.0f, wy = floorf((y - fy) * 256.0f) / 256.0f;
	int ix = (int)fx, iy = (int)fy;
	float s[2][2];
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			int xx = ix + i, yy = iy + j;
			xx = xx < 0 ? 0 : xx >= (int)tw ? (int)tw - 1 : xx;
			yy = yy < 0 ? 0 : yy >= (int)th ? (int)th - 1 : yy;
			s[j][i] = t[(size_t)yy * tw + xx];
		}
	float a = s[0][0] + (s[0][1] - s[0][0]) * wx, b = s[1][0] + (s[1][1] - s[1][0]) * wx;
	return a + (b - a) * wy;
}

static float texf2(const float *t, uint32_t tw, uint32_t th, float u, float v, int ch)
{
	float x = u * tw - 0.5f, y = v * th - 0.5f;
	float fx = floorf(x), fy = floorf(y);
	float wx = floorf((x - fx) * 256.0f) / 256.0f, wy = floorf((y - fy) * 256.0f) / 256.0f;
	int ix = (int)fx, iy = (int)fy;
	float s[2][2];
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			int xx = ix + i, yy = iy + j;
			xx = xx < 0 ? 0 : xx >= (int)tw ? (int)tw - 1 : xx;
			yy = yy < 0 ? 0 : yy >= (int)th ? (int)th - 1 : yy;
			s[j][i] = t[((size_t)yy * tw + xx) * 2 + ch];
		}
	float a = s[0][0] + (s[0][1] - s[0][0]) * wx, b = s[1][0] + (s[1][1] - s[1][0]) * wx;
	return a + (b - a) * wy;
}

/* h − 2,5 − B·t de la columna i en t, con el filtro lineal del RSX a lo
 * largo de t (fondo_marcha.fcg, fondo_polvo.fcg). */
static float h_at(uint32_t i, float t)
{
	const uint16_t *row = htab + (size_t)(i / 4) * FONDO_NT * 4 + i % 4;
	float u = (t * ((FONDO_NT - 1) / FONDO_MAX_DIST) + 0.5f) * (1.0f / FONDO_NT);
	float x = u * FONDO_NT - 0.5f, fx = floorf(x);
	float w = floorf((x - fx) * 256.0f) / 256.0f;
	int j0 = (int)fx, j1 = j0 + 1;
	j0 = j0 < 0 ? 0 : j0 >= FONDO_NT ? FONDO_NT - 1 : j0;
	j1 = j1 < 0 ? 0 : j1 >= FONDO_NT ? FONDO_NT - 1 : j1;
	float a = fondo_unhalf(row[j0 * 4]), b = fondo_unhalf(row[j1 * 4]);
	return a + (b - a) * w;
}

/* fondo_marcha.fcg: los 100 pasos del original desde t = 1 con la h de la
 * tabla; fondo_polvo.fcg: g_min en t_k < t de parada; fondo_low.fcg: los
 * dos por píxel con el factor de los bordes. */
static void r_low(int j)
{
	for (uint32_t i = 0; i < vw.lw; i++) {
		/* TEXCOORD1.x de fondo_frame_consts. */
		float a = fr.low_k[0] + fr.low_ky[0] * (j + 0.5f), t = FONDO_T0;
		/* fondo_marcha.cgh: 4 pasadas de 25 pasos, t en fp16 entre ellas. */
		for (int p = 0; p < FONDO_STEPS / 25; p++) {
			for (int k = 0; k < 25; k++) {
				float g = fabsf(a * t + h_at(i, t) + 2.5f);
				t += g <= FONDO_MIN_DIST ? 0.05f : 0.8f * g;
			}
			t = fondo_unhalf(fondo_half(fminf(t, FONDO_MAX_DIST)));
		}
		float ts = t;
		float mn = 1000.0f, mx = -1000.0f;
		for (int k = 0; k < FONDO_TS; k++) {
			float tk = 10.0f + 30.0f * k / (FONDO_TS - 1);
			float g = h_at(i, tk) + (a * tk + 2.5f);
			float o = tk >= ts ? 1.0f : 0.0f;
			mn = fminf(mn, o * 1000.0f + g);
			mx = fmaxf(mx, g - o * 1000.0f);
		}
		float gmin = fmaxf(fmaxf(mn, -mx), 0.0f);
		float e = fondo_unhalf(fondo_half(clampf(1.0f - gmin * (1.0f / 3.0f), 0.0f, 1.0f)));
		float ux = (i + 0.5f) / vw.lw + fr.unpack_k[1];
		float edge = powf(0.64f + 0.46f * cosf(ux * 6.28f), 1.7f) * 0.3f;
		low[((size_t)j * vw.lw + i) * 2] = ts;
		low[((size_t)j * vw.lw + i) * 2 + 1] = fondo_unhalf(fondo_half(e * edge));
		(void)0;
	}
}

/* fondo_ola.fcg: F̃ de una tabla fp16 de 256 valores con filtro lineal. */
static float prim_fp(float t)
{
	static float tab[256];
	static int ready;
	if (!ready) {
		for (int i = 0; i < 256; i++)
			tab[i] = fondo_unhalf(fondo_half(fondo_prim(FONDO_MAX_DIST * i / 255.0f)));
		ready = 1;
	}
	float u = t * (255.0f / (40.0f * 256.0f)) + 0.5f / 256.0f;
	float x = u * 256.0f - 0.5f, fx = floorf(x);
	float w = floorf((x - fx) * 256.0f) / 256.0f;
	int j0 = (int)fx, j1 = j0 + 1;
	j0 = j0 < 0 ? 0 : j0 > 255 ? 255 : j0;
	j1 = j1 < 0 ? 0 : j1 > 255 ? 255 : j1;
	return tab[j0] + (tab[j1] - tab[j0]) * w;
}

/* Rasteriza triángulos indexados de `v` sumando sgn·F̃(t) en `glow`.
 * cull: 0 ninguno, 1 quita los de frente (antihorario con la y hacia
 * arriba), 2 los de atrás. */
static int clamp_t = 1, lin_from = 6;
static void raster(const fondo_vertex *v, const uint16_t *idx, uint32_t n, int cull, float sgn)
{
	for (uint32_t k = 0; k + 2 < n; k += 3) {
		float X[3], Y[3], iw[3], tw[3];
		for (int e = 0; e < 3; e++) {
			const fondo_vertex *p = &v[idx[k + e]];
			float w = p->pos[3];
			/* Ventana con la y hacia abajo, a 1/16 de píxel. */
			X[e] = roundf(((p->pos[0] / w) + 1.0f) * 0.5f * gw * 16.0f) / 16.0f;
			Y[e] = roundf((1.0f - (p->pos[1] / w)) * 0.5f * gh * 16.0f) / 16.0f;
			iw[e] = 1.0f / w;
			tw[e] = p->t0[0] / w;
		}
		/* Área con la y hacia arriba: positiva = antihorario = frente. */
		float area = -((X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]));
		if (area == 0.0f || (cull == 1 && area > 0) || (cull == 2 && area < 0))
			continue;
		/* Funciones de arista en la ventana (y abajo), orientadas para que
		 * el interior sea positivo. */
		float s = area > 0 ? 1.0f : -1.0f;
		int x0 = (int)floorf(fminf(X[0], fminf(X[1], X[2]))), x1 = (int)ceilf(fmaxf(X[0], fmaxf(X[1], X[2])));
		int y0 = (int)floorf(fminf(Y[0], fminf(Y[1], Y[2]))), y1 = (int)ceilf(fmaxf(Y[0], fmaxf(Y[1], Y[2])));
		x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
		x1 = x1 > (int)gw ? (int)gw : x1, y1 = y1 > (int)gh ? (int)gh : y1;
		for (int y = y0; y < y1; y++)
			for (int x = x0; x < x1; x++) {
				float px = x + 0.5f, py = y + 0.5f, l[3];
				bool in = true;
				for (int e = 0; e < 3 && in; e++) {
					int a = (e + 1) % 3, b = (e + 2) % 3;
					/* Arista a → b, el vértice e enfrente. */
					float ex = X[b] - X[a], ey = Y[b] - Y[a];
					float f = s * ((px - X[a]) * ey - (py - Y[a]) * ex);
					/* Regla de arriba a la izquierda (y abajo): las aristas
					 * de arriba y de la izquierda cuentan en el borde. */
					bool top_left = (s * ey > 0) || (ey == 0 && -s * ex > 0);
					if (f < 0 || (f == 0 && !top_left))
						in = false;
					l[e] = f;
				}
				if (!in)
					continue;
				float sum = l[0] + l[1] + l[2];
				float b0 = l[0] / sum, b1 = l[1] / sum, b2 = l[2] / sum;
				float t = (b0 * tw[0] + b1 * tw[1] + b2 * tw[2]) / (b0 * iw[0] + b1 * iw[1] + b2 * iw[2]);
				if (clamp_t)
					t = fminf(t, texf2(low, vw.lw, vw.lh, (x + 0.5f) / gw, (y + 0.5f) / gh, 0));
				uint16_t *d = &glow[(size_t)y * gw + x];
				float src = fondo_unhalf(fondo_half(sgn * prim_fp(t)));
				*d = fondo_half(fondo_unhalf(*d) + src);
				frags++;
			}
	}
}

static float glow_at(float u, float v)
{
	/* La superficie de la ola, con NEAREST si es de la pantalla y bilineal si no. */
	if (gw == vw.w && gh == vw.h) {
		uint32_t x = (uint32_t)(u * gw), y = (uint32_t)(v * gh);
		return fondo_unhalf(glow[(size_t)y * gw + x]);
	}
	static float *gf;
	static uint32_t gfn;
	if (gfn != gw * gh) {
		free(gf);
		gf = malloc(sizeof *gf * gw * gh);
		gfn = gw * gh;
		for (uint32_t i = 0; i < gfn; i++)
			gf[i] = fondo_unhalf(glow[i]);
	}
	return texf(gf, gw, gh, u, v);
}

/* Una constante afín de la pasada final en el centro del píxel. */
static void attr(int j, int px, int py, float out[4])
{
	for (int c = 0; c < 4; c++)
		out[c] = fr.k[j][c] + fr.kx[j][c] * (px + 0.5f) + fr.ky[j][c] * (py + 0.5f);
}

static void r_row(int py)
{
	for (int px = 0; px < W; px++) {
		/* fondo_final.cgh: TEXCOORD0.zw y TEXCOORD1..5. */
		float zu = (px + 0.5f) / W, zv = (py + 0.5f) / H;
		float p12[4], p3b3[4], b12[4], lin[4], xy[4];
		attr(0, px, py, p12);
		attr(1, px, py, p3b3);
		attr(2, px, py, b12);
		attr(3, px, py, lin);
		attr(4, px, py, xy);
		float c[3];
		for (int i = 0; i < 3; i++)
			c[i] = xy[i] * xy[3] + lin[i];
		float a = fminf(clampf(glow_at(zu, zv), 0.0f, 1.0f), 0.615f);
		for (int i = 0; i < 3; i++)
			c[i] = c[i] * (1.0f - a) + a;
		if ((uint32_t)py >= fr.band_y0 && (uint32_t)py < fr.band_y1) {
			const float inv[4] = { 1.0f / 2048, 1.0f / 1024, 1.0f / 2048, 1.0f / 1024 };
			float cc[4];
			for (int i = 0; i < 4; i++) {
				float p = p12[i], f = fracf(p);
				cc[i] = (p - f + f * f * (3.0f - 2.0f * f)) * inv[i] + 0.5f * inv[i];
				if (i >= lin_from)
					cc[i] = (p + 0.5f) * inv[i];
			}
			float s[3] = { tex8(tx.star, FONDO_STAR_W, FONDO_STAR_H, cc[0], cc[1], 0),
				       tex8(tx.star, FONDO_STAR_W, FONDO_STAR_H, cc[2], cc[3], 1),
				       tex8(tx.star, FONDO_STAR_W, FONDO_STAR_H, p3b3[0], p3b3[1], 2) };
			const float A[3] = { 0.11f, 0.1f, 0.1f }, T[3] = { 0.71f, 0.31f, 0.91f }, wgt[3] = { 4, 5, 2 };
			float b[3] = { tex8(tx.bri, FONDO_BRI_W, FONDO_BRI_H, b12[0], b12[1], 0),
				       tex8(tx.bri, FONDO_BRI_W, FONDO_BRI_H, b12[2], b12[3], 1),
				       tex8(tx.bri, FONDO_BRI_W, FONDO_BRI_H, p3b3[2], p3b3[3], 2) };
			float dust = 0;
			for (int i = 0; i < 3; i++) {
				float x = clampf(s[i] * (1.0f / T[i]) - A[i] / T[i], 0.0f, 1.0f);
				x = x * x * (3.0f - 2.0f * x);
				dust += x * b[i] * wgt[i];
			}
			float e = texf2(low, vw.lw, vw.lh, zu, zv, 1);
			for (int i = 0; i < 3; i++)
				c[i] += e * dust;
		}
		float d = fracf(fracf(lin[3]) * 52.9829189f);
		for (int i = 0; i < 3; i++)
			img[((size_t)(H - 1 - py) * W + px) * 3 + i] = c[i] + (d * (1.0f / 255.0f) - 0.5f / 255.0f);
	}
}

static void rsx_render(uint32_t glow_div, uint32_t low_div)
{
	uint32_t lw = ((uint32_t)W / low_div + 3) & ~3u;
	vw = (struct fondo_view){ (uint32_t)W, (uint32_t)H, lw, (uint32_t)H / low_div };
	gw = vw.w / glow_div, gh = vw.h / glow_div;
	double T = itime;
	tx.star = calloc((size_t)FONDO_STAR_W * FONDO_STAR_H * 4, 1);
	tx.bri = calloc((size_t)FONDO_BRI_W * FONDO_BRI_H * 4, 1);
	fondo_tex_reset(&tx);
	fondo_tex_update(&tx, &vw, T);
	fondo_frame_consts(&fr, &vw, T, iday);

	htab = calloc((size_t)(vw.lw / 4) * FONDO_NT * 4, sizeof *htab);
	fondo_htable(htab, FONDO_NT * 8, &vw, T);
	low = calloc((size_t)vw.lw * vw.lh * 2, sizeof *low);
	int saveH = H;
	H = (int)vw.lh;
	run_rows(r_low);
	H = saveH;
	if (getenv("FONDO_LINEAL_DESDE"))
		lin_from = atoi(getenv("FONDO_LINEAL_DESDE"));
	if (getenv("FONDO_SIN_PARADA"))
		clamp_t = 0;

	fondo_vertex *v = malloc(sizeof *v * FONDO_VERTS);
	uint16_t *idx = malloc(sizeof *idx * FONDO_INDICES);
	fondo_indices(idx);
	fondo_mesh(v, &vw, T);
	glow = calloc((size_t)gw * gh, sizeof *glow);
	/* Superficie +0,04: de frente se entra (−), de atrás se sale (+); la
	 * −0,04 al revés. Las tapas, sin descartar caras. */
	raster(v + FONDO_VERT_SURF_P, idx, FONDO_SURF_INDICES, 2, -1.0f);
	raster(v + FONDO_VERT_SURF_P, idx, FONDO_SURF_INDICES, 1, +1.0f);
	raster(v + FONDO_VERT_SURF_N, idx, FONDO_SURF_INDICES, 2, +1.0f);
	raster(v + FONDO_VERT_SURF_N, idx, FONDO_SURF_INDICES, 1, -1.0f);
	raster(v + FONDO_VERT_CAP_FAR, idx + FONDO_SURF_INDICES, FONDO_CAP_INDICES, 0, +1.0f);
	raster(v + FONDO_VERT_CAP_NEAR, idx + FONDO_SURF_INDICES, FONDO_CAP_INDICES, 0, -1.0f);

	run_rows(r_row);
	fprintf(stderr, "rsx: %u fragmentos de la ola (%.2f por píxel de %ux%u), reducida %ux%u, polvo en las filas "
			"%u..%u, %u texeles de las tablas\n",
		frags, (double)frags / (gw * gh), gw, gh, vw.lw, vw.lh, fr.band_y0, fr.band_y1, tx.filled);
}

/* ---- Volcados de la PS3 o de RPCS3 ------------------------------------------------ */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* Una superficie fp16 que dejó `fondo-volcado` (cabecera w, h y 4 fp16 por
 * texel, en el orden de la PS3): el canal c, en flotante. */
static float *read_f16(const char *path, uint32_t *w, uint32_t *h, int c)
{
	FILE *f = fopen(path, "rb");
	uint8_t hdr[8];
	if (!f || fread(hdr, 8, 1, f) != 1) {
		if (f)
			fclose(f);
		return NULL;
	}
	*w = be32(hdr), *h = be32(hdr + 4);
	size_t n = (size_t)*w * *h;
	uint8_t *raw = malloc(n * 8);
	float *out = malloc(n * sizeof *out);
	if (fread(raw, 8, n, f) != n) {
		fclose(f);
		free(raw);
		free(out);
		return NULL;
	}
	fclose(f);
	for (size_t i = 0; i < n; i++)
		out[i] = fondo_unhalf((uint16_t)(raw[i * 8 + c * 2] << 8 | raw[i * 8 + c * 2 + 1]));
	free(raw);
	return out;
}

static void write_gray(const char *path, const float *v, uint32_t w, uint32_t h, float scale)
{
	FILE *f = fopen(path, "wb");
	fprintf(f, "P5\n%u %u\n255\n", w, h);
	for (size_t i = 0; i < (size_t)w * h; i++) {
		float x = v[i] * scale;
		fputc(x < 0 ? 0 : x > 255 ? 255 : (int)(x + 0.5f), f);
	}
	fclose(f);
}

/* Compara la ola (a = clamp(suma, 0, 0,615)) y la t de parada volcadas con
 * las de la emulación en el mismo iTime. */
static int compare_dump(const char *ola, const char *lowp)
{
	uint32_t w, h, lw2, lh2;
	float *d = read_f16(ola, &w, &h, 0);
	if (!d || (int)w != W || (int)h != H) {
		fprintf(stderr, "%s: no es una ola de %dx%d\n", ola, W, H);
		return 1;
	}
	rsx_render(1, 8);
	double sum = 0, mx = 0;
	size_t big = 0, n = (size_t)w * h;
	float *e = malloc(n * sizeof *e), *dd = malloc(n * sizeof *dd);
	for (size_t i = 0; i < n; i++) {
		float a = fminf(fmaxf(fondo_unhalf(glow[i]), 0.0f), 0.615f), b = fminf(fmaxf(d[i], 0.0f), 0.615f);
		float df = fabsf(a - b);
		sum += df;
		mx = df > mx ? df : mx;
		big += df > 4.0f / 255.0f;
		e[i] = a, dd[i] = b;
	}
	printf("ola: diferencia de a promedio %.5f, máxima %.4f, %.3f %% de los píxeles a más de 4/255\n", sum / n, mx,
	       100.0 * big / n);
	write_gray("ola-emulada.pgm", e, w, h, 400.0f);
	write_gray("ola-volcada.pgm", dd, w, h, 400.0f);
	if (lowp) {
		float *l = read_f16(lowp, &lw2, &lh2, 0);
		if (l && lw2 == vw.lw && lh2 == vw.lh) {
			double s2 = 0, m2 = 0;
			for (size_t i = 0; i < (size_t)lw2 * lh2; i++) {
				float df = fabsf(l[i] - low[i * 2]);
				s2 += df;
				m2 = df > m2 ? df : m2;
			}
			printf("t de parada: diferencia promedio %.4f, máxima %.3f\n", s2 / ((size_t)lw2 * lh2), m2);
		}
	}
	return 0;
}

/* ---- Comparación ---------------------------------------------------------------- */

static int compare(const char *pa, const char *pb, const char *pd)
{
	int wa, ha, wb, hb;
	unsigned char *a = read_ppm(pa, &wa, &ha), *b = read_ppm(pb, &wb, &hb);
	if (!a || !b || wa != wb || ha != hb) {
		fprintf(stderr, "no se pueden comparar %s y %s\n", pa, pb);
		return 1;
	}
	size_t n = (size_t)wa * ha * 3;
	double se = 0, sum = 0;
	int maxd = 0;
	unsigned long hist[256] = { 0 };
	for (size_t i = 0; i < n; i++) {
		int d = abs((int)a[i] - (int)b[i]);
		se += (double)d * d;
		sum += d;
		hist[d]++;
		if (d > maxd)
			maxd = d;
	}
	double mse = se / n, psnr = mse > 0 ? 10.0 * log10(255.0 * 255.0 / mse) : INFINITY;
	unsigned long acc = 0;
	int p999 = 0;
	for (int d = 0; d < 256; d++) {
		acc += hist[d];
		if (acc >= n * 0.999) {
			p999 = d;
			break;
		}
	}
	printf("PSNR %.2f dB, promedio %.3f/255, p99,9 %d, máximo %d, a 1 o menos %.2f %%, a 4 o menos %.2f %%\n", psnr,
	       sum / n, p999, maxd, 100.0 * (hist[0] + hist[1]) / n,
	       100.0 * (hist[0] + hist[1] + hist[2] + hist[3] + hist[4]) / n);
	if (pd) {
		FILE *f = fopen(pd, "wb");
		fprintf(f, "P6\n%d %d\n255\n", wa, ha);
		for (size_t i = 0; i < n; i++) {
			int d = abs((int)a[i] - (int)b[i]) * 8;
			fputc(d > 255 ? 255 : d, f);
		}
		fclose(f);
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 4 && !strcmp(argv[1], "compara"))
		return compare(argv[2], argv[3], argc > 4 ? argv[4] : NULL);
	if (argc >= 6 && !strcmp(argv[1], "ola")) {
		W = atoi(argv[2]);
		H = atoi(argv[3]);
		itime = (float)atof(argv[4]);
		img = calloc((size_t)W * H * 3, sizeof *img);
		return compare_dump(argv[5], argc > 6 ? argv[6] : NULL);
	}
	if (argc != 7) {
		fprintf(stderr, "uso: fondo-ref original|rsx ANCHO ALTO ITIME HORA SALIDA.ppm\n"
				"     fondo-ref compara A.ppm B.ppm [DIFERENCIA.ppm]\n");
		return 2;
	}
	W = atoi(argv[2]);
	H = atoi(argv[3]);
	itime = (float)atof(argv[4]);
	iday = (float)(atof(argv[5]) * 3600.0);
	img = calloc((size_t)W * H * 3, sizeof *img);
	if (!strcmp(argv[1], "original"))
		run_rows(o_row);
	else if (!strcmp(argv[1], "rsx")) {
		const char *g = getenv("FONDO_OLA_DIV"), *p = getenv("FONDO_LOW_DIV");
		rsx_render(g ? (uint32_t)atoi(g) : 1, p ? (uint32_t)atoi(p) : 8);
	} else {
		fprintf(stderr, "modo desconocido: %s\n", argv[1]);
		return 2;
	}
	write_ppm(argv[6]);
	return 0;
}
