# SPDX-License-Identifier: GPL-2.0-or-later
"""Arma el atlas de íconos de botones del menú (assets/menu/prompts.png) y el
manifiesto src/frontend/prompts_atlas.h.

Fuente: Kenney «Input Prompts» 1.5A (CC0, https://kenney.nl/assets/input-prompts),
tamaño «Default» (64 × 64), bajado a 48 × 48 con promedio de área. No hay
botones de N64 en el paquete: se usan los de GameCube (A y B recoloreados al
azul y al verde de la N64, el disco de Start en rojo; Z, L y R se tiñen de
gris al dibujar con el `tint` del manifiesto).

Uso, en la Mac (sin PIL; Blender sin ventana aporta la E/S de PNG):
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/prompts-atlas.py -- <carpeta del paquete Kenney>
"""
import os, sys
import numpy as np
import bpy

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
PS = 'PlayStation Series/Default/'
GC = 'Nintendo Gamecube/Default/'
CELL, COLS = 48, 6

# (id del enum, marcador, archivo, recoloreo, tinte RGBA)
ICONS = [
    ('X',        'X',        PS + 'playstation_button_color_cross.png',    None, 0xFFFFFFFF),
    ('CIRCLE',   'O',        PS + 'playstation_button_color_circle.png',   None, 0xFFFFFFFF),
    ('SQUARE',   'SQ',       PS + 'playstation_button_color_square.png',   None, 0xFFFFFFFF),
    ('TRIANGLE', 'TRI',      PS + 'playstation_button_color_triangle.png', None, 0xFFFFFFFF),
    ('SELECT',   'SELECT',   PS + 'playstation3_button_select.png',        None, 0xFFFFFFFF),
    ('START',    'START',    PS + 'playstation3_button_start.png',         None, 0xFFFFFFFF),
    ('L1',       'L1',       PS + 'playstation_trigger_l1.png',            None, 0xFFFFFFFF),
    ('R1',       'R1',       PS + 'playstation_trigger_r1.png',            None, 0xFFFFFFFF),
    ('L2',       'L2',       PS + 'playstation_trigger_l2.png',            None, 0xFFFFFFFF),
    ('R2',       'R2',       PS + 'playstation_trigger_r2.png',            None, 0xFFFFFFFF),
    ('L3',       'L3',       PS + 'playstation_stick_l_press.png',         None, 0xFFFFFFFF),
    ('R3',       'R3',       PS + 'playstation_stick_r_press.png',         None, 0xFFFFFFFF),
    ('DPAD_UD',  'UPDOWN',   PS + 'playstation_dpad_vertical.png',         None, 0xFFFFFFFF),
    ('DPAD_LR',  'LEFTRIGHT', PS + 'playstation_dpad_horizontal.png',      None, 0xFFFFFFFF),
    ('DPAD',     'DPAD',     PS + 'playstation_dpad.png',                  None, 0xFFFFFFFF),
    ('STICK_L',  'STICK',    PS + 'playstation_stick_l.png',               None, 0xFFFFFFFF),
    ('STICK_R',  'RSTICK',   PS + 'playstation_stick_r.png',               None, 0xFFFFFFFF),
    ('N64_A',    'N64A',     GC + 'gamecube_button_color_a.png',           ('hue', 222), 0xFFFFFFFF),
    ('N64_B',    'N64B',     GC + 'gamecube_button_color_b.png',           ('hue', 128), 0xFFFFFFFF),
    ('N64_START', 'N64START', GC + 'gamecube_button_start.png',            ('disc', (0.86, 0.20, 0.18)), 0xFFFFFFFF),
    ('N64_Z',    'N64Z',     GC + 'gamecube_button_z.png',                 None, 0xA4AAB0FF),
    ('N64_L',    'N64L',     GC + 'gamecube_trigger_l.png',                None, 0xA4AAB0FF),
    ('N64_R',    'N64R',     GC + 'gamecube_trigger_r.png',                None, 0xA4AAB0FF),
    ('N64_DPAD', 'N64DPAD',  GC + 'gamecube_dpad.png',                     None, 0xFFFFFFFF),
    ('N64_DPAD_UP',    'N64DUP',    GC + 'gamecube_dpad_up.png',           None, 0xFFFFFFFF),
    ('N64_DPAD_DOWN',  'N64DDOWN',  GC + 'gamecube_dpad_down.png',         None, 0xFFFFFFFF),
    ('N64_DPAD_LEFT',  'N64DLEFT',  GC + 'gamecube_dpad_left.png',         None, 0xFFFFFFFF),
    ('N64_DPAD_RIGHT', 'N64DRIGHT', GC + 'gamecube_dpad_right.png',        None, 0xFFFFFFFF),
    ('N64_C_UP',    'N64CUP',    GC + 'gamecube_stick_c_color_up.png',     None, 0xFFFFFFFF),
    ('N64_C_DOWN',  'N64CDOWN',  GC + 'gamecube_stick_c_color_down.png',   None, 0xFFFFFFFF),
    ('N64_C_LEFT',  'N64CLEFT',  GC + 'gamecube_stick_c_color_left.png',   None, 0xFFFFFFFF),
    ('N64_C_RIGHT', 'N64CRIGHT', GC + 'gamecube_stick_c_color_right.png',  None, 0xFFFFFFFF),
    ('N64_STICK',   'N64STICK',  GC + 'gamecube_stick.png',                None, 0xFFFFFFFF),
]


def load(path):
    im = bpy.data.images.load(path)
    w, h = im.size
    a = np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1]  # de arriba abajo
    bpy.data.images.remove(im)
    return a


def hue_shift(a, hue):
    """Pone el matiz `hue` (grados) a los píxeles con color; el blanco queda."""
    rgb = a[..., :3]
    mx, mn = rgb.max(-1), rgb.min(-1)
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    out = rgb.copy()
    h = hue / 60.0
    c = mx * s
    x = c * (1 - abs(h % 2 - 1))
    m = mx - c
    z = np.zeros_like(c)
    r, g, b = [(c, x, z), (x, c, z), (z, c, x), (z, x, c), (x, z, c), (c, z, x)][int(h) % 6]
    new = np.stack([r + m, g + m, b + m], -1)
    sel = (s > 0.35)[..., None]
    a = a.copy()
    a[..., :3] = np.where(sel, new, out)
    return a


def disc_color(a, col):
    """El disco de Start (mitad de abajo del dibujo) al color `col`."""
    a = a.copy()
    h = a.shape[0]
    lower = np.zeros(a.shape[:2], bool)
    lower[int(h * 0.52):] = True
    sel = (a[..., 3] > 0.05) & lower
    a[sel, :3] = col
    return a


def down(a, n):
    """64 → 48 por promedio de área, con el alfa premultiplicado."""
    p = a[..., :3] * a[..., 3:4]
    big = np.concatenate([p, a[..., 3:4]], -1)
    big = np.repeat(np.repeat(big, 3, 0), 3, 1)  # 192
    f = big.shape[0] // n  # 4
    big = big.reshape(n, f, n, f, 4).mean((1, 3))
    al = big[..., 3:4]
    rgb = np.where(al > 1e-4, big[..., :3] / np.maximum(al, 1e-4), 0)
    return np.concatenate([np.clip(rgb, 0, 1), al], -1)


def main():
    src = sys.argv[sys.argv.index('--') + 1]
    rows = (len(ICONS) + COLS - 1) // COLS
    W, H = COLS * CELL, rows * CELL
    atlas = np.zeros((H, W, 4), np.float32)
    for i, (_, _, f, fx, _) in enumerate(ICONS):
        a = load(os.path.join(src, f))
        if fx and fx[0] == 'hue':
            a = hue_shift(a, fx[1])
        elif fx and fx[0] == 'disc':
            a = disc_color(a, fx[1])
        r, c = divmod(i, COLS)
        atlas[r * CELL:(r + 1) * CELL, c * CELL:(c + 1) * CELL] = down(a, CELL)
    out = os.path.join(ROOT, 'assets/menu/prompts.png')
    im = bpy.data.images.new('prompts', W, H, alpha=True)
    im.alpha_mode = 'STRAIGHT'
    im.pixels = atlas[::-1].ravel().tolist()
    im.filepath_raw = out
    im.file_format = 'PNG'
    im.save()
    hdr = os.path.join(ROOT, 'src/frontend/prompts_atlas.h')
    with open(hdr, 'w') as h:
        h.write('/* Generado por tools/prompts-atlas.py (no editar). Íconos de Kenney «Input Prompts» (CC0). */\n')
        h.write('#ifndef SALSA64_PROMPTS_ATLAS_H\n#define SALSA64_PROMPTS_ATLAS_H\n\n')
        h.write('#define PROMPT_ATLAS_W %d\n#define PROMPT_ATLAS_H %d\n#define PROMPT_CELL %d\n\n' % (W, H, CELL))
        h.write('enum prompt_icon {\n')
        for i, (n, _, _, _, _) in enumerate(ICONS):
            h.write('\tPROMPT_%s%s\n' % (n, ' = 0,' if i == 0 else ','))
        h.write('\tPROMPT_COUNT\n};\n\n')
        h.write('/* Marcador {NOMBRE} de cada ícono, su casilla (x, y) en el atlas y su tinte RGBA. */\n')
        h.write('static const struct {\n\tconst char *name;\n\tunsigned short x, y;\n\tunsigned tint;\n} prompt_table[PROMPT_COUNT] = {\n')
        for i, (n, m, _, _, t) in enumerate(ICONS):
            r, c = divmod(i, COLS)
            h.write('\t[PROMPT_%s] = { "%s", %d, %d, 0x%08XU },\n' % (n, m, c * CELL, r * CELL, t))
        h.write('};\n\n#endif\n')
    print('atlas %dx%d, %d íconos' % (W, H, len(ICONS)))


main()
