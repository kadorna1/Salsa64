#!/bin/sh
# Vista previa del fondo «Seda» del menú (tools/seda-vista.c): la malla con
# el mismo código C del SPE, rasterizada por software, en varios instantes
# y a mediodía y a medianoche. Deja los PNG en build/fondo-seda/.
#   tools/seda-vista.sh [ANCHO ALTO]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${1:-1280}"
H="${2:-720}"
OUT="$ROOT/build/fondo-seda"
mkdir -p "$OUT"
cc -O2 -I"$ROOT/src" -o "$ROOT/build/seda-vista" "$ROOT/tools/seda-vista.c" -lm
for t in 5 21 47 130; do
	for h in 13 23 18; do
		"$ROOT/build/seda-vista" "$W" "$H" "$t" "$h" "$OUT/seda-t$t-h$h.png"
		echo "$OUT/seda-t$t-h$h.png"
	done
done
