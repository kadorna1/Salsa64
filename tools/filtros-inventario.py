#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inventario estático de CRT a 1440×1080, fuente 320×237 a 3×.
Uso: python3 tools/filtros-inventario.py build/salsa64/obj > build/filtros/inventario.txt
El tráfico cuenta tomas lógicas, sin caché/compresión; no predice tiempos.
"""
import math
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
obj = Path(sys.argv[1])
print('Destino 1440x1080, pantalla 1920x1080; fuente 960x711 -> 320x237.')
print('Tráfico = escritura + TEX por píxel (bilineal: 4 texeles); caché no descontada.')
print('MiB/cuadro no son transferencias medidas ni tiempos de PS3. Pitch alineado a 64.')
print('Filtro | pasada | destino | formato | instrucciones/TEX | MiB destino | MiB lógicos/cuadro')


def row(filt, name, w, h, bpp=8, tex_bpp=8, bilinear=1, times=1, shader=None, reads=None):
    path = obj / (name + '.fpo') if shader is None else root / shader
    result = subprocess.check_output([sys.executable, str(root / 'tools/fpo-info.py'), str(path), '--sin-condiciones'], text=True)
    ins, tex = map(int, re.search(r': (\d+) instrucciones \((\d+) de textura\)', result).groups())
    traffic = (tex * tex_bpp * bilinear if reads is None else reads) + bpp
    allocated = ((w * bpp + 63) // 64 * 64) * h * times / 1048576
    print(f'{filt} | {name}' + (f' x{times}' if times > 1 else '') +
          f' | {w}x{h} | {"RGBA16F" if bpp == 8 else "X8R8G8B8"} | {ins}/{tex} | {allocated:.3f} | {w*h*traffic*times/1048576:.3f}')


for filt in ('mascara', 'mascara-fina', 'rgb', 'svideo', 'compuesto', 'trinitron'):
    row(filt, 'reducir-h', 320, 711, shader='build/rsxkit/obj/rsxkit_post_reduce_h.fpo', tex_bpp=4, bilinear=4)
    row(filt, 'reducir-v', 320, 237, bpp=4, shader='build/rsxkit/obj/rsxkit_post_reduce_v.fpo', bilinear=4)
    row(filt, 'crt_lineal', 320, 237, tex_bpp=4)
    row(filt, 'crt_glow', 640, 474, bilinear=4)
    if filt.startswith('mascara'):
        d = 3 if filt.endswith('fina') else 4
        row(filt, 'crt_mask_tile', d*3, 6, times=3)
        row(filt, 'crt_mask_dots', math.ceil(1440/d)+8, 363)
        # 3 teselas + 3 puntos + una lectura bilineal del resplandor.
        row(filt, 'crt_mask_final', 1440, 1080, bpp=4, reads=6*8+4*8)
    else:
        if filt == 'trinitron':
            row(filt, 'crt_trinitron_lines_a', 1440, 237)
            row(filt, 'crt_trinitron_lines_b', 1440, 237)
        else:
            # ceil(4*170.667) + count + 2, según crt_cable_taps.
            cols = 694 if filt == 'rgb' else 712
            times = 2 if filt == 'compuesto' else 1
            row(filt, 'crt_cable_signal', cols, 237, tex_bpp=4, bilinear=4, times=times)
            row(filt, 'crt_cable_decode9' if filt == 'rgb' else 'crt_cable_decode27', cols, 237, times=times)
            grid = '8' if filt == 'compuesto' else '4'
            row(filt, 'crt_tube_lines_a'+grid, 1440, 237)
            row(filt, 'crt_tube_lines_b'+grid, 1440, 237)
        row(filt, 'crt_tube_final', 1440, 1080, bpp=4, reads=4*8+4*8)
    if (obj / 'crt_copy.fpo').exists():
        row(filt+' (solo repetido válido)', 'crt_copy', 1440, 1080, bpp=4, tex_bpp=4)
print('Caché: vista del búfer presentado; cero bytes adicionales y cero copias en cuadros completos.')
print('Imagen repetida vieja: solo final. Nueva: crt_copy si sigue válida; con UI, final original. Imagen nueva: cadena original.')
print('Sin cambiar: luz lineal/resplandor, decodificación SPE y filtros sencillos de presentación.')
print('\nProgramas auxiliares y sencillos (sin cambios):')
for path in sorted((root / 'build/rsxkit/obj').glob('*.fpo')):
    result = subprocess.check_output([sys.executable, str(root / 'tools/fpo-info.py'), str(path)], text=True)
    print(result.splitlines()[0])
