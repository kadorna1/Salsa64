#!/bin/sh
# Ejecuta make (o cualquier comando con --sh) dentro de la imagen del toolchain.
#   tools/build.sh                 make all (todos los programas de la Fase 0)
#   tools/build.sh -C tests/phase0/jit-ppe pkg
#   tools/build.sh --sh 'ppu-gcc --version'
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${SALSA64_IMAGE:-salsa64-toolchain:dev}"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
	echo "No existe la imagen $IMAGE: correr tools/docker-image.sh" >&2
	exit 1
fi

TTY=""
[ -t 0 ] && [ -t 1 ] && TTY="-t"

# La CI puede inyectar una versión de desarrollo derivada del último hito de
# main. Sin override, se conserva la versión nativa del árbol Git.
# Se calcula aquí: dentro del contenedor, el .git de un worktree apunta a una
# ruta del host que no está montada. Sin git la versión queda «desconocida».
VERSION="${SALSA64_VERSION:-$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || true)}"
COMMIT="${SALSA64_COMMIT:-$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || true)}"

run() {
	docker run --rm -i $TTY --user "$(id -u):$(id -g)" -e HOME=/tmp \
		-e SALSA64_VERSION="$VERSION" -e SALSA64_COMMIT="$COMMIT" \
		-v "$ROOT:/src" -w /src "$IMAGE" "$@"
}

if [ "${1:-}" = "--sh" ]; then
	shift
	run sh -c "$*"
else
	run make "$@"
fi
