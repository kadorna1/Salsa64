#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Comprueba que cada texto literal que el frontend pasa a tr() o a las
funciones que lo dibujan o avisan (osd_notice, menu_set_status, stext,
osd_confirm_draw...) tenga entrada en la tabla de src/frontend/i18n.c.
Uso: tools/i18n-check.py [-v]   (sale con 1 si falta alguna)."""
import glob, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
CALLS = ('tr', 'osd_notice', 'menu_set_status', 'stext', 'cells_w', 'osd_confirm_draw', 'fail', 'osd_fps_text')
# Nombres propios que no se traducen y no necesitan entrada.
KEEP = {'Transfer Pak', 'Rumble Pak', 'Controller Pak', 'Salsa64'}
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def table_keys():
    src = open(os.path.join(ROOT, 'src/frontend/i18n_tabla.inc'), encoding='utf8').read()
    return {m.group(1) for m in re.finditer(r'^\t\{ "((?:[^"\\]|\\.)*)",', src, re.M)}


def strip(src):
    src = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), src, flags=re.S)
    return re.sub(r'//[^\n]*', '', src)


def args_of(src, i):
    depth, j, instr = 0, i, False
    while j < len(src):
        c = src[j]
        if instr:
            if c == '\\':
                j += 1
            elif c == '"':
                instr = False
        elif c == '"':
            instr = True
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
        j += 1
    return ''


# Archivos donde casi cualquier literal con aire de texto se dibuja (filas,
# rótulos, nombres de opciones): ahí se revisan todos, no solo los que van en
# una llamada conocida.
UI_FILES = ('menu.c', 'osd.c', 'osd_fps.c', 'pausectl.c', 'romlist.c', 'states.c', 'cheats.c')
SKIP = re.compile(r'(?<![\w.>])(salsa64_log\w*|printf|fprintf|strcmp|strncmp|strcasecmp|memcmp|fopen|strstr|strchr|'
                  r'getenv|sscanf|fputs|fwrite|fputc|_Static_assert|name_find)\s*\(')
# Nombres propios, claves de archivos de configuración y cadenas que no se ven.
UI_IGNORE = {'Español', 'English', 'Français', 'Deutsch', 'Italiano', 'Português', 'Nederlands', 'Svenska', 'Dansk',
             'Norsk', 'Suomi', 'Polski', 'Türkçe', 'Русский', 'PAL', 'PAL-M', 'NTSC', 'EEPROM 4K', 'EEPROM 16K',
             'SRAM', 'SRAM 768K', 'SRAM 1M', 'FlashRAM', 'Start', 'L1', 'R1', 'L2', 'R2', 'L3', 'R3', 'A', 'B', 'Z',
             'L', 'R', 'Gateway', 'Stick', 'Select',
             # El nombre de un idioma CJK guardado cuando no hay fuente del sistema (en inglés a propósito).
             'Japanese', 'Korean', 'Chinese (Traditional)', 'Chinese (Simplified)'}


def ui_literals(src):
    out = list(src)
    for m in SKIP.finditer(src):
        a = args_of(src, m.end() - 1)
        for k in range(m.end() - 1, m.end() - 1 + len(a)):
            if out[k] != '\n':
                out[k] = ' '
    s2 = re.sub(r'^\s*#.*$', '', ''.join(out), flags=re.M)
    for ln, line in enumerate(s2.split('\n'), 1):
        for m in LIT.finditer(line):
            s = m.group(1)
            if (s[:1].isupper() or ' ' in s) and re.search(r'[a-záéíóúñ]{3}', s) and not re.search(r'[=\\]|^[a-z_]+$', s):
                yield ln, s


def main():
    keys, bad = table_keys(), []
    pat = re.compile(r'(?<![\w.>])(' + '|'.join(CALLS) + r')\s*\(')
    for f in sorted(glob.glob(os.path.join(ROOT, 'src/frontend/*.c'))):
        if f.endswith('i18n.c'):
            continue
        src = strip(open(f, encoding='utf8').read())
        for m in pat.finditer(src):
            line = src.count('\n', 0, m.start()) + 1
            call = args_of(src, m.end() - 1)
            if m.group(1) in ('stext', 'cells_w'):
                call = re.sub(r'^\(', '', call)
            for lit in LIT.findall(call):
                if not re.search(r'[A-Za-zÀ-ÿ]{2}', lit) or lit in keys or lit in KEEP:
                    continue
                if re.fullmatch(r'[%\w.:/ \-]*%[\w.]*[%\w.:/ \-]*', lit) and not re.search(r'[a-záéíóú]{4}', lit):
                    continue
                bad.append((os.path.relpath(f, ROOT), line, lit))
    for name in UI_FILES:
        f = os.path.join(ROOT, 'src/frontend', name)
        for ln, s in ui_literals(strip(open(f, encoding='utf8').read())):
            if s not in keys and s not in KEEP and s not in UI_IGNORE and (name, ln, s) not in bad:
                bad.append((os.path.relpath(f, ROOT), ln, s))
    for f, l, s in bad:
        print('%s:%d: sin traducción: «%s»' % (f, l, s))
    print('%d textos sin entrada' % len(bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
