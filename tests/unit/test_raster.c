/*
 * Salsa64 — pruebas del rasterizador de referencia del host
 * (src/host/raster.c): el espejo del framebuffer en la RDRAM. Las tareas
 * son salidas de la HLE armadas a mano (un TARGET y un DRAW que pinta toda
 * la imagen de un color) y los campos, imágenes del VI; se mira la RDRAM.
 */
#include "test.h"

#include <string.h>

#include "core/endian.h"
#include "core/rcp.h"
#include "host/raster.h"

#define W     320u
#define H     240u
#define LINE  (W * 2)
#define FB    (LINE * H)
/* Tres framebuffers seguidos, como los de un juego con triple búfer. */
#define FB_A  0x10000u
#define FB_B  (FB_A + FB)
#define FB_C  (FB_B + FB)

static uint8_t ram[FB_C + FB] __attribute__((aligned(16)));
static struct gfx_vertex vtx[6] __attribute__((aligned(16)));
static uint8_t cmd[sizeof(struct gfx_cmd) + sizeof(struct gfx_draw) + sizeof(struct gfx_cmd)]
	__attribute__((aligned(16)));

static uint16_t rgba5551(uint32_t r, uint32_t g, uint32_t b)
{
	return (uint16_t)((r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | 1);
}

static uint16_t px(uint32_t fb, uint32_t x, uint32_t y)
{
	return salsa64_rd16(ram + fb + (y * W + x) * 2);
}

static void set_vtx(struct gfx_vertex *v, float x, float y)
{
	memset(v, 0, sizeof *v);
	v->x = x;
	v->y = y;
	v->w = 1.0f;
}

/* Una tarea con un FILLRECT de `x0, y0`–`x1, y1` sobre una color image de
 * `fb`/`w` cuyas tijeras llegan a la fila `lh` (más de una pantalla). */
static void task_tall_fill(uint32_t fb, uint32_t w, uint32_t lh, float x0, float y0, float x1, float y1, uint32_t r,
			   uint32_t g, uint32_t b)
{
	set_vtx(&vtx[0], x0, y0);
	set_vtx(&vtx[1], x1, y0);
	set_vtx(&vtx[2], x0, y1);
	set_vtx(&vtx[3], x1, y0);
	set_vtx(&vtx[4], x1, y1);
	set_vtx(&vtx[5], x0, y1);
	memset(cmd, 0, sizeof cmd);
	struct gfx_cmd *t = (struct gfx_cmd *)cmd;
	t->op = GFX_OP_TARGET;
	t->size = sizeof *t;
	t->a = fb;
	t->b = w;
	t->c = 2;
	struct gfx_draw *d = (struct gfx_draw *)(cmd + sizeof *t);
	d->h.op = GFX_OP_DRAW;
	d->h.size = sizeof *d;
	d->h.a = 0;
	d->h.b = 6;
	d->cycle = 3;  /* G_CYC_FILL */
	d->scissor[2] = (uint16_t)(w * 4);
	d->scissor[3] = (uint16_t)(lh * 4);
	d->k[GFX_K_ONE][0][0] = r / 255.0f;
	d->k[GFX_K_ONE][0][1] = g / 255.0f;
	d->k[GFX_K_ONE][0][2] = b / 255.0f;
	d->k[GFX_K_ONE][0][3] = 1.0f;
	struct gfx_job job;
	struct gfx_result res;
	memset(&job, 0, sizeof job);
	memset(&res, 0, sizeof res);
	job.rdram_ea = (uint64_t)(uintptr_t)ram;
	job.rdram_size = sizeof ram;
	job.vtx_ea = (uint64_t)(uintptr_t)vtx;
	job.cmd_ea = (uint64_t)(uintptr_t)cmd;
	res.cmd_bytes = sizeof cmd;
	raster_task(&job, &res);
}

/* Lo mismo con un dibujo normal (1 ciclo) en vez de un relleno. */
static void task_tall_draw(uint32_t fb, uint32_t w, uint32_t lh, float x0, float y0, float x1, float y1, uint32_t r,
			   uint32_t g, uint32_t b)
{
	task_tall_fill(fb, w, lh, x0, y0, x1, y1, r, g, b);  /* arma el comando... */
	((struct gfx_draw *)(cmd + sizeof(struct gfx_cmd)))->cycle = 0;  /* ...y se repite como 1 ciclo */
	struct gfx_job job;
	struct gfx_result res;
	memset(&job, 0, sizeof job);
	memset(&res, 0, sizeof res);
	job.rdram_ea = (uint64_t)(uintptr_t)ram;
	job.rdram_size = sizeof ram;
	job.vtx_ea = (uint64_t)(uintptr_t)vtx;
	job.cmd_ea = (uint64_t)(uintptr_t)cmd;
	res.cmd_bytes = sizeof cmd;
	raster_task(&job, &res);
}

/* Una tarea que pinta toda la color image `fb` (`w` de ancho, 16 bits). */
static void task_fill_w(uint32_t fb, uint32_t w, uint32_t r, uint32_t g, uint32_t b)
{
	memset(cmd, 0, sizeof cmd);
	struct gfx_cmd *t = (struct gfx_cmd *)cmd;
	t->op = GFX_OP_TARGET;
	t->size = sizeof *t;
	t->a = fb;
	t->b = w;
	t->c = 2;  /* G_IM_SIZ_16b, G_IM_FMT_RGBA */
	struct gfx_draw *d = (struct gfx_draw *)(cmd + sizeof *t);
	d->h.op = GFX_OP_DRAW;
	d->h.size = sizeof *d;
	d->h.a = 0;
	d->h.b = 6;
	d->scissor[2] = (uint16_t)(w * 4);
	d->scissor[3] = (uint16_t)(w * 3 / 4 * 4);
	d->k[GFX_K_ONE][0][0] = r / 255.0f;
	d->k[GFX_K_ONE][0][1] = g / 255.0f;
	d->k[GFX_K_ONE][0][2] = b / 255.0f;
	d->k[GFX_K_ONE][0][3] = 1.0f;
	float fw = (float)w, fh = (float)(w * 3 / 4);
	set_vtx(&vtx[0], 0, 0);
	set_vtx(&vtx[1], fw, 0);
	set_vtx(&vtx[2], 0, fh);
	set_vtx(&vtx[3], fw, 0);
	set_vtx(&vtx[4], fw, fh);
	set_vtx(&vtx[5], 0, fh);

	struct gfx_job job;
	struct gfx_result res;
	memset(&job, 0, sizeof job);
	memset(&res, 0, sizeof res);
	job.rdram_ea = (uint64_t)(uintptr_t)ram;
	job.rdram_size = sizeof ram;
	job.vtx_ea = (uint64_t)(uintptr_t)vtx;
	job.cmd_ea = (uint64_t)(uintptr_t)cmd;
	res.cmd_bytes = sizeof cmd;  /* el último gfx_cmd queda en 0: GFX_OP_END */
	raster_task(&job, &res);
}

static void task_fill(uint32_t fb, uint32_t r, uint32_t g, uint32_t b)
{
	task_fill_w(fb, W, r, g, b);
}

/* Un campo del VI mostrando el framebuffer `fb` (desde su línea 1, como SM64). */
static void field(uint32_t fb)
{
	struct salsa64_vi_frame f;
	memset(&f, 0, sizeof f);
	f.type = SALSA64_VI_RGBA5551;
	f.origin = fb + LINE;
	f.width = W;
	f.height = H - 3;
	f.stride = LINE;
	f.bpp = 2;
	raster_vi(&f);
}

/* Lo que un DMA del juego deja en la memoria de un framebuffer. */
static void dma_data(uint32_t fb)
{
	for (uint32_t y = 20; y < 40; y++)
		for (uint32_t x = 0; x < LINE; x++)
			ram[fb + y * LINE + x] = (uint8_t)(0xA5 ^ x ^ y);
}

static int data_intact(uint32_t fb)
{
	for (uint32_t y = 20; y < 40; y++)
		for (uint32_t x = 0; x < LINE; x++)
			if (ram[fb + y * LINE + x] != (uint8_t)(0xA5 ^ x ^ y))
				return 0;
	return 1;
}

/* Super Mario 64 al pasar del logo a la cara de Mario, sin datos del juego:
 * triple búfer de 320 × 240; el RDP dibuja B, el VI sigue dos campos en A,
 * un DMA carga datos encima de A y de B, y el VI muestra B (y A) sin que
 * el RDP vuelva a dibujarlos. El espejo copiaba el framebuffer presentado
 * en cada campo y pisaba esos datos: el juego dejaba de mandar tareas
 * gráficas (la 96 era la última). Ahora lo dibujado se copia una vez, al
 * terminar la tarea si ya se sabe que es un framebuffer y si no al
 * presentarlo por primera vez. */
static void raster_frame_mirror_sm64(void)
{
	const uint32_t fb[3] = { FB_A, FB_B, FB_C };
	int fbfx = raster_fbfx;
	raster_reset();
	raster_fbfx = 2;
	memset(ram, 0, sizeof ram);

	/* El arranque: tareas que solo dibujan una imagen chica lejos de los
	 * framebuffers (una copia reciente protege su memoria durante
	 * RB_KEEP_TASKS tareas, y al empezar todas cuentan como recientes). */
	for (int k = 0; k < 40; k++)
		task_fill_w(0x1000, 64, 0, 0, 0);

	/* Antes de que el VI muestre nada no se sabe que es un framebuffer: el
	 * primer cuadro llega a la RDRAM al presentarlo. */
	task_fill(FB_A, 255, 0, 0);
	CHECK_EQ(px(FB_A, 10, 10), 0);
	field(FB_A);
	CHECK_EQ(px(FB_A, 10, 10), rgba5551(255, 0, 0));
	CHECK_EQ(px(FB_A, 319, 237), rgba5551(255, 0, 0));
	field(FB_A);

	/* Triple búfer: cada tarea dibuja uno y el VI lo muestra dos campos.
	 * Los ya mostrados alguna vez se copian al terminar la tarea. */
	for (uint32_t k = 1; k < 7; k++) {
		uint32_t f = fb[k % 3], r = 30 * k;
		task_fill(f, r, 0, 0);
		if (k >= 3)
			CHECK_EQ(px(f, 10, 10), rgba5551(r, 0, 0));
		field(f);
		field(f);
		CHECK_EQ(px(f, 10, 10), rgba5551(r, 0, 0));
	}

	/* El cambio de escena. */
	task_fill(FB_B, 0, 255, 0);
	CHECK_EQ(px(FB_B, 10, 10), rgba5551(0, 255, 0));
	field(FB_A);
	field(FB_A);
	dma_data(FB_A);
	dma_data(FB_B);
	field(FB_B);
	field(FB_B);
	field(FB_A);
	CHECK(data_intact(FB_A));
	CHECK(data_intact(FB_B));
	CHECK_EQ(px(FB_B, 10, 10), rgba5551(0, 255, 0));  /* lo que el RDP dejó fuera del DMA */

	/* Lo que el RDP vuelve a dibujar sí se copia. */
	task_fill(FB_A, 0, 0, 255);
	CHECK_EQ(px(FB_A, 10, 30), rgba5551(0, 0, 255));

	raster_reset();
	raster_fbfx = fbfx;
}

/* Star Fox 64: una tarea limpia el framebuffer y, antes de que termine, el
 * juego carga por DMA datos encima; el espejo al terminar la tarea no debe
 * pisarlos. Un DMA anterior a la tarea sí se pisa (la copia es más nueva). */
static void raster_mirror_after_dma(void)
{
	raster_reset();
	memset(ram, 0, sizeof ram);
	for (int k = 0; k < 40; k++)
		task_fill_w(0x1000, 64, 0, 0, 0);
	task_fill(FB_A, 255, 0, 0);
	field(FB_A);
	field(FB_A);
	uint32_t t = 41;  /* tareas hechas: la próxima es la 42 */
	/* DMA antes de la tarea 42: la tarea lo pisa, como el RDP. */
	dma_data(FB_A);
	raster_rdram_written(FB_A, FB, t);
	task_fill(FB_A, 0, 255, 0);
	CHECK_EQ(px(FB_A, 10, 30), rgba5551(0, 255, 0));
	/* DMA mientras corre la tarea 43 (en vuelo: 43): el espejo no lo pisa. */
	t = 43;
	dma_data(FB_A);
	raster_rdram_written(FB_A + 20 * LINE, 20 * LINE, t);
	task_fill(FB_A, 0, 0, 255);
	CHECK(data_intact(FB_A));
	CHECK_EQ(px(FB_A, 10, 10), rgba5551(0, 255, 0));  /* el cuadro anterior sigue ahí */
	/* La tarea 44 vuelve a dibujar después del DMA: se copia. */
	task_fill(FB_A, 0, 0, 255);
	CHECK_EQ(px(FB_A, 10, 30), rgba5551(0, 0, 255));
	raster_reset();
}

/* Star Fox 64: FILLRECT de 320 × 720 sobre una color image en el primer
 * framebuffer, que limpia los tres seguidos (y su borde de 8 px, que el
 * juego nunca vuelve a tocar). No se crea un destino de 720: el relleno va
 * a cada framebuffer existente. */
static void raster_tall_fill(void)
{
	raster_reset();
	memset(ram, 0, sizeof ram);
	task_fill(FB_A, 255, 0, 0);
	task_fill(FB_B, 0, 255, 0);
	task_fill(FB_C, 0, 0, 255);
	task_tall_fill(FB_A, W, 3 * H, 0, 0, (float)W, 3.0f * H, 40, 50, 60);
	uint32_t w = 0;
	const uint32_t *a = raster_image(FB_A, &w), *b = raster_image(FB_B, &w), *c = raster_image(FB_C, &w);
	CHECK(a && b && c);
	if (!a || !b || !c)
		return;
	CHECK_EQ(a[3 * W + 3], 0x28323cu);  /* (40, 50, 60) */
	CHECK_EQ(b[100 * W + 200], 0x28323cu);
	CHECK_EQ(c[(H - 1) * W + (W - 1)], 0x28323cu);
	/* Solo las filas 500–520 de la imagen alta: la parte alta de C. */
	task_tall_fill(FB_A, W, 3 * H, 0, 500, (float)W, 520, 1, 2, 3);
	CHECK_EQ(c[25 * W + 7], 0x010203u);
	CHECK_EQ(c[41 * W + 7], 0x28323cu);
	CHECK_EQ(b[100 * W + 200], 0x28323cu);
	/* Donkey Kong 64: 459 filas sobre dos búferes (más de pantalla y
	 * media): también se reparte, sin crear un destino de 320 × 459. */
	task_tall_fill(FB_A, W, 459, 0, 0, (float)W, 459, 9, 8, 7);
	CHECK_EQ(a[0], 0x090807u);
	CHECK_EQ(b[218 * W + 5], 0x090807u);
	CHECK_EQ(b[230 * W + 5], 0x28323cu);
	uint32_t bw = 0;
	CHECK(raster_image(FB_A, &bw) == a);  /* sigue siendo el de 240 */
	CHECK(raster_image(FB_A + 250 * LINE, &bw) == b);  /* no hay un destino de 459 */
	/* Con un dibujo de verdad (la transición del cierre de DK64, que el CPU
	 * lee después) la imagen de 320 × 459 sí se crea tal cual. */
	task_tall_draw(FB_A, W, 459, 0, 0, (float)W, 459, 100, 110, 120);
	const uint32_t *t = raster_image(FB_A + 250 * LINE, &bw);
	CHECK(t && t != b);
	raster_reset();
}

/* Un dibujo de 1 ciclo de (x0, y0)–(x1, y1) sobre la color image `fb`
 * (320 de ancho) con tijeras sc (píxeles enteros). */
static void task_scissor(uint32_t fb, float x0, float y0, float x1, float y1, const uint16_t sc[4], uint32_t r,
			 uint32_t g, uint32_t b)
{
	set_vtx(&vtx[0], x0, y0);
	set_vtx(&vtx[1], x1, y0);
	set_vtx(&vtx[2], x0, y1);
	set_vtx(&vtx[3], x1, y0);
	set_vtx(&vtx[4], x1, y1);
	set_vtx(&vtx[5], x0, y1);
	memset(cmd, 0, sizeof cmd);
	struct gfx_cmd *t = (struct gfx_cmd *)cmd;
	t->op = GFX_OP_TARGET;
	t->size = sizeof *t;
	t->a = fb;
	t->b = W;
	t->c = 2;
	struct gfx_draw *d = (struct gfx_draw *)(cmd + sizeof *t);
	d->h.op = GFX_OP_DRAW;
	d->h.size = sizeof *d;
	d->h.a = 0;
	d->h.b = 6;
	for (int k = 0; k < 4; k++)
		d->scissor[k] = (uint16_t)(sc[k] * 4);
	d->k[GFX_K_ONE][0][0] = r / 255.0f;
	d->k[GFX_K_ONE][0][1] = g / 255.0f;
	d->k[GFX_K_ONE][0][2] = b / 255.0f;
	d->k[GFX_K_ONE][0][3] = 1.0f;
	struct gfx_job job;
	struct gfx_result res;
	memset(&job, 0, sizeof job);
	memset(&res, 0, sizeof res);
	job.rdram_ea = (uint64_t)(uintptr_t)ram;
	job.rdram_size = sizeof ram;
	job.vtx_ea = (uint64_t)(uintptr_t)vtx;
	job.cmd_ea = (uint64_t)(uintptr_t)cmd;
	res.cmd_bytes = sizeof cmd;
	raster_task(&job, &res);
}

/* Datos del juego (medias palabras pares: el bit del alfa en 0). */
static uint16_t game_word(uint32_t x, uint32_t y)
{
	return (uint16_t)(0x8000 | ((x * 7 + y * 13) & 0x3FFF) << 1);
}

/* ¿Quedó intacto todo lo que está fuera de [x0, x1) × [y0, y1) en las
 * primeras `rows` filas de `fb`? */
static int outside_intact(uint32_t fb, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t rows)
{
	for (uint32_t y = 0; y < rows; y++)
		for (uint32_t x = 0; x < W; x++)
			if ((x < x0 || x >= x1 || y < y0 || y >= y1) && px(fb, x, y) != game_word(x, y))
				return 0;
	return 1;
}

/* Super Smash Bros., en el KO: con su z-buffer como color image (320 de
 * ancho, tijeras 10..310 × 10..230) dibuja un rectángulo de toda la
 * pantalla y una malla encima. El juego guarda datos en los márgenes del
 * z-buffer, fuera de las tijeras: la copia a la RDRAM de filas enteras les
 * ponía a 1 el bit del alfa y el juego se colgaba. Solo se copia lo que el
 * RDP pudo escribir. Lo mismo para el espejo de un framebuffer. */
static void raster_copy_inside_scissor(void)
{
	static const uint16_t sc[4] = { 10, 10, 310, 230 };
	int fbfx = raster_fbfx;
	raster_reset();
	raster_fbfx = 2;
	memset(ram, 0, sizeof ram);
	for (int k = 0; k < 40; k++)
		task_fill_w(0x1000, 64, 0, 0, 0);
	task_fill(FB_A, 255, 0, 0);
	field(FB_A);
	field(FB_A);
	for (uint32_t y = 0; y < H; y++)
		for (uint32_t x = 0; x < W; x++) {
			salsa64_wr16(ram + FB_C + (y * W + x) * 2, game_word(x, y));
			salsa64_wr16(ram + FB_A + (y * W + x) * 2, game_word(x, y));
		}
	/* FB_C no es un framebuffer del VI: copia esperada a la RDRAM. */
	task_scissor(FB_C, 0, 0, (float)W, (float)H, sc, 0, 0, 0);
	CHECK_EQ(px(FB_C, 10, 10), rgba5551(0, 0, 0));
	CHECK_EQ(px(FB_C, 309, 229), rgba5551(0, 0, 0));
	CHECK(outside_intact(FB_C, 10, 10, 310, 230, H));
	/* Una malla chica: solo su caja (y lo de antes sigue en la RDRAM). */
	task_scissor(FB_C, 100.0f, 50.0f, 120.5f, 60.0f, sc, 255, 255, 255);
	CHECK_EQ(px(FB_C, 110, 55), rgba5551(255, 255, 255));
	CHECK_EQ(px(FB_C, 20, 20), rgba5551(0, 0, 0));
	CHECK(outside_intact(FB_C, 10, 10, 310, 230, H));
	/* El framebuffer (ya mostrado: espejo al terminar la tarea). */
	task_scissor(FB_A, 0, 0, (float)W, (float)H, sc, 0, 255, 0);
	CHECK_EQ(px(FB_A, 10, 10), rgba5551(0, 255, 0));
	CHECK(outside_intact(FB_A, 10, 10, 310, 230, H));
	raster_reset();
	raster_fbfx = fbfx;
}

/* Star Fox 64 al arrancar (el ruido de colores del logo de «Nintendo
 * presents»): el FILLRECT de 320 × 720 que limpia los tres framebuffers
 * (y su borde de 8 px, que el juego no vuelve a tocar) llega en la primera
 * tarea, cuando aún no existe ninguno; los destinos nacen después, uno por
 * tarea, con un rectángulo de 8..312 × 8..232, y se ceban con la RDRAM,
 * donde quedan los datos que el juego cargó antes por DMA en el primero. El
 * borde tiene que salir con el color del relleno; si un DMA tocó el destino
 * después del relleno, manda lo que el juego dejó. */
static void raster_tall_fill_first(void)
{
	static const uint16_t inner[4] = { 0, 0, (uint16_t)W, (uint16_t)H };
	int fbfx = raster_fbfx;
	raster_fbfx = 2;
	raster_reset();
	memset(ram, 0, sizeof ram);
	/* Los datos del DMA de arranque: filas 0–31 del primer framebuffer. */
	for (uint32_t y = 0; y < 32; y++)
		for (uint32_t x = 0; x < W; x++)
			salsa64_wr16(ram + FB_A + (y * W + x) * 2, game_word(x, y));
	raster_rdram_written(FB_A, 32 * LINE, 0);
	task_tall_fill(FB_A, W, 3 * H, 0, 0, (float)W, 3.0f * H, 40, 50, 60);
	task_scissor(FB_B, 8, 8, 312, 232, inner, 255, 0, 0);
	task_scissor(FB_C, 8, 8, 312, 232, inner, 0, 255, 0);
	task_scissor(FB_A, 8, 8, 312, 232, inner, 0, 0, 255);
	uint32_t w = 0;
	const uint32_t *a = raster_image(FB_A, &w), *b = raster_image(FB_B, &w), *c = raster_image(FB_C, &w);
	CHECK(a && b && c);
	if (a && b && c) {
		CHECK_EQ(a[0], 0x28323cu);
		CHECK_EQ(a[5 * W + 3], 0x28323cu);
		CHECK_EQ(a[20 * W + 315], 0x28323cu);
		CHECK_EQ(a[100 * W + 100], 0x0000ffu);
		CHECK_EQ(b[0], 0x28323cu);
		CHECK_EQ(b[(H - 1) * W + 4], 0x28323cu);
		CHECK_EQ(b[100 * W + 100], 0xff0000u);
		CHECK_EQ(c[3 * W + 3], 0x28323cu);
		CHECK_EQ(c[100 * W + 100], 0x00ff00u);
	}
	/* Un destino que nace mucho después no recibe el relleno viejo. */
	raster_reset();
	memset(ram, 0, sizeof ram);
	task_tall_fill(FB_A, W, 3 * H, 0, 0, (float)W, 3.0f * H, 40, 50, 60);
	for (int k = 0; k < 40; k++)
		task_fill_w(0x1000, 64, 0, 0, 0);
	task_scissor(FB_B, 8, 8, 312, 232, inner, 255, 0, 0);
	b = raster_image(FB_B, &w);
	CHECK(b && (b[0] & 0xFFFFFFu) == 0u);  /* el byte alto es el alfa (sin VI, con alfa) */
	/* Con un DMA posterior al relleno en la memoria del destino, manda el DMA. */
	raster_reset();
	memset(ram, 0, sizeof ram);
	for (uint32_t y = 0; y < 4; y++)
		for (uint32_t x = 0; x < W; x++)
			salsa64_wr16(ram + FB_B + (y * W + x) * 2, rgba5551(200, 100, 50));
	task_tall_fill(FB_A, W, 3 * H, 0, 0, (float)W, 3.0f * H, 40, 50, 60);
	raster_rdram_written(FB_B, 4 * LINE, 1);
	task_scissor(FB_B, 8, 8, 312, 232, inner, 255, 0, 0);
	task_scissor(FB_C, 8, 8, 312, 232, inner, 0, 255, 0);
	b = raster_image(FB_B, &w);
	c = raster_image(FB_C, &w);
	CHECK(b && c);
	if (b && c) {
		CHECK_EQ(b[3 * W + 3], 0xce6331u);  /* (200, 100, 50) de la RDRAM, a 8 bits */
		CHECK_EQ(c[3 * W + 3], 0x28323cu);  /* sin DMA: el relleno */
	}
	raster_reset();
	raster_fbfx = fbfx;
}


/* Pokémon Stadium al elegir VS: una sola tarea con nueve color images chicas
 * (de 172 de ancho; la tabla de destinos tiene ocho ranuras), pegadas una a
 * otra en la RDRAM. La novena recicla la ranura de la más vieja, que dibujó
 * más filas: la caja de lo dibujado era la de la anterior y la copia a la
 * RDRAM pisaba las filas de más de la novena, que son datos del juego (los
 * nodos del planificador de eventos; el juego saltaba a un callback pisado y
 * se quedaba en su pantalla de error). La ranura reciclada copia antes lo
 * dibujado de la imagen vieja a su dirección y empieza sin su caja. */
static void recycled_slot(const uint32_t SW, const uint32_t FULL, const uint32_t LAST)
{
	enum { N = 9 };
	const uint32_t GAP = SW * 2 * FULL;
	static struct gfx_vertex v9[N * 6] __attribute__((aligned(16)));
	static uint8_t c9[N * (sizeof(struct gfx_cmd) + sizeof(struct gfx_draw)) + sizeof(struct gfx_cmd)]
		__attribute__((aligned(16)));
	static uint8_t big[0x100000] __attribute__((aligned(16)));
	const uint32_t base = 0x20000;
	int fbfx = raster_fbfx;
	raster_reset();
	raster_fbfx = 2;
	memset(big, 0, sizeof big);
	memset(c9, 0, sizeof c9);
	uint32_t off = 0;
	for (unsigned k = 0; k < N; k++) {
		uint32_t rows = k == N - 1 ? LAST : FULL;
		struct gfx_cmd *t = (struct gfx_cmd *)(c9 + off);
		t->op = GFX_OP_TARGET;
		t->size = sizeof *t;
		t->a = base + k * GAP;
		t->b = SW;
		t->c = 2;
		off += sizeof *t;
		struct gfx_draw *d = (struct gfx_draw *)(c9 + off);
		d->h.op = GFX_OP_DRAW;
		d->h.size = sizeof *d;
		d->h.a = k * 6;
		d->h.b = 6;
		d->scissor[2] = SW * 4;
		d->scissor[3] = (uint16_t)(rows * 4);
		d->k[GFX_K_ONE][0][0] = 1.0f;
		d->k[GFX_K_ONE][0][3] = 1.0f;
		set_vtx(&v9[k * 6 + 0], 0, 0);
		set_vtx(&v9[k * 6 + 1], (float)SW, 0);
		set_vtx(&v9[k * 6 + 2], 0, (float)rows);
		set_vtx(&v9[k * 6 + 3], (float)SW, 0);
		set_vtx(&v9[k * 6 + 4], (float)SW, (float)rows);
		set_vtx(&v9[k * 6 + 5], 0, (float)rows);
		off += sizeof *d;
	}
	/* Los datos del juego tras las primeras filas de la novena imagen. */
	uint32_t last = base + (N - 1) * GAP;
	for (uint32_t y = LAST; y < FULL; y++)
		for (uint32_t x = 0; x < SW; x++)
			salsa64_wr16(big + last + (y * SW + x) * 2, game_word(x, y));
	struct gfx_job job;
	struct gfx_result res;
	memset(&job, 0, sizeof job);
	memset(&res, 0, sizeof res);
	job.rdram_ea = (uint64_t)(uintptr_t)big;
	job.rdram_size = sizeof big;
	job.vtx_ea = (uint64_t)(uintptr_t)v9;
	job.cmd_ea = (uint64_t)(uintptr_t)c9;
	res.cmd_bytes = off + sizeof(struct gfx_cmd);
	raster_task(&job, &res);
	/* Lo dibujado de la novena sale en sus 20 filas... */
	CHECK_EQ(salsa64_rd16(big + last + (5 * SW + 10) * 2), rgba5551(255, 0, 0));
	/* La imagen de la ranura que se recicló (la segunda: la primera ya salió
	 * de la tabla por otro lado) llegó a la RDRAM antes de que la ranura
	 * cambiara de dueña, con su dirección y todas sus filas. */
	CHECK_EQ(salsa64_rd16(big + base + GAP + ((FULL / 2) * SW + 10) * 2), rgba5551(255, 0, 0));
	CHECK_EQ(salsa64_rd16(big + base + GAP + ((FULL - 1) * SW + SW - 1) * 2), rgba5551(255, 0, 0));
	/* ...y el resto de su memoria sigue siendo del juego. */
	unsigned bad = 0;
	for (uint32_t y = LAST; y < FULL; y++)
		for (uint32_t x = 0; x < SW; x++)
			bad += salsa64_rd16(big + last + (y * SW + x) * 2) != game_word(x, y);
	CHECK_EQ(bad, 0);
	raster_reset();
	raster_fbfx = fbfx;
}

static void raster_recycled_slot(void)
{
	recycled_slot(172, 60, 20);
}

/* El aviso de «Game Pak Check» (538 × 100 en el juego): una imagen que no
 * cabe en un espejo de 64 KB pero sí en el de una ranura que se recicla
 * (128 KB) también llega a la RDRAM; aquí 300 × 110 = 66 000 bytes. */
static void raster_recycled_slot_big(void)
{
	recycled_slot(300, 110, 20);
}

/* Las capas de 8 bits grandes (big8: los fondos de Yoshi's Story, 336 × 256
 * en CI8 que el juego redibuja por tiras) se ceban desde la RDRAM con el
 * índice entero cuando su destino es nuevo o quedó viejo, como las de 16
 * bits: una tarea que dibuja dos tiras (arriba y abajo) copia a la RDRAM la
 * caja que las abarca, y lo que hay en medio tiene que seguir siendo lo que
 * la RDRAM tenía, no ceros. */
#define L8_W   336u
#define L8_H   256u
#define L8     0x40000u  /* fuera de los framebuffers de 16 bits */
static uint8_t cmd2[sizeof(struct gfx_cmd) + 2 * sizeof(struct gfx_draw) + sizeof(struct gfx_cmd)]
	__attribute__((aligned(16)));
static struct gfx_vertex vtx2[12] __attribute__((aligned(16)));

static void quad(struct gfx_vertex *v, float x0, float y0, float x1, float y1)
{
	set_vtx(&v[0], x0, y0);
	set_vtx(&v[1], x1, y0);
	set_vtx(&v[2], x0, y1);
	set_vtx(&v[3], x1, y0);
	set_vtx(&v[4], x1, y1);
	set_vtx(&v[5], x0, y1);
}

static void task_big8_strips(uint32_t idx)
{
	memset(cmd2, 0, sizeof cmd2);
	struct gfx_cmd *t = (struct gfx_cmd *)cmd2;
	t->op = GFX_OP_TARGET;
	t->size = sizeof *t;
	t->a = L8;
	t->b = L8_W;
	t->c = 2u << 8 | 1u;  /* G_IM_FMT_CI, G_IM_SIZ_8b */
	for (int k = 0; k < 2; k++) {
		struct gfx_draw *d = (struct gfx_draw *)(cmd2 + sizeof *t + k * sizeof *d);
		d->h.op = GFX_OP_DRAW;
		d->h.size = sizeof *d;
		d->h.a = (uint16_t)(6 * k);
		d->h.b = 6;
		d->cycle = 0;
		d->scissor[2] = (uint16_t)(L8_W * 4);
		d->scissor[3] = (uint16_t)(L8_H * 4);
		d->k[GFX_K_ONE][0][0] = idx / 255.0f;
		d->k[GFX_K_ONE][0][1] = idx / 255.0f;
		d->k[GFX_K_ONE][0][2] = idx / 255.0f;
		d->k[GFX_K_ONE][0][3] = 1.0f;
	}
	quad(&vtx2[0], 0, 0, L8_W, 8);
	quad(&vtx2[6], 0, 232, L8_W, 240);
	struct gfx_job job;
	struct gfx_result res;
	memset(&job, 0, sizeof job);
	memset(&res, 0, sizeof res);
	job.rdram_ea = (uint64_t)(uintptr_t)ram;
	job.rdram_size = sizeof ram;
	job.vtx_ea = (uint64_t)(uintptr_t)vtx2;
	job.cmd_ea = (uint64_t)(uintptr_t)cmd2;
	res.cmd_bytes = sizeof cmd2;
	raster_task(&job, &res);
}

static void raster_big8_primed(void)
{
	int fbfx = raster_fbfx;
	raster_reset();
	raster_fbfx = 2;
	/* La capa, con un índice distinto por posición, y un campo del VI (la
	 * copia de una capa big8 pide saber el ancho del VI). */
	for (uint32_t y = 0; y < L8_H; y++)
		for (uint32_t x = 0; x < L8_W; x++)
			ram[L8 + y * L8_W + x] = (uint8_t)(x * 7 + y * 3);
	task_fill(FB_A, 0, 0, 255);
	field(FB_A);
	task_big8_strips(200);
	/* Las tiras llegan con su índice... */
	unsigned bad = 0;
	for (uint32_t x = 0; x < 320; x++) {
		bad += ram[L8 + 4 * L8_W + x] < 199 || ram[L8 + 4 * L8_W + x] > 201;
		bad += ram[L8 + 236 * L8_W + x] < 199 || ram[L8 + 236 * L8_W + x] > 201;
	}
	CHECK_EQ(bad, 0);
	/* ...y lo de en medio, que la caja de la copia abarca, sigue siendo lo
	 * que tenía la RDRAM (el cebado), no ceros. */
	for (uint32_t y = 8; y < 232; y++)
		for (uint32_t x = 0; x < 320; x++)
			bad += ram[L8 + y * L8_W + x] != (uint8_t)(x * 7 + y * 3);
	CHECK_EQ(bad, 0);
	/* Otra tarea con otro índice, muchas tareas después (destino viejo: se
	 * vuelve a cebar): el CPU cambió la capa entre medias y eso se respeta. */
	for (uint32_t y = 100; y < 110; y++)
		for (uint32_t x = 0; x < L8_W; x++)
			ram[L8 + y * L8_W + x] = 0x5A;
	for (int k = 0; k < 6; k++)
		task_fill(FB_A, 0, 0, 255);
	task_big8_strips(100);
	for (uint32_t x = 0; x < 320; x++)
		bad += ram[L8 + 4 * L8_W + x] < 99 || ram[L8 + 4 * L8_W + x] > 101;
	for (uint32_t y = 100; y < 110; y++)
		for (uint32_t x = 0; x < 320; x++)
			bad += ram[L8 + y * L8_W + x] != 0x5A;
	CHECK_EQ(bad, 0);
	raster_reset();
	raster_fbfx = fbfx;
}

/* LEGO Racers (#34): cada auto lejano se dibuja en una imagen de 32 × 32
 * borrada con alfa 0 (relleno 0x0000) y se usa como textura con prueba de
 * alfa. La copia a la RDRAM lleva el alfa del destino al bit de RGBA5551,
 * como el RDP (en la consola, el fondo de esas imágenes queda en 0x0000);
 * con alfa 1 en todo, una textura decodificada de la RDRAM (destino viejo)
 * sacaba el auto dentro de un cuadro. */
#define AUX 0x8000u  /* 32 × 32 a 16 bits, antes de los framebuffers */
static void task_aux_car(bool with_fill)
{
	memset(cmd2, 0, sizeof cmd2);
	struct gfx_cmd *t = (struct gfx_cmd *)cmd2;
	t->op = GFX_OP_TARGET;
	t->size = sizeof *t;
	t->a = AUX;
	t->b = 32;
	t->c = 2;
	for (int k = 0; k < 2; k++) {
		struct gfx_draw *d = (struct gfx_draw *)(cmd2 + sizeof *t + k * sizeof *d);
		d->h.op = GFX_OP_DRAW;
		d->h.size = sizeof *d;
		d->h.a = (uint32_t)(6 * k);
		d->h.b = 6;
		d->scissor[2] = 32 * 4;
		d->scissor[3] = 32 * 4;
		d->k[GFX_K_ONE][0][3] = 1.0f;
	}
	struct gfx_draw *fill = (struct gfx_draw *)(cmd2 + sizeof *t);
	fill->cycle = 3;  /* G_CYC_FILL, color 0x0000: alfa 0 */
	fill->aref = 0;
	quad(&vtx2[0], 0, 0, 32, 32);
	if (!with_fill)
		fill->h.b = 0;  /* sin triángulos: solo el auto */
	struct gfx_draw *car = fill + 1;
	car->k[GFX_K_ONE][0][0] = 1.0f;  /* el auto: rojo, opaco */
	quad(&vtx2[6], 8, 8, 24, 24);
	struct gfx_job job;
	struct gfx_result res;
	memset(&job, 0, sizeof job);
	memset(&res, 0, sizeof res);
	job.rdram_ea = (uint64_t)(uintptr_t)ram;
	job.rdram_size = sizeof ram;
	job.vtx_ea = (uint64_t)(uintptr_t)vtx2;
	job.cmd_ea = (uint64_t)(uintptr_t)cmd2;
	res.cmd_bytes = sizeof cmd2;
	raster_task(&job, &res);
}

static uint16_t aux_px(uint32_t x, uint32_t y)
{
	return salsa64_rd16(ram + AUX + (y * 32 + x) * 2);
}

static void raster_alpha_copy(void)
{
	int fbfx = raster_fbfx;
	bool ta = raster_target_alpha, ca = raster_copy_alpha;
	raster_fbfx = 2;
	for (int mode = 0; mode < 3; mode++) {
		raster_reset();
		memset(ram, 0x55, sizeof ram);
		raster_target_alpha = mode != 2;
		raster_copy_alpha = mode == 0;
		task_fill(FB_A, 0, 0, 255);
		field(FB_A);
		task_aux_car(true);  /* el espejo de la imagen chica, al terminar la tarea */
		/* Lo dibujado: rojo con alfa 1. */
		CHECK_EQ(aux_px(16, 16), rgba5551(255, 0, 0));
		/* El fondo: 0x0000 como en la consola; sin el alfa (copia-sin-alfa o
		 * sin-alfa-destinos), 0x0001. */
		CHECK_EQ(aux_px(0, 0), mode == 0 ? 0x0000 : 0x0001);
		CHECK_EQ(aux_px(31, 31), mode == 0 ? 0x0000 : 0x0001);
		/* La imagen del destino: el alfa en el byte alto (alfa 0: 0xFF). */
		uint32_t w = 0;
		const uint32_t *img = raster_image(AUX, &w);
		CHECK(img != NULL && w == 32);
		if (img) {
			CHECK_EQ(img[0] >> 24, mode != 2 ? 0xFFu : 0u);
			CHECK_EQ(img[16 * RASTER_W + 16] >> 24, 0u);
		}
	}
	/* El cebado desde la RDRAM conserva el bit: tareas después (destino
	 * viejo) la imagen se ceba de la copia, sin el relleno, y el fondo sigue
	 * con alfa 0. */
	raster_target_alpha = raster_copy_alpha = true;
	raster_reset();
	memset(ram, 0x55, sizeof ram);
	task_fill(FB_A, 0, 0, 255);
	field(FB_A);
	task_aux_car(true);
	for (int k = 0; k < 6; k++)
		task_fill(FB_A, 0, 0, 255);
	task_aux_car(false);
	uint32_t w = 0;
	const uint32_t *img = raster_image(AUX, &w);
	CHECK(img && img[0] >> 24 == 0xFFu && img[16 * RASTER_W + 16] >> 24 == 0u);
	CHECK_EQ(aux_px(0, 0), 0x0000);
	raster_reset();
	raster_target_alpha = ta;
	raster_copy_alpha = ca;
	raster_fbfx = fbfx;
}

/* layer8 (src/rdp/layer8.c, lo que el PPE hace en la PS3 con las capas de
 * 8 bits) contra el rasterizador del host, byte a byte: tiles CI8 de 16 ×
 * 16 (RGBA8888 con el índice en el rojo) copiados 1:1 sin filtro, un
 * triángulo, un tile con prueba de alfa y uno repetido (wrap), sobre una
 * capa de 336 de ancho con contenido previo. */
#include "rdp/layer8.h"
#define L8T_N 9
static uint8_t cmd8[sizeof(struct gfx_cmd) + L8T_N * sizeof(struct gfx_draw) + sizeof(struct gfx_cmd)]
	__attribute__((aligned(16)));
static struct gfx_vertex vtx8[6 * L8T_N] __attribute__((aligned(16)));
static uint8_t tex8[3][16 * 64] __attribute__((aligned(128)));
static uint8_t ram8[sizeof ram] __attribute__((aligned(16)));

static void quad_st(struct gfx_vertex *v, float x0, float y0, float x1, float y1, float s1, float t1)
{
	quad(v, x0, y0, x1, y1);  /* (x0,y0) (x1,y0) (x0,y1) (x1,y0) (x1,y1) (x0,y1) */
	v[1].s = v[3].s = v[4].s = s1;
	v[2].t = v[4].t = v[5].t = t1;
}

static void l8_draw(struct gfx_draw *d, uint32_t v0, uint32_t n, int tex, unsigned wraps, uint8_t atest, uint8_t aref)
{
	memset(d, 0, sizeof *d);
	d->h.op = GFX_OP_DRAW;
	d->h.size = sizeof *d;
	d->h.a = (uint16_t)v0;
	d->h.b = (uint16_t)n;
	d->scissor[2] = (uint16_t)(672 * 4);
	d->scissor[3] = (uint16_t)(256 * 4);
	d->atest = atest;
	d->aref = aref;
	if (tex < 0) {
		d->k[GFX_K_ONE][0][0] = 77 / 255.0f;
		d->k[GFX_K_ONE][0][3] = 1.0f;
		return;
	}
	d->ntex = 1;
	d->k[GFX_K_T0][0][0] = d->k[GFX_K_T0][0][1] = d->k[GFX_K_T0][0][2] = 1.0f;
	d->k[GFX_K_T0A][0][3] = 1.0f;
	d->tex[0].offset = (uint32_t)tex * sizeof tex8[0];
	d->tex[0].width = d->tex[0].height = 16;
	d->tex[0].pitch = 64;
	d->tex[0].flags = (uint8_t)wraps;
	d->tex[0].xf[0] = d->tex[0].xf[1] = 1.0f / 16;
}

static void raster_layer8_equals_host(void)
{
	int fbfx = raster_fbfx;
	raster_reset();
	raster_fbfx = 2;
	for (int i = 0; i < 3; i++)
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++) {
				uint8_t *p = &tex8[i][y * 64 + x * 4];
				p[0] = (uint8_t)(i * 50 + x * 7 + y * 13);
				p[1] = p[2] = 0;
				p[3] = (uint8_t)((x + y) & 1 ? 255 : 0);
			}
	for (uint32_t y = 0; y < L8_H; y++)
		for (uint32_t x = 0; x < L8_W; x++)
			ram[L8 + y * L8_W + x] = (uint8_t)(x * 3 + y * 5);
	task_fill(FB_A, 0, 0, 255);
	field(FB_A);
	memset(cmd8, 0, sizeof cmd8);
	struct gfx_cmd *t = (struct gfx_cmd *)cmd8;
	t->op = GFX_OP_TARGET;
	t->size = sizeof *t;
	t->a = L8;
	t->b = L8_W;
	t->c = 2u << 8 | 1u;
	struct gfx_draw *d = (struct gfx_draw *)(cmd8 + sizeof *t);
	/* Tiles 1:1 contiguos, uno con prueba de alfa, uno repetido dos veces
	 * a lo ancho (wrap), un relleno constante y un triángulo. */
	quad_st(&vtx8[0], 0, 0, 16, 16, 16, 16);
	l8_draw(&d[0], 0, 6, 0, 0, 0, 0);
	quad_st(&vtx8[6], 16, 0, 32, 16, 16, 16);
	l8_draw(&d[1], 6, 6, 1, 0, 0, 0);
	quad_st(&vtx8[12], 16, 16, 32, 32, 16, 16);
	l8_draw(&d[2], 12, 6, 2, 0, GFX_ATEST_GEQUAL, 128);
	quad_st(&vtx8[18], 40, 40, 72, 56, 32, 16);
	l8_draw(&d[3], 18, 6, 0, GFX_WRAP_REPEAT | GFX_WRAP_MIRROR << 2, 0, 0);
	quad_st(&vtx8[24], 100, 100, 140, 120, 0, 0);
	l8_draw(&d[4], 24, 6, -1, 0, 0, 0);
	set_vtx(&vtx8[30], 200, 30);
	set_vtx(&vtx8[31], 250, 37);
	set_vtx(&vtx8[32], 210, 90);
	vtx8[31].s = 16;
	vtx8[32].t = 16;
	l8_draw(&d[5], 30, 3, 1, 0, 0, 0);
	/* Fuera de los 320 × 240 del host: no se compara, pero tampoco debe
	 * salirse de la capa (x hasta 336, y hasta 256). */
	quad_st(&vtx8[36], 320, 240, 336, 256, 16, 16);
	l8_draw(&d[6], 36, 6, 2, 0, 0, 0);
	/* Un rectángulo escalado 2:1 (no es el camino rápido) y uno con
	 * origen a medio texel: ambos por el genérico. */
	quad_st(&vtx8[42], 150, 150, 182, 166, 16, 16);
	l8_draw(&d[7], 42, 6, 1, 0, 0, 0);
	quad_st(&vtx8[48], 200, 150, 216, 166, 16, 16);
	for (int i = 0; i < 6; i++)
		vtx8[48 + i].s += 0.5f;
	l8_draw(&d[8], 48, 6, 2, GFX_WRAP_REPEAT, 0, 0);

	memcpy(ram8, ram, sizeof ram);
	struct gfx_job job;
	struct gfx_result res;
	memset(&job, 0, sizeof job);
	memset(&res, 0, sizeof res);
	job.rdram_ea = (uint64_t)(uintptr_t)ram;
	job.rdram_size = sizeof ram;
	job.vtx_ea = (uint64_t)(uintptr_t)vtx8;
	job.cmd_ea = (uint64_t)(uintptr_t)cmd8;
	job.tex_ea = (uint64_t)(uintptr_t)tex8;
	res.cmd_bytes = sizeof cmd8;
	raster_task(&job, &res);

	struct layer8_result lr;
	layer8_run(ram8, sizeof ram8, cmd8, sizeof cmd8, vtx8, (const uint8_t *)tex8, NULL, 128, &lr);
	CHECK_EQ(lr.n, 1);
	CHECK_EQ(lr.addr[0], L8);
	CHECK_EQ(lr.draws, L8T_N);
	CHECK_EQ(lr.refused, 0);
	CHECK(lr.pixels > 16 * 16 * 4);
	/* Los tiles 1:1 y el relleno van por el camino rápido; el triángulo,
	 * el 2:1 y el de medio texel por el genérico. */
	CHECK_EQ(lr.fast_pixels, 16 * 16 * 4 + 32 * 16 + 40 * 20 - (16 * 16 - 16 * 8));
	CHECK(lr.slow_pixels > 32 * 16 + 16 * 16);
	unsigned bad = 0, drawn = 0;
	for (uint32_t y = 0; y < 240; y++)
		for (uint32_t x = 0; x < 320; x++) {
			bad += ram[L8 + y * L8_W + x] != ram8[L8 + y * L8_W + x];
			drawn += ram8[L8 + y * L8_W + x] != (uint8_t)(x * 3 + y * 5);
		}
	CHECK_EQ(bad, 0);
	CHECK(drawn > 16 * 16 * 3);
	/* El tile de la esquina llegó entero a la capa de 336 × 256. */
	CHECK_EQ(ram8[L8 + 255 * L8_W + 335], tex8[2][15 * 64 + 15 * 4]);
	/* Un dibujo con mezcla deja el destino entero para el RSX. */
	d[3].blend = GFX_BLEND_ALPHA;
	memcpy(ram8, ram, sizeof ram);
	layer8_run(ram8, sizeof ram8, cmd8, sizeof cmd8, vtx8, (const uint8_t *)tex8, NULL, 128, &lr);
	CHECK_EQ(lr.n, 0);
	CHECK_EQ(lr.refused, 1);
	CHECK_EQ(lr.draws, 0);
	d[3].blend = GFX_BLEND_OFF;
	/* Sin el camino rápido, lo mismo byte a byte. */
	layer8_fast = false;
	memcpy(ram8, ram, sizeof ram);
	layer8_run(ram8, sizeof ram8, cmd8, sizeof cmd8, vtx8, (const uint8_t *)tex8, NULL, 128, &lr);
	layer8_fast = true;
	CHECK_EQ(lr.fast_pixels, 0);
	bad = 0;
	for (uint32_t y = 0; y < 240; y++)
		for (uint32_t x = 0; x < 320; x++)
			bad += ram[L8 + y * L8_W + x] != ram8[L8 + y * L8_W + x];
	CHECK_EQ(bad, 0);
	raster_reset();
	raster_fbfx = fbfx;
}

/* Medida (guía, no aserción): una capa entera de 336 × 256 en tiles de
 * 16 × 16 por los dos caminos, en ns por píxel del host. */
#include <stdio.h>
#include <time.h>
static uint64_t host_clock(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#define L8B_N (21 * 16)
static uint8_t cmdb[sizeof(struct gfx_cmd) + L8B_N * sizeof(struct gfx_draw) + sizeof(struct gfx_cmd)]
	__attribute__((aligned(16)));
static struct gfx_vertex vtxb[6 * L8B_N] __attribute__((aligned(16)));

static void raster_layer8_cost(void)
{
	memset(cmdb, 0, sizeof cmdb);
	struct gfx_cmd *t = (struct gfx_cmd *)cmdb;
	t->op = GFX_OP_TARGET;
	t->size = sizeof *t;
	t->a = L8;
	t->b = L8_W;
	t->c = 2u << 8 | 1u;
	struct gfx_draw *d = (struct gfx_draw *)(cmdb + sizeof *t);
	for (int i = 0; i < L8B_N; i++) {
		float x = (float)(i % 21 * 16), y = (float)(i / 21 * 16);
		quad_st(&vtxb[6 * i], x, y, x + 16, y + 16, 16, 16);
		l8_draw(&d[i], (uint32_t)(6 * i), 6, i % 3, 0, i & 1 ? GFX_ATEST_GEQUAL : 0, 128);
	}
	layer8_clock = host_clock;
	struct layer8_result lr;
	for (int pass = 0; pass < 2; pass++) {
		layer8_fast = pass == 0;
		layer8_run(ram8, sizeof ram8, cmdb, sizeof cmdb, vtxb, (const uint8_t *)tex8, NULL, 128, &lr);
		fprintf(stderr, "    layer8 capa 336x256 %s: %u px en %.3f ms (%.1f ns/px)\n",
			pass == 0 ? "rectángulos 1:1" : "genérico", pass == 0 ? lr.fast_pixels : lr.slow_pixels,
			(pass == 0 ? lr.fast_ticks : lr.slow_ticks) / 1e6,
			(double)(pass == 0 ? lr.fast_ticks : lr.slow_ticks) / (pass == 0 ? lr.fast_pixels : lr.slow_pixels));
	}
	layer8_fast = true;
	layer8_clock = NULL;
	CHECK_EQ(lr.n, 1);
}

void t_suite_raster(void)
{
	RUN(raster_layer8_equals_host);
	RUN(raster_layer8_cost);
	RUN(raster_big8_primed);
	RUN(raster_alpha_copy);
	RUN(raster_recycled_slot);
	RUN(raster_recycled_slot_big);
	RUN(raster_copy_inside_scissor);
	RUN(raster_tall_fill);
	RUN(raster_tall_fill_first);
	RUN(raster_mirror_after_dma);
	RUN(raster_frame_mirror_sm64);
}
