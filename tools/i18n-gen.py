#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Vuelca tools/i18n/tabla.tsv en src/frontend/i18n_tabla.inc.

Columnas: es, en, fr, de, it, pt, nl, sv, da, no, fi, pl, tr, ru (separadas por tabulador,
sin comillas ni escapes). La salida va ordenada por bytes UTF-8 (lo mismo que
strcmp), como pide bsearch. Con --check solo comprueba que el .inc esté al día.
"""
import os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
TSV = os.path.join(ROOT, 'tools/i18n/tabla.tsv')
INC = os.path.join(ROOT, 'src/frontend/i18n_tabla.inc')
NCOL = 14


def esc(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')


def build():
    rows = {}
    for n, line in enumerate(open(TSV, encoding='utf8').read().split('\n'), 1):
        if not line:
            continue
        c = line.split('\t')
        if len(c) != NCOL or not all(c):
            sys.exit('%s:%d: %d columnas o alguna vacía' % (TSV, n, len(c)))
        if c[0] in rows:
            sys.exit('%s:%d: clave repetida «%s»' % (TSV, n, c[0]))
        rows[c[0]] = c
    out = ['/* SPDX-License-Identifier: GPL-2.0-or-later */',
           '/* Generado por tools/i18n-gen.py desde tools/i18n/tabla.tsv: no editar a mano. */']
    for k in sorted(rows, key=lambda s: s.encode('utf8')):
        c = rows[k]
        out.append('\t{ "%s",' % esc(k))
        out.append('\t  { %s } },' % ', '.join('"%s"' % esc(x) for x in c[1:]))
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    text = build()
    if '--check' in sys.argv:
        sys.exit(0 if os.path.exists(INC) and open(INC, encoding='utf8').read() == text else 'i18n_tabla.inc desactualizado')
    open(INC, 'w', encoding='utf8').write(text)
