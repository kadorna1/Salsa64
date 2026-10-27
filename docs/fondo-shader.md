# Fondo «Shader» del menú

Un tercer valor de la fila «Fondo» de Opciones (`fondo = shader` en
`menu.cfg`; los otros son `si`, las ondas de siempre, y `no`, el fondo
fijo): el shader **«XdGfRR» de Shadertoy, de int_45h**
(https://www.shadertoy.com/view/XdGfRR), una ola translúcida hecha con
raymarching, polvo de estrellas cerca de la ola y un degradado que va del
azul (mediodía) al gris (medianoche) con la hora de la consola. El autor
permite hacer cualquier cosa con él y pide un comentario de crédito: va en
la cabecera de cada archivo, en la ayuda de la fila y en
docs/THIRD_PARTY.md.

Archivos: `src/frontend/fondo.c` (el PPE: memoria, pasadas, medición),
`fondo_calc.c`/`.h` (lo de la CPU, igual en el PPE y en la Mac),
`fondo_ref.h` (hash, estrellas, tramado y degradado del original),
`src/frontend/fondo/*.fcg` (los fragment programs) y `tools/fondo-ref.c`
(el original y la versión del RSX en C, para comparar).

Si algo no carga (memoria, superficies, programas) o una pasada falla, el
menú vuelve solo a las ondas (`fondo: el shader no cargó, vuelven las
olas` en el log) y no lo reintenta hasta cambiar de fondo o salir del menú.
Todo se suelta al abrir un juego.

## Por qué no entra tal cual

El original, a 1080p: 2 millones de píxeles × 4 muestras de AA × hasta 100
pasos de raymarching, cada paso con 3 senos y un ruido de valor (4 hash con
enteros de 32 bits, que el RSX no tiene), más 3 capas de estrellas con 8
hash cada una. Del orden de 10¹¹ operaciones por cuadro; el RSX da ~13 ×
10⁹ instrucciones por segundo en los fragment programs (lo medido con los
filtros CRT, docs/filtros.md), unas 100 × 10⁶ en 8 ms.

## Cómo se reparte

**La ola es analítica, no un raymarching.** El SDF del original es
|g| con g = A·y + h(x, z) (un plano desplazado: un campo de alturas), y lo
que acumula el raymarching es 0,015 por cada paso de 0,05 dentro de la
cáscara |g| < 0,04, por f(t) = smoothstep(0, 0,3, 0,009·t): la integral
de 0,3·f(t) sobre el tramo del rayo dentro de la cáscara. Con F̃ una
primitiva de 0,3·f, esa integral es la suma de ±F̃(t) en los cruces del
rayo con las superficies g = +0,04 y g = −0,04 (− al entrar, + al salir),
y si se entra o se sale lo dice hacia dónde mira el triángulo cruzado.
Así que las dos superficies van como **mallas** de 65 × 257 vértices que la
CPU calcula en cada cuadro (en u = dx del rayo y z = t, de t = 1 a 40) y
el RSX las rasteriza **sumando en una superficie fp16**: cada píxel recibe
exactamente sus cruces, incluidos los rasantes (las líneas brillantes,
donde el rayo corre a lo largo de la ola), y las parejas lejanas se anulan
solas. Dos tiras cierran la cáscara en t = 40 (+F̃(40)) y en t = 1. El
fragment program de la malla (`fondo_ola.fcg`) son 9 instrucciones. El
tope de 41 pasos dentro (MAX_DRAWS) se aplica al final: a ≤ 0,615.

**Los 100 pasos del original importan.** Cuando la cámara pasa cerca de la
ola (cada ~12 s), los rayos que corren casi pegados a ella gastan los 100
pasos sin llegar a la ola de atrás, y el original no la dibuja (sin esto,
PSNR 29 dB en esos instantes). Una marcha con **los mismos pasos que el
original** (0,05 dentro de la cáscara, 0,8·|g| fuera, desde t = 1, el
jitter medio) a **1/8 de la resolución** da en cada rayo el t donde se le
acaban los pasos, y la malla corta su t ahí (`min(t, t_parada)`): lo que
queda detrás no suma. g(t) = A·dy·t + h(dx·t, t) y h depende solo de la
columna, así que la CPU tabula h en 512 valores de t por columna (fp16,
filtro lineal) y cada paso de la marcha es una lectura y 7 operaciones,
con 4 rayos por fragmento (las 4 columnas de un texel, una por canal). Va
en 4 pasadas de 25 pasos (cgcomp no ensambla un programa de 100).

**El polvo** es 1 − g_min/3, con g_min el mínimo de |g| para t en
(10, 40] antes de que se acaben los pasos (0 si el rayo cruza la ola): en
la misma resolución reducida, g en 32 valores de t de la tabla.

**Las estrellas** usan el hash exacto del original (`uint`), que la CPU
escribe en texturas RGBA8 que se repiten (un anillo por canal: capas 1, 2
y 3), y solo rellena lo que entra al moverse el campo (unos miles de
texeles por cuadro; la primera vez, 1,8 millones). La interpolación cúbica
de Hermite de las 4 esquinas sale de una sola lectura bilineal con la
coordenada en floor(p) + 0,5 + w(fract(p)); en la capa 3 (celdas de 1,7
píxeles a 1080p) basta la bilineal. El brillo de cada capa,
pow(value2d·0,5 + 0,5, 8,3), va horneado a 8 muestras por celda. Las
estrellas solo se calculan en las filas donde puede haber polvo (la CPU
las acota con la tabla de h): el resto de la pantalla usa un programa de
13 instrucciones.

**Todo lo que es lineal en la pantalla llega interpolado** (constantes
afines de `rsxkit_post_pass`, `kx`/`ky`): las coordenadas de las 6
texturas de estrellas y brillo, el degradado (su término x·y es el
producto de dos interpolados) y el producto del tramado. El AA de 2 × 2 del
original se reemplaza por una muestra en el centro de sus cuatro (U −
0,25).

## Pasadas y costo por píxel

| Pasada | Tamaño (1080p) | Instrucciones |
|---|---|---|
| marcha (4 × 25 pasos, 4 rayos por fragmento) | 60 × 135 | 324 + 3 × 330 |
| polvo (4 columnas por fragmento) | 60 × 135 | 353 |
| desempaque (t de parada y polvo por columna) | 240 × 135 | 20 |
| borrar la ola (fp16) | 1920 × 1080 | 1 |
| malla de la ola: 2 superficies × 2 caras + 2 tapas, suma en fp16 | ~0,8–1 fragmento por píxel | 9 |
| final con estrellas (filas con polvo, ~55 %) | 1920 × ~600 | 34 (8 lecturas) |
| final sin estrellas | 1920 × ~480 | 13 |

Estimado con lo medido de los filtros CRT (~13 × 10⁹ instrucciones por
segundo): ~0,9 ms la marcha y el polvo, ~1,5–2 ms la ola (incluida la
mezcla en fp16) y ~4 ms la final: **~6,5–7,5 ms a 1080p**, la mitad a
720p. La CPU: ~1 ms por cuadro en el PPE (malla y tabla de h; la primera
vez, ~0,1 s para llenar los anillos). **Medirlo en la PS3**: el log
escribe cada 5 s

    fondo: X ms del RSX por cuadro (marcha y polvo A, ola B, final C; filas con estrellas N de H), CPU D ms, M cuadros

Si a 1080p pasa de 8 ms, el archivo `fondo-ola-media` en `/dev_hdd0/salsa64`
deja la superficie de la ola a la mitad de la resolución (un cuarto de los
fragmentos y del borrado, leída con filtro bilineal): las líneas salen algo
más blandas (en la Mac, 41,5–41,9 dB en lugar de 42,4–46,1 contra el
original; `FONDO_OLA_DIV=2 build/fondo-ref rsx …`).

## Verificación contra el original

`tools/fondo-compara.sh [ANCHO ALTO]` compila `tools/fondo-ref.c`, evalúa
el shader original en C (flotante de 32 bits, con su AA de 2 × 2 y sus
100 pasos) y la versión del RSX con la misma lógica que los fragment
programs (texturas RGBA8 y fp16 con filtro de 8 bits de fracción, la malla
rasterizada con la regla de arriba a la izquierda y la suma redondeada a
fp16), en iTime 10, 47,3, 123,4 y 600, a medianoche y a mediodía, y deja
los PPM y PNG en `build/fondo-shader/` (`o-*` el original, `r-*` el del
RSX, `d-*` la diferencia × 8). A 1920 × 1080 (2026-10-27):

| iTime | Medianoche | Mediodía |
|---|---|---|
| 10 | 46,1 dB, promedio 0,24/255 | 48,4 dB, 0,15/255 |
| 47,3 (la cámara junto a la ola) | 44,0 dB, 0,21/255 | 46,4 dB, 0,14/255 |
| 123,4 | 45,3 dB, 0,20/255 | 47,7 dB, 0,13/255 |
| 600 | 42,4 dB, 0,26/255 | 44,9 dB, 0,17/255 |

A 1280 × 720: 43,7 / 46,1, 41,1 / 43,5, 43,0 / 45,4 y 40,0 / 42,5 dB. En
los dos casos, 97–98 % de los valores a 1/255 o menos. Lo que queda está en las líneas
brillantes de la ola: el original las muestrea con 4 rayos por píxel, cada
uno con su jitter (ruido), y aquí es la medida exacta de la cáscara en un
rayo; y la t de parada viene de 1/8 de la resolución (a 1/4 subiría a
45,8 dB en iTime 600, a 4 veces el costo de la marcha).

Para comparar con la consola o con RPCS3: el archivo `fondo-volcado` en
`/dev_hdd0/salsa64` hace que el cuadro 40 del fondo deje la superficie de
la ola y la reducida en `fondo-ola.f16` y `fondo-low.f16`, con su iTime en
el log; `build/fondo-ref ola ANCHO ALTO ITIME fondo-ola.f16 fondo-low.f16`
las compara con la emulación.
