#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Convierte el cartucho del modelo «Stylized N64» (glTF binario) en el
recurso compacto del menú: assets/menu/cartucho.mesh y la textura del
plástico, reducida y oscurecida (assets/menu/cartucho_plastico.png).

Solo la biblioteca estándar de Python. Uso:

    tools/cartucho-malla.py CARTUCHO.glb [TEXTURA_BASE.png] [--out assets/menu]

Formato de cartucho.mesh (todo big-endian):

    0   "CMSH"
    4   u16 versión (1)
    6   u16 vértices
    8   u16 triángulos de plástico
    10  u16 triángulos de etiqueta
    12  vértices: 8 × f32 (x y z, nx ny nz, u v), 32 bytes cada uno
    ..  índices: u16 × 3 × (plástico + etiqueta); primero los del plástico

El modelo queda centrado en el origen, con el alto (y) igual a 1, la y hacia
arriba, la x a la derecha y la z hacia quien mira cuando la etiqueta da la
cara. u, v son las del glTF (v hacia abajo, como las filas de la imagen).
"""
import json
import math
import struct
import sys
import zlib

DARKEN = 0.52   # el plástico se oscurece para que quede en el gris de la N64
TEX = 256


def read_glb(path):
    d = open(path, 'rb').read()
    magic, ver, total = struct.unpack('<4sII', d[:12])
    assert magic == b'glTF' and ver == 2
    off, js, bn = 12, None, None
    while off < total:
        n, t = struct.unpack('<II', d[off:off + 8])
        body = d[off + 8:off + 8 + n]
        if t == 0x4E4F534A:
            js = json.loads(body)
        elif t == 0x004E4942:
            bn = body
        off += 8 + n
    return js, bn


def accessor(js, bn, i):
    a = js['accessors'][i]
    bv = js['bufferViews'][a['bufferView']]
    off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[a['type']]
    fmt = {5126: 'f', 5123: 'H', 5125: 'I'}[a['componentType']]
    vals = struct.unpack_from('<%d%s' % (a['count'] * n, fmt), bn, off)
    return [vals[k:k + n] for k in range(0, len(vals), n)] if n > 1 else list(vals)


def qrot(q, v):
    x, y, z, w = q
    # v' = v + 2w(q×v) + 2 q×(q×v)
    cx = (y * v[2] - z * v[1], z * v[0] - x * v[2], x * v[1] - y * v[0])
    c2 = (y * cx[2] - z * cx[1], z * cx[0] - x * cx[2], x * cx[1] - y * cx[0])
    return tuple(v[i] + 2 * (w * cx[i] + c2[i]) for i in range(3))


def build_mesh(path):
    js, bn = read_glb(path)
    node = js['nodes'][0]
    q = node.get('rotation', [0, 0, 0, 1])
    s = node.get('scale', [1, 1, 1])
    t = node.get('translation', [0, 0, 0])
    verts, tris = [], [[], []]
    mats = js['materials']
    for prim in js['meshes'][0]['primitives']:
        at = prim['attributes']
        pos = accessor(js, bn, at['POSITION'])
        nor = accessor(js, bn, at['NORMAL'])
        uv = accessor(js, bn, at['TEXCOORD_0'])
        idx = accessor(js, bn, prim['indices'])
        base = len(verts)
        for p, n, u in zip(pos, nor, uv):
            p = qrot(q, (p[0] * s[0], p[1] * s[1], p[2] * s[2]))
            p = (p[0] + t[0], p[1] + t[1], p[2] + t[2])
            n = qrot(q, n)
            ln = math.sqrt(sum(c * c for c in n)) or 1.0
            verts.append([p, tuple(c / ln for c in n), u])
        label = mats[prim['material']]['name'].startswith('etiqueta')
        for k in range(0, len(idx), 3):
            tris[1 if label else 0].append(tuple(base + i for i in idx[k:k + 3]))
    lo = [min(v[0][i] for v in verts) for i in range(3)]
    hi = [max(v[0][i] for v in verts) for i in range(3)]
    ctr = [(lo[i] + hi[i]) / 2 for i in range(3)]
    sc = 1.0 / (hi[1] - lo[1])
    out = bytearray(b'CMSH')
    out += struct.pack('>HHHH', 1, len(verts), len(tris[0]), len(tris[1]))
    for p, n, u in verts:
        out += struct.pack('>8f', *[(p[i] - ctr[i]) * sc for i in range(3)], *n, *u)
    for group in tris:
        for tri in group:
            out += struct.pack('>3H', *tri)
    return bytes(out), len(verts), len(tris[0]), len(tris[1])


# ---- PNG mínimo ------------------------------------------------------------

def read_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n'
    off, idat, w = 8, b'', None
    while off < len(d):
        n, typ = struct.unpack('>I4s', d[off:off + 8])
        body = d[off + 8:off + 8 + n]
        if typ == b'IHDR':
            w, h, depth, ctype, _, _, il = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype in (2, 6) and il == 0
        elif typ == b'IDAT':
            idat += body
        off += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        cur = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                cur[i] = (cur[i] + a) & 255
            elif f == 2:
                cur[i] = (cur[i] + b) & 255
            elif f == 3:
                cur[i] = (cur[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[i] = (cur[i] + pr) & 255
        rows.append(cur)
        prev = cur
    return w, h, bpp, rows


def write_png(path, w, h, rgb_rows):
    def chunk(t, b):
        c = struct.pack('>I', len(b)) + t + b
        return c + struct.pack('>I', zlib.crc32(t + b) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + bytes(r) for r in rgb_rows)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def plastic(src, dst):
    w, h, bpp, rows = read_png(src)
    f = w // TEX
    assert f >= 1 and w == h and w % TEX == 0
    out = []
    for y in range(TEX):
        r = bytearray(TEX * 3)
        for x in range(TEX):
            for c in range(3):
                acc = 0
                for yy in range(f):
                    row = rows[y * f + yy]
                    for xx in range(f):
                        acc += row[(x * f + xx) * bpp + c]
                r[x * 3 + c] = min(255, int(acc / (f * f) * DARKEN + 0.5))
        out.append(r)
    write_png(dst, TEX, TEX, out)


def main(argv):
    outdir = 'assets/menu'
    args = []
    i = 1
    while i < len(argv):
        if argv[i] == '--out':
            outdir = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1
    if not args:
        sys.exit(__doc__)
    mesh, nv, np_, nl = build_mesh(args[0])
    open(outdir + '/cartucho.mesh', 'wb').write(mesh)
    print('cartucho.mesh: %d vértices, %d + %d triángulos, %d bytes' % (nv, np_, nl, len(mesh)))
    if len(args) > 1:
        plastic(args[1], outdir + '/cartucho_plastico.png')
        print('cartucho_plastico.png: %dx%d' % (TEX, TEX))


if __name__ == '__main__':
    main(sys.argv)
