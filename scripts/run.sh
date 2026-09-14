#!/bin/bash
# Compila (si hace falta) y levanta FastNote.
# Uso: scripts/run.sh [archivo] [--font PATH]
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
SYSROOT_PKG="/tmp/opencode/deps/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig"

# En máquinas sin libsdl3-dev/libfreetype-dev en el sistema pero con el
# sysroot local extraído, úsalo automáticamente.
if ! pkg-config --exists sdl3 freetype2 2>/dev/null && [ -d "$SYSROOT_PKG" ]; then
    export PKG_CONFIG_PATH="$SYSROOT_PKG"
    export PKG_CONFIG_SYSROOT_DIR="/tmp/opencode/deps/sysroot"
fi

if [ ! -x "$BUILD/fastnote" ]; then
    echo "== configurando =="
    cmake -S "$ROOT" -B "$BUILD" || exit 1
fi
echo "== compilando =="
cmake --build "$BUILD" -j || exit 1

echo "== levantando fastnote =="
exec "$BUILD/fastnote" "$@"
