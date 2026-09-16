#!/usr/bin/env bash
# ===========================================================================
#  The Black Hole - build the screensaver for Linux and install it into
#  XScreenSaver
#
#  Uses what 1-download-tools-linux.sh set up: the SDL3 source in ~/workenv
#  (override with BH_WORKENV) and, where the system ones were too old, the
#  pinned CMake and Ninja from the same folder. The build does not use the
#  network.
#
#  Installing puts the program in XScreenSaver's hack directory and its
#  settings page in XScreenSaver's config directory (both need sudo), then
#  adds it to your own ~/.xscreensaver list so xscreensaver-settings shows it.
#
#  Usage:  ./2-build-and-install-linux.sh [options]
#            --debug        build with symbols into build/linux-debug
#            --clean        throw the build folder away first
#            --no-install   build only
#            --system-sdl   link the system's SDL3 instead of building it
#            --prefix DIR   install under DIR instead of XScreenSaver's own
#                           directories (then XScreenSaver is given the full path)
#            --uninstall    remove an installed copy and its list entry
# ===========================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKENV="${BH_WORKENV:-$HOME/workenv}"
V_CMAKE=4.4.3
V_SDL=3.4.16

BUILD_TYPE=Release
BUILD_DIR="$ROOT/build/linux"
CLEAN=0
INSTALL=1
SYSTEM_SDL=0
PREFIX=""
UNINSTALL=0

while [ $# -gt 0 ]; do
    case "$1" in
        --debug)      BUILD_TYPE=Debug; BUILD_DIR="$ROOT/build/linux-debug" ;;
        --clean)      CLEAN=1 ;;
        --no-install) INSTALL=0 ;;
        --system-sdl) SYSTEM_SDL=1 ;;
        --prefix)     shift; PREFIX="${1:?--prefix needs a directory}" ;;
        --uninstall)  UNINSTALL=1 ;;
        -h|--help)    sed -n '2,24p' "$0"; exit 0 ;;
        *)            echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

say() { printf ' %s\n' "$*"; }
die() { printf '\n ERROR: %s\n\n FAILED.\n' "$*" >&2; exit 1; }

SUDO=""
[ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1 && SUDO="sudo"

# --- where XScreenSaver keeps its hacks and their settings pages -------------
# Debian, Ubuntu and Fedora use /usr/libexec/xscreensaver; Arch uses
# /usr/lib/xscreensaver. The one that already holds hacks wins.
detect_hackdir() {
    local d
    for d in /usr/libexec/xscreensaver /usr/lib/xscreensaver /usr/lib64/xscreensaver \
             /usr/local/libexec/xscreensaver /usr/local/lib/xscreensaver; do
        if [ -d "$d" ] && [ -n "$(ls -A "$d" 2>/dev/null)" ]; then echo "$d"; return; fi
    done
    echo /usr/libexec/xscreensaver
}
detect_confdir() {
    local d
    for d in /usr/share/xscreensaver/config /usr/local/share/xscreensaver/config; do
        [ -d "$d" ] && { echo "$d"; return; }
    done
    echo /usr/share/xscreensaver/config
}

if [ -n "$PREFIX" ]; then
    HACKDIR="$PREFIX/libexec/xscreensaver"
    CONFDIR="$PREFIX/share/xscreensaver/config"
else
    HACKDIR="$(detect_hackdir)"
    CONFDIR="$(detect_confdir)"
fi
HACK="$HACKDIR/theblackhole"
# In XScreenSaver's own directory the bare name is enough (it puts that
# directory on the hacks' PATH); anywhere else it needs the full path.
if [ -n "$PREFIX" ]; then LIST_CMD="$HACK -root"; else LIST_CMD="theblackhole -root"; fi

# --- the per-user list in ~/.xscreensaver -------------------------------------
XSS_FILE="$HOME/.xscreensaver"

register_user() {
    if [ ! -f "$XSS_FILE" ]; then
        say "[note] $XSS_FILE does not exist yet. Open xscreensaver-settings once"
        say "       (it writes the file when it closes), then run this script again"
        say "       to add The Black Hole to the list."
        return 0
    fi
    if grep -q 'theblackhole' "$XSS_FILE"; then
        say "Already in $XSS_FILE"
        return 0
    fi
    cp "$XSS_FILE" "$XSS_FILE.bak-theblackhole"
    # The list is one long resource value: "programs:" and then one entry per
    # line, each ending in a literal \n\ . The new entry goes first.
    awk -v entry="  GL:                $LIST_CMD                        \\\\n\\\\" '
        { print }
        !done && /^programs:/ { print entry; done = 1 }
    ' "$XSS_FILE.bak-theblackhole" > "$XSS_FILE"
    if grep -q 'theblackhole' "$XSS_FILE"; then
        say "Added to $XSS_FILE (backup: $XSS_FILE.bak-theblackhole)"
    else
        cp "$XSS_FILE.bak-theblackhole" "$XSS_FILE"
        say "[warn] no 'programs:' list found in $XSS_FILE; add this line to it yourself:"
        say "         GL: $LIST_CMD"
    fi
}

unregister_user() {
    [ -f "$XSS_FILE" ] && grep -q 'theblackhole' "$XSS_FILE" || return 0
    cp "$XSS_FILE" "$XSS_FILE.bak-theblackhole"
    grep -v 'theblackhole' "$XSS_FILE.bak-theblackhole" > "$XSS_FILE"
    say "Removed from $XSS_FILE (backup: $XSS_FILE.bak-theblackhole)"
}

# ===========================================================================
if [ "$UNINSTALL" = 1 ]; then
    echo
    say "The Black Hole - uninstall"
    for f in "$HACK" "$CONFDIR/theblackhole.xml"; do
        if [ -e "$f" ]; then
            if [ -w "$(dirname "$f")" ]; then rm -f "$f"; else $SUDO rm -f "$f"; fi
            say "Removed $f"
        fi
    done
    unregister_user
    say "Saved settings are kept in ${XDG_CONFIG_HOME:-$HOME/.config}/theblackhole"
    echo
    exit 0
fi

# ===========================================================================
#  toolchain
# ===========================================================================
for d in "$WORKENV/cmake-$V_CMAKE-linux-x86_64/bin" "$WORKENV/cmake-$V_CMAKE-linux-aarch64/bin" "$WORKENV/ninja"; do
    [ -d "$d" ] && PATH="$d:$PATH"
done
export PATH

command -v cmake >/dev/null 2>&1 || die "CMake not found - run ./1-download-tools-linux.sh first"
NINJA="$(command -v ninja || command -v ninja-build || true)"
[ -n "$NINJA" ] || die "Ninja not found - run ./1-download-tools-linux.sh first"

SDL_ARGS=()
if [ "$SYSTEM_SDL" = 1 ]; then
    SDL_ARGS+=(-DBH_SYSTEM_SDL=ON)
elif [ -f "$WORKENV/SDL3-$V_SDL/CMakeLists.txt" ]; then
    SDL_ARGS+=(-DBH_SYSTEM_SDL=OFF "-DFETCHCONTENT_SOURCE_DIR_SDL3=$WORKENV/SDL3-$V_SDL")
else
    die "SDL3 source not found in $WORKENV - run ./1-download-tools-linux.sh first
        (or pass --system-sdl to use an SDL3 installed by your distribution)"
fi

echo
say "The Black Hole - Linux build"
say "----------------------------"
say "toolchain : $WORKENV"
say "build     : $BUILD_TYPE, $BUILD_DIR"
say "$(cmake --version | head -n1), ninja $("$NINJA" --version)"
[ "$INSTALL" = 1 ] && say "install   : $HACK"
echo

if [ "$CLEAN" = 1 ] && [ -d "$BUILD_DIR" ]; then
    say "Removing $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi

# ===========================================================================
#  build
# ===========================================================================
say "Configuring..."
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    "-DCMAKE_MAKE_PROGRAM=$NINJA" \
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE" \
    "-DBH_XSS_HACKDIR=$HACKDIR" \
    "-DBH_XSS_CONFDIR=$CONFDIR" \
    "${SDL_ARGS[@]}"

echo
say "Compiling..."
cmake --build "$BUILD_DIR" --parallel

OUT="$BUILD_DIR/theblackhole"
[ -x "$OUT" ] || die "the build reported success but $OUT is missing"
echo
say "Built: $OUT"
say "       $(stat -c %s "$OUT") bytes, SHA-256 $(sha256sum "$OUT" | cut -d' ' -f1)"

if [ "$INSTALL" = 0 ]; then
    echo
    say "Try it in a window:   $OUT --window"
    say "Install it:           ./2-build-and-install-linux.sh"
    echo
    exit 0
fi

# ===========================================================================
#  install
# ===========================================================================
echo
say "Installing (sudo may ask for your password)..."
if [ -n "$PREFIX" ] && mkdir -p "$PREFIX" 2>/dev/null && [ -w "$PREFIX" ]; then
    cmake --install "$BUILD_DIR"      # a prefix of your own needs no sudo
else
    $SUDO cmake --install "$BUILD_DIR"
fi
[ -x "$HACK" ] || die "install finished but $HACK is missing"

register_user

if ! command -v xscreensaver >/dev/null 2>&1; then
    echo
    say "[note] XScreenSaver is not installed. Install it (./1-download-tools-linux.sh"
    say "       does), or run the screensaver directly: $HACK --window"
fi

echo
say "Done. Open xscreensaver-settings and pick \"The Black Hole\"."
say "GNOME and KDE use their own screen lockers, which cannot run third-party"
say "screensavers; see README.md for running XScreenSaver alongside them."
echo
