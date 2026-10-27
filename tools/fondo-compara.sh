#!/bin/sh
# Compara el fondo «Shader» del RSX (tools/fondo-ref.c, modo rsx) contra el
# shader original en varios instantes y a medianoche y mediodía. Deja los
# PPM y PNG en build/fondo-shader/ (o-* el original, r-* el del RSX, d-* la
# diferencia ×8).
#   tools/fondo-compara.sh [ANCHO ALTO]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${1:-1920}"
H="${2:-1080}"
OUT="$ROOT/build/fondo-shader"
mkdir -p "$OUT"
cc -O2 -I"$ROOT/src" -o "$ROOT/build/fondo-ref" "$ROOT/tools/fondo-ref.c" -lm -lpthread
cd "$OUT"
for t in 10 47.3 123.4 600; do
	for h in 0 12; do
		n="$H-t$t-h$h"
		"$ROOT/build/fondo-ref" original "$W" "$H" "$t" "$h" "o-$n.ppm"
		"$ROOT/build/fondo-ref" rsx "$W" "$H" "$t" "$h" "r-$n.ppm" 2>/dev/null
		printf 'iTime %-6s hora %2s: ' "$t" "$h"
		"$ROOT/build/fondo-ref" compara "o-$n.ppm" "r-$n.ppm" "d-$n.ppm"
		if command -v sips >/dev/null 2>&1; then
			for p in o r d; do
				sips -s format png "$p-$n.ppm" --out "$p-$n.png" >/dev/null
			done
		fi
	done
done
