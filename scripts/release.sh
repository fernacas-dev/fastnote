#!/bin/bash
# Prepare a release tarball of the compiled application:
# fresh Release build -> tests -> stripped binary -> tarball + checksums.
# Usage: scripts/release.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Same fallback as run.sh: use the local sysroot when system dev packages
# are missing but the extracted headers exist.
if ! pkg-config --exists sdl3 freetype2 2>/dev/null &&
    [ -d /tmp/opencode/deps/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig ]; then
    export PKG_CONFIG_PATH=/tmp/opencode/deps/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig
    export PKG_CONFIG_SYSROOT_DIR=/tmp/opencode/deps/sysroot
fi
VER="$(grep -E '^#define FASTNOTE_VERSION ' "$ROOT/src/version.h" | awk '{print $3}' | tr -d '"')"
ARCH="$(uname -m)"
BUILD="$ROOT/build-release"
NAME="fastnote-$VER-linux-$ARCH"
STAGE="$BUILD/stage/$NAME"

echo "== FastNote $VER release ($ARCH) =="

echo "== configuring (Release) =="
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release

echo "== building =="
cmake --build "$BUILD" -j

echo "== testing =="
ctest --test-dir "$BUILD" --output-on-failure

echo "== staging =="
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BUILD/fastnote" "$STAGE/"
if command -v strip >/dev/null 2>&1; then
    strip --strip-unneeded "$STAGE/fastnote"
fi
cp "$ROOT/README.md" "$STAGE/"
cp "$ROOT/packaging/fastnote.desktop" "$STAGE/"

echo "== smoke test (staged binary) =="
"$STAGE/fastnote" --version
if SDL_VIDEODRIVER=dummy timeout 3 "$STAGE/fastnote" >/dev/null 2>&1; then
    echo "(app exited on its own)"
else
    rc=$?
    if [ "$rc" -ne 124 ]; then
        echo "smoke test failed (rc=$rc)" >&2
        exit 1
    fi
    echo "(survived 3s headless run)"
fi

echo "== packaging =="
tar -czf "$BUILD/$NAME.tar.gz" -C "$BUILD/stage" "$NAME"
(cd "$BUILD" && sha256sum "$NAME.tar.gz" > "$NAME.sha256")

echo
echo "Runtime deps (expected on target system):"
ldd "$STAGE/fastnote" 2>/dev/null | grep -E "SDL|freetype" || true
echo
ls -la "$BUILD/$NAME.tar.gz" "$BUILD/$NAME.sha256"
echo
echo "Release ready:"
echo "  tarball:  $BUILD/$NAME.tar.gz"
echo "  checksum: $BUILD/$NAME.sha256"
echo "Install (user): tar xzf $NAME.tar.gz && sudo install -m755 $NAME/fastnote /usr/local/bin/fastnote"
