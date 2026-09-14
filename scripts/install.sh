#!/bin/bash
# Install FastNote from a release tarball.
#
# Usage: scripts/install.sh [--system] [--no-mime] [tarball]
#
#   (default)  user install:  ~/.local/bin + ~/.local/share/applications
#   --system   system install: /usr/local/bin + /usr/share/applications (sudo)
#   --no-mime  skip registering FastNote as default for text/code files
#   tarball    path to fastnote-*.tar.gz (default: newest in build-release/;
#              runs scripts/release.sh first if none exists)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODE="user"
SET_MIME=1
TARBALL=""

for arg in "$@"; do
    case "$arg" in
        --system) MODE="system" ;;
        --no-mime) SET_MIME=0 ;;
        -h|--help)
            sed -n '2,12p' "$0"
            exit 0
            ;;
        *) TARBALL="$arg" ;;
    esac
done

if [ -z "$TARBALL" ]; then
    TARBALL="$(ls -t "$ROOT"/build-release/fastnote-*.tar.gz 2>/dev/null | head -1 || true)"
fi
if [ -z "$TARBALL" ] || [ ! -f "$TARBALL" ]; then
    echo "No tarball found, building a release first..."
    "$ROOT/scripts/release.sh"
    TARBALL="$(ls -t "$ROOT"/build-release/fastnote-*.tar.gz | head -1)"
fi

if [ "$MODE" = "system" ]; then
    BINDIR="/usr/local/bin"
    APPDIR="/usr/share/applications"
    if [ "$(id -u)" -ne 0 ]; then
        command -v sudo >/dev/null 2>&1 || {
            echo "sudo required for --system" >&2
            exit 1
        }
        SUDO="sudo"
    else
        SUDO=""
    fi
else
    BINDIR="$HOME/.local/bin"
    APPDIR="$HOME/.local/share/applications"
    SUDO=""
    mkdir -p "$BINDIR" "$APPDIR"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
tar xzf "$TARBALL" -C "$TMP"
PKGDIR="$(echo "$TMP"/fastnote-*)"

echo "== installing FastNote ($MODE) =="
if [ -n "$SUDO" ]; then
    $SUDO install -m755 "$PKGDIR/fastnote" "$BINDIR/fastnote"
else
    install -m755 "$PKGDIR/fastnote" "$BINDIR/fastnote"
fi
# Absolute Exec path so the launcher works regardless of PATH.
sed "s|^Exec=fastnote %F|Exec=$BINDIR/fastnote %F|" \
    "$PKGDIR/fastnote.desktop" > "$TMP/fastnote.desktop"
if [ -n "$SUDO" ]; then
    $SUDO install -m644 "$TMP/fastnote.desktop" "$APPDIR/fastnote.desktop"
    $SUDO update-desktop-database "$APPDIR" 2>/dev/null || true
else
    install -m644 "$TMP/fastnote.desktop" "$APPDIR/fastnote.desktop"
    command -v update-desktop-database >/dev/null 2>&1 &&
        update-desktop-database "$APPDIR" 2>/dev/null || true
fi

if [ "$SET_MIME" -eq 1 ] && command -v xdg-mime >/dev/null 2>&1; then
    echo "== registering default for text/code =="
    for mt in text/plain text/x-chdr text/x-csrc text/x-c++src text/x-java \
              text/x-python text/x-shellscript text/html text/markdown; do
        xdg-mime default fastnote.desktop "$mt"
    done
fi

echo "== verifying =="
"$BINDIR/fastnote" --version
if [ "$SET_MIME" -eq 1 ] && command -v xdg-mime >/dev/null 2>&1; then
    echo "default for text/plain: $(xdg-mime query default text/plain)"
fi
echo "OK: FastNote installed ($MODE)."
