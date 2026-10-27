# Fondo «Seda» del menú

Cuarto valor de la fila «Fondo» de Opciones (`fondo = seda` en `menu.cfg`;
el orden es Ondas → Shader → Seda → No): tres cintas de tela que flotan,
se tuercen sobre sí mismas y ondulan, con brillo en el borde donde quedan
de canto, sobre un degradado que sigue la hora de la consola y con
destellos que suben despacio.

**Origen.** Todo es propio: el movimiento sale de sumas de senos con
distintas frecuencias, direcciones y velocidades, más una torsión a lo
largo del eje de la cinta. No se tomó nada del firmware de la PS3 ni de
repositorios que lo analicen, ni constantes, ni tablas, ni lógica de
terceros (por eso no hay entrada nueva en `docs/THIRD_PARTY.md`). Los
colores del día y de la noche son una paleta propia (`seda_colors`).

Archivos: `src/frontend/seda_calc.c`/`.h` (la malla y la paleta; el mismo
código C en el PPE, el SPE y la Mac), `src/spu/seda/main.c` (el SPE),
`src/frontend/seda_spe.h` (el trabajo), `src/frontend/seda.c` (el PPE: memoria,
SPE, pasadas, medición), `src/frontend/seda/*.fcg` (los fragment programs),
`tools/seda-vista.c` y `tools/seda-vista.sh` (la vista previa) y
`tests/unit/test_seda.c`.

Si algo falla (memoria, programas, una pasada, el trabajo del SPE) el menú
vuelve solo a las ondas (`fondo seda: … vuelven las olas` en el log) y no lo
reintenta hasta cambiar de fondo o salir del menú. Todo, el SPE incluido, se
suelta al abrir un juego. Si el SPE no arranca, los vértices se calculan en
el PPE; el archivo `seda-ppe` en `/dev_hdd0/salsa64` lo fuerza (para
comparar tiempos).

## La cinta

Cada capa es una red de 128 × 24 vértices (`SEDA_NU` a lo largo,
`SEDA_NV` a lo ancho), con s de −1 a 1 a lo largo y v de −1 a 1 a lo ancho:

- **Eje** C(s): x lineal (la cinta sale por los dos lados de la pantalla);
  y y z, dos senos cada uno (frecuencias 3,1 y 6,7 en y; 2,5 y 5,6 en z, con
  fases que corren a 70, 95, 55 y 80 ciclos por periodo).
- **Torsión**: el ancho gira alrededor del eje un ángulo
  φ(s) = k·s + 1,2·sin(2,2·s + fase) + giro lento. Con k de 2,6 a 4,4 rad por
  unidad de s la tela da más de una media vuelta y queda de canto en uno o
  dos puntos de la pantalla; el giro lento (un ciclo cada 30 s, a 20 por
  periodo) los desplaza a lo largo de la cinta.
- **Marco**: T es el eje derivado; B = T × arriba, N2 = B × T; el ancho es
  D = cos φ·N2 + sin φ·B y la normal de la tela M = T × D. El semiancho varía
  ±28 % a lo largo.
- **Pliegues**: cuatro ondas h(s, v) = Σ aᵢ·sin(kᵤᵢ·s + k_vᵢ·v ± ωᵢt + φᵢ)
  con amplitudes 0,075, 0,05, 0,034 y 0,02, frecuencias a lo largo de 5, 9,
  14 y 21 y a lo ancho de ±2 a ±7: las crestas son diagonales, corren en
  los dos sentidos a distinta velocidad y se cruzan; en la orilla ondulan
  más (×(0,45 + 0,55·v²)).
- P = C + v·semiancho·D + h·M.
- **Tres capas** con otra profundidad (3,0; 3,55; 4,2), altura, ancho,
  torsión, fase y velocidad (+0, +9 y −6 ciclos por periodo en cada onda).

El tiempo es una fase, la fracción de 600 s: cada onda da un número entero
de ciclos por periodo, así que no hay salto al reiniciar (lo comprueba
`seda_period_wraps_seamlessly`). El seno propio (`sn`) es un polinomio de
grado 9 tras reducir a [−π/2, π/2]: solo `float` y `sqrtf`, igual en el PPE
y en el SPE.

## Iluminación (RSX)

El vértice lleva en TEXCOORD0 `(|n·V|, n·H, intensidad, tono)` con la normal
n de las diferencias de la red (tela de dos caras), V = −P/|P| y H el vector
medio con una luz fija. El fragment program `seda_cinta.fcg` (19
instrucciones, una lectura):

- Fresnel: (1 − |n·V|)³ · 1,7 + 0,16 de base: poco opaca de frente y
  encendida de canto (la transparencia mayor de frente).
- Especular suave: (n·H)¹⁶ · 0,9, que corre por los pliegues porque la
  normal sale de los pliegues; un poco más blanco que el tinte.
- Color: rampa de 16 texeles de la capa (tinte A → tinte B por el tono,
  por la ganancia de la capa: 1,0, 0,7 y 0,55), que la CPU escribe cada
  cuadro con la paleta de la hora. Va por textura porque los programas de
  malla no reciben constantes por dibujo (`k` es de las pasadas de
  pantalla) y parchear un programa cada cuadro obliga a esperar al RSX.
- Mezcla aditiva (`RSXKIT_BLEND_ADD`), sin descartar caras.

El fondo (`seda_fondo.fcg`, 23 instrucciones): degradado vertical de la hora
(`k[0]` arriba, `k[1]` abajo), resplandor detrás de la cinta, viñeta y un
tramado mínimo contra las bandas. La paleta (`seda_colors`) interpola con
suavidad 8 claves: noche índigo (0 h), amanecer rosa (5,5 h), día azul
(8 y 13 h), tarde ámbar (17,5 h), anochecer violeta (20 h), noche.

**Destellos**: 48 cuadritos aditivos (`seda_destello.fcg`, 14 instrucciones)
que suben a 3–7 vueltas de pantalla por periodo, parpadean (100–300
parpadeos por periodo) y brillan más de noche. La PS3 no tiene point
sprites en rsxkit: cada uno es un cuadrito de dos triángulos con un centro
suave y una cruz de brillo.

## Reparto

| Parte | Quién | Qué |
|---|---|---|
| Posiciones (3 × 3072 vértices) | SPE | 4 senos por vértice, 6 por posición de eje |
| Normales, proyección, t0 | SPE | por filas de 768 bytes, doble búfer de DMA |
| Destellos | SPE | 48 × 4 vértices |
| Rampas de color (4 × 16 texeles) | PPE | cada cuadro |
| Degradado, 3 mallas, destellos | RSX | 5 pasadas |

El PPE publica el trabajo del cuadro N en uno de 3 búferes de vértices
(memoria principal mapeada al RSX, 294 KB cada uno), espera el del cuadro
N − 1 y dibuja ese: un cuadro de latencia que no se nota. Antes de reescribir
un búfer espera la barrera de su último dibujo.

Local store del SPE: posiciones de una capa 48 KB, filas 1,5 KB, destellos
6 KB, el resto código y pila (la cabecera de `src/spu/seda/main.c` lo
detalla).

## Costo estimado y qué medir

- SPE: ~9 200 vértices por cuadro; a ~400 ciclos escalares por vértice,
  unos 3,7 M de ciclos: **~1,2–3 ms** (es código escalar: si pasa de 5 ms,
  vectorizar los senos de las 24 columnas de una fila).
- RSX a 1080p: degradado 2 M píxeles × 23 instrucciones y cintas ~1,5
  M fragmentos × 19: del orden de **3–6 ms**; a 720p, la mitad.
- PPE: las rampas y el trabajo del SPE, ~0,05 ms.

El log escribe cada 5 s

    fondo seda: X ms del RSX por cuadro, Y ms del SPE, Z ms del PPE, N cuadros

(el del PPE solo con `seda-ppe`; con el SPE es 0). Para comparar el camino
del PPE con el del SPE: crear `seda-ppe` en `/dev_hdd0/salsa64` y reabrir
el menú.

## Vista previa en la Mac

`tools/seda-vista.sh [ANCHO ALTO]` compila `tools/seda-vista.c`, que genera
la malla con el mismo `seda_calc.c` y la rasteriza por software con la
misma cuenta que los fragment programs (interpolación con corrección de
perspectiva, rampa de 8 bits, mezcla aditiva), a 4 instantes y a 13, 18 y
23 h; deja los PNG en `build/fondo-seda/`.

## Pruebas

`tests/unit/test_seda.c` (`tools/build.sh -C tests/unit -j8 run`): posiciones
contra una evaluación independiente en doble precisión (error < 2e-3), el
camino por filas del SPE igual bit a bit a la capa entera del PPE,
normales y Fresnel contra la red en doble precisión, proyección, que la
tela no se rompa entre vértices vecinos, el cierre del periodo sin saltos,
movimiento, índices, destellos y paleta (continua, 0–1, a medianoche igual
por los dos lados).
