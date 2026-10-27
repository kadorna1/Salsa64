# Fuente del sistema de la PS3 (libfont)

Desde la rama fuente-sistema (2026-10-26) todo el texto del menú, la pausa y
los avisos sale con la fuente del XMB, leída de la consola en tiempo de
ejecución; la 8x13 de rsxkit queda de respaldo. Código: `lib/rsxkit/src/sysfont.c`
(la fuente), `lib/rsxkit/src/ui_geom.c` (caché de glifos y texto proporcional) y
`src/frontend/main.c` (`font_init_once`, `font_select`).

**Estado:** compila y pasa las pruebas del host; **falta probarlo en la PS3
real.** RPCS3 implementa libfont a medias (con stb_truetype, varias funciones
vacías y la `y` de la pluma tomada como tope del renglón): no sirve para
concluir nada, ni siquiera si trae las fuentes de `dev_flash`.

## La API (PSL1GHT f649a08)

Cabeceras `font/font.h`, `font/fontFT.h`, `font/fontset.h`; bibliotecas
`-lfontFT -lfont` (stubs de `libfont` y `libfontFT`). Módulos del sistema
(`sysmodule/sysmodule.h`), cargados en este orden y descargados al revés:

| Módulo | Id |
|---|---|
| `SYSMODULE_FONT` | 0x19 |
| `SYSMODULE_FREETYPE` | 0x1b |
| `SYSMODULE_FONTFT` | 0x1a |

Secuencia que usa rsxkit:

1. `sysModuleLoad` de los tres. Si alguno falla, no se llama nada de libfont
   (sin módulo, el stub salta a basura: es la misma trampa que `sysFs*` sin
   módulo, que cuelga la consola).
2. `fontInit(&cfg)` con `fontConfig_initialize`, caché de archivos de 1 MB
   (`fileCache.buffer`, alineada a 128) y 8 `userFontEntries`. `fontInit` es
   un `static inline` que pasa las banderas de revisión del stub (0x14) a
   `fontInitializeWithRevision`.
3. `fontInitLibraryFreeType(&ft, &lib)` con `fontLibraryConfigFT_initialize`
   y las cuatro funciones de memoria (`malloc`, `free`, `realloc`, `calloc`
   con un primer argumento `object`).
4. `fontCreateRenderer(lib, &rcfg, &renderer)`: búfer propio de libfont
   (`buffer = NULL`), 256 KB al empezar, hasta 1 MB, crece de a 64 KB.
5. `fontOpenFontset(lib, &type, &font)` con `type = { CELL_FONT_TYPE_*,
   FONT_MAP_UNICODE }`.
6. `fontSetResolutionDpi(72, 72)`, `fontSetScalePixel(22, 22)`,
   `fontBindRenderer`, `fontSetupRenderScalePixel(22, 22)` y
   `fontGetHorizontalLayout` (renglón y línea base, solo para el log).
7. Por glifo: `fontGetFontIdCode` (¿lo tiene?) y
   `fontRenderCharGlyphImage(&font, cp, &surface, x, y, &metrics, &trans)`
   sobre una superficie de 64 × 64 de 1 byte por píxel
   (`fontRenderSurfaceInit(.., pixelSizeByte = 1, ..)` y
   `fontRenderSurfaceSetScissor`). Cobertura de 8 bits; el avance sale en
   `metrics.horizontal.advance` (píxeles).
8. Cierre: `fontUnbindRenderer`, `fontCloseFont`, `fontDestroyRenderer`,
   `fontEndLibrary(lib)`, `fontEnd()`, soltar la caché y descargar los
   módulos.

### Juegos de fuentes (CELL_FONT_TYPE_*)

PSL1GHT solo define los de la SDK 1.60; el resto son los mismos valores
(documentados en el código de RPCS3, usado solo como referencia). Archivos en
`/dev_flash/data/font/SCE-PS3-*.TTF` (RD = Rodin, NR = NewRodin, YG =
coreano, DH = DFHei5; `-R-`, `-L-`, `-B-` = normal, fina, negrita).

| Escritura | Normal (en orden de preferencia) | Negrita |
|---|---|---|
| Latín | 0x204 (NewRodin + Rodin2), 0x104, 0x101 | 0x70204, 0x70104 |
| Japonés y coreano | 0x209 (NewRodin + YG + DFHei5 + Rodin2), 0x109, 0x108, 0x100 | 0x70201, 0x70101 (sin chino) |
| Chino tradicional | 0x20b (DFHei5 TCH primero), 0x10b, 0x10a | — |
| Chino simplificado | 0x20d (DFHei5 SCH primero), 0x10d, 0x10c | — |

Los «RODIN2» (0x2xx) traen Latin2; el cirílico, el polaco y el turco deberían
salir de ahí (por confirmar en la PS3). Lo que no esté, por carácter, sale con
la 8x13. Salsa64 usa la normal; la negrita queda en la API
(`rsxkit_sysfont_open(script, 1)`).

## Trampas

- **Funciones de memoria por descriptor de 32 bits.** Los campos de
  `fontMemoryInterface` son punteros de 32 bits (`ATTRIBUTE_PRXPTR`). Hay que
  pasar `__get_opd32(f)` (el descriptor de 32 bits que el enlazador deja en
  el descriptor de 64, como hace PSL1GHT con los callbacks de sysutil); con
  el puntero de función de 64 bits la PRX salta a cualquier lado.
- **`fontSetScalePixel` está mal declarada** en `font.h` («fontontSetScalePixel»);
  el símbolo exportado es `fontSetScalePixel`: rsxkit lo declara él mismo.
- **¿La `y` de la pluma es la línea base o el tope del renglón?** No se
  supone: al abrir se dibuja la «H» y su última fila con tinta da la línea
  base en la superficie; vale para todos los glifos. El log lo dice («H» en
  las filas a–b). Si la «H» no deja tinta, la fuente no se usa.
- **¿Cómo dice libfont que no tiene un glifo?** Se usan dos señales: el
  error de `fontRenderCharGlyphImage` y `fontGetFontIdCode` (falla o da el
  glifo 0, `.notdef`). Si `fontGetFontIdCode` dice que no tiene ni la «A», esa
  lectura no se usa y queda solo la primera (el log dice cuál).
- Todas las estructuras que se pasan (`font`, `fontRenderer`, superficie)
  tienen que estar en memoria por debajo de 4 GB (siempre lo están en PSL1GHT).
- No es seguro entre hilos: se usa solo desde el hilo que arma el 2D.

## Cómo se usa en Salsa64

- `font_init_once` (al aplicar el idioma, con el 2D ya listo y antes de
  cualquier ROM): si existe `/dev_hdd0/salsa64/fuente-propia`, la 8x13. Si
  no, abre el juego japonés (cubre latín, kana, hangul y hanzi) y comprueba
  あ, 한, 體 y 简: con los cuatro, ofrece japonés, coreano y chino.
- `font_select`: con chino tradicional o simplificado abre su juego (los
  ideogramas cambian de forma); con japonés o coreano, el japonés; con los
  demás, el latino. Si un juego no abre, vuelve al japonés; si nada abre, la
  8x13 y los CJK caen en inglés.
- Con el modo `embedded` o una ROM directa (sin menú) sigue la 8x13: abrir
  libfont ahí reservaría memoria permanente con la ROM ya en el heap.
- Log: «fuente: la del sistema (juego 0x…, 22 px, …, libfont N KB ahora y M
  KB de máximo …)» o «fuente: la propia 8x13»; y el estado de la caché «al
  abrir el juego» y «al salir» (casillas, aciertos, rasterizados, reemplazos,
  diferidos, con la 8x13).

## Caché de glifos (rsxkit, `ui_geom.c`)

- El atlas del 2D pasó de 288 × 616 a 512 × 952: la 8x13 sigue arriba a la
  izquierda y el resto son 392 casillas de 28 × 28 (glifo de hasta 26 × 26 y
  borde transparente para el filtro lineal).
- Tabla de 2048 glifos conocidos (punto de código → avance, imagen, casilla):
  medir rasteriza una vez; si se llena (1536), se vacía entera.
- Reemplazo LRU por cuadro, nunca de una casilla usada en este cuadro o el
  anterior (el RSX puede estar leyéndola: `rsxkit_ui_begin` solo espera la
  barrera de hace dos cuadros). Sin espera al RSX; lo que no cabe, o lo que
  pasa de 32 re-rasterizados por cuadro, se dibuja en el siguiente.
- Los glifos se escriben en el atlas mientras se arma el lote;
  `rsxkit_ui_end` pone una barrera y `rsxkit_vertices` invalida la caché de
  texturas del RSX en cada lote.

## Qué mirar en la PS3

1. Que el log diga «fuente: la del sistema» y la «H» caiga en unas 4 filas
   (si la `y` es la línea base: filas ~8–23) o ~24 más abajo (si es el tope).
2. Que el texto quede centrado en su renglón como antes (si sale corrido
   arriba o abajo, ajustar `RSXKIT_SYSFONT_BASELINE`).
3. Cirílico, polaco y turco con la fuente del sistema o con la 8x13 (cuántos
   «con la 8x13» en el log).
4. Que los «diferidos» del log sean 0 o casi en uso normal, y la memoria de
   libfont (máximo) para `docs/memory-budget.md`.
