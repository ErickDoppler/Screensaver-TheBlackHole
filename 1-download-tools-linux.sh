#!/usr/bin/env bash
# ===========================================================================
#  The Black Hole - install the build toolchain (Linux)
#
#  1. Installs the compiler, CMake, Ninja, the X11 / OpenGL / Wayland headers
#     SDL3 builds against, and XScreenSaver, from the distribution's own
#     packages (apt, dnf, pacman or zypper; asks for sudo).
#  2. Downloads the pinned SDL3 source into ~/workenv (override with the first
#     argument or BH_WORKENV), so the build itself never touches the network.
#  3. If the distribution's CMake is older than 3.25 or Ninja is missing,
#     downloads the pinned official release binaries into the same folder.
#
#  Every download is checked against a pinned SHA-256 before it is unpacked.
#
#  Usage:  ./1-download-tools-linux.sh [workenv-dir] [--no-packages]
#                                      [--no-xscreensaver] [--force]
#            --no-packages      skip step 1 (you install the packages yourself)
#            --no-xscreensaver  do not install XScreenSaver
#            --force            unpack again even if a tool folder exists
# ===========================================================================
set -euo pipefail

WORKENV="${BH_WORKENV:-$HOME/workenv}"
PACKAGES=1
XSS=1
FORCE=0
for a in "$@"; do
    case "$a" in
        --no-packages)     PACKAGES=0 ;;
        --no-xscreensaver) XSS=0 ;;
        --force)           FORCE=1 ;;
        -h|--help)         sed -n '2,22p' "$0"; exit 0 ;;
        -*)                echo "unknown option: $a" >&2; exit 2 ;;
        *)                 WORKENV="$a" ;;
    esac
done
DOWNLOADS="$WORKENV/downloads"

# --- pinned versions --------------------------------------------------------
V_CMAKE=4.4.3
V_NINJA=1.13.2
V_SDL=3.4.16
SHA_SDL=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68

ARCH="$(uname -m)"
case "$ARCH" in
    x86_64)
        CMAKE_PKG="cmake-$V_CMAKE-linux-x86_64"
        SHA_CMAKE=d6c83076c575bc00b823522ac974bda66d0af05d6ddc30e739c12385cf32c6cc
        NINJA_ZIP=ninja-linux.zip
        SHA_NINJA=5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6 ;;
    aarch64|arm64)
        CMAKE_PKG="cmake-$V_CMAKE-linux-aarch64"
        SHA_CMAKE=2efc974dbd63b4444c0e8494b92f2e80c2d7e635b4b80eac2916985ddd8f72a6
        NINJA_ZIP=ninja-linux-aarch64.zip
        SHA_NINJA=fd2cacc8050a7f12a16a2e48f9e06fca5c14fc4c2bee2babb67b58be17a607fc ;;
    *)
        CMAKE_PKG=""; NINJA_ZIP="" ;;
esac

say()  { printf ' %s\n' "$*"; }
ok()   { printf '   [ok]   %s\n' "$*"; }
die()  { printf '\n ERROR: %s\n\n FAILED.\n' "$*" >&2; exit 1; }

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 && SUDO="sudo"
fi

echo
say "The Black Hole - Linux build toolchain"
say "--------------------------------------"
say "target folder : $WORKENV"
say "architecture  : $ARCH"
echo

# ===========================================================================
#  1. distribution packages
# ===========================================================================
install_packages() {
    local pm pkgs xss
    if command -v apt-get >/dev/null 2>&1; then
        pm=apt
        pkgs="build-essential cmake ninja-build pkg-config curl unzip ca-certificates
              libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
              libxfixes-dev libxss-dev libxtst-dev libgl-dev libegl-dev
              libwayland-dev libxkbcommon-dev wayland-protocols"
        xss="xscreensaver xscreensaver-gl"
    elif command -v dnf >/dev/null 2>&1; then
        pm=dnf
        pkgs="gcc make cmake ninja-build pkgconf-pkg-config curl unzip
              libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel
              libXfixes-devel libXScrnSaver-devel libXtst-devel
              mesa-libGL-devel mesa-libEGL-devel
              wayland-devel libxkbcommon-devel wayland-protocols-devel"
        xss="xscreensaver-base xscreensaver-gl-base"
    elif command -v pacman >/dev/null 2>&1; then
        pm=pacman
        pkgs="base-devel cmake ninja pkgconf curl unzip
              libx11 libxext libxrandr libxcursor libxi libxfixes libxss libxtst
              mesa libglvnd wayland libxkbcommon wayland-protocols"
        xss="xscreensaver"
    elif command -v zypper >/dev/null 2>&1; then
        pm=zypper
        pkgs="gcc make cmake ninja pkgconf-pkg-config curl unzip
              libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel
              libXfixes-devel libXss-devel libXtst-devel
              Mesa-libGL-devel Mesa-libEGL-devel
              wayland-devel libxkbcommon-devel wayland-protocols-devel"
        xss="xscreensaver"
    else
        say "[warn] no known package manager (apt, dnf, pacman, zypper)."
        say "       Install a C compiler, CMake 3.25+, Ninja, curl, unzip and the"
        say "       X11 / OpenGL development headers yourself, then run this again"
        say "       with --no-packages."
        return 1
    fi
    [ "$XSS" = 1 ] && pkgs="$pkgs $xss"
    # shellcheck disable=SC2086
    set -- $pkgs
    say "[1/3] packages via $pm (sudo may ask for your password)"
    case "$pm" in
        apt)    $SUDO apt-get update && $SUDO apt-get install -y "$@" ;;
        dnf)    $SUDO dnf install -y "$@" ;;
        pacman) $SUDO pacman -S --needed --noconfirm "$@" ;;
        zypper) $SUDO zypper --non-interactive install "$@" ;;
    esac
}

if [ "$PACKAGES" = 1 ]; then
    install_packages || die "package installation failed"
else
    say "[1/3] packages: skipped (--no-packages)"
fi

command -v curl  >/dev/null 2>&1 || die "curl is required"
command -v tar   >/dev/null 2>&1 || die "tar is required"
command -v cc    >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || die "no C compiler found"

# ===========================================================================
#  helpers
# ===========================================================================
sha256_of() { sha256sum "$1" | cut -d' ' -f1; }

# fetch  filename  url  sha256
fetch() {
    local f="$DOWNLOADS/$1"
    if [ -f "$f" ]; then
        if [ "$(sha256_of "$f")" = "$3" ]; then ok "$1 already downloaded"; return 0; fi
        say "  [warn] $1 does not match its pinned hash - downloading again"
        rm -f "$f"
    fi
    say "  [get]  $1"
    curl -L --fail --progress-bar -o "$f.part" "$2" || { rm -f "$f.part"; die "download failed: $2"; }
    local got
    got="$(sha256_of "$f.part")"
    if [ "$got" != "$3" ]; then
        rm -f "$f.part"
        die "SHA-256 mismatch for $1 - the file was NOT what we expected.
        expected $3
        actual   $got"
    fi
    mv "$f.part" "$f"
    ok "$1 verified"
}

# unpacked dir -> true if it is already there and we are not forcing
unpacked() {
    [ "$FORCE" = 0 ] && [ -d "$1" ] && { ok "$(basename "$1") already unpacked"; return 0; }
    return 1
}

version_ge() {   # version_ge 3.28.1 3.25  -> true
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

mkdir -p "$DOWNLOADS"

# ===========================================================================
#  2. SDL3 source
# ===========================================================================
echo
say "[2/3] SDL3 $V_SDL source (built statically into the screensaver)"
fetch "SDL3-$V_SDL.tar.gz" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$V_SDL/SDL3-$V_SDL.tar.gz" \
    "$SHA_SDL"
if ! unpacked "$WORKENV/SDL3-$V_SDL"; then
    rm -rf "$WORKENV/SDL3-$V_SDL"
    tar -xzf "$DOWNLOADS/SDL3-$V_SDL.tar.gz" -C "$WORKENV"
    ok "SDL3 unpacked"
fi

# ===========================================================================
#  3. CMake and Ninja, only when the system ones will not do
# ===========================================================================
echo
say "[3/3] CMake 3.25+ and Ninja"
SYS_CMAKE_OK=0
if command -v cmake >/dev/null 2>&1; then
    v="$(cmake --version | head -n1 | awk '{print $3}')"
    if version_ge "$v" 3.25; then SYS_CMAKE_OK=1; ok "system CMake $v"; else say "  system CMake $v is too old"; fi
fi
if [ "$SYS_CMAKE_OK" = 0 ]; then
    [ -n "$CMAKE_PKG" ] || die "no prebuilt CMake for $ARCH - install CMake 3.25 or newer yourself"
    fetch "$CMAKE_PKG.tar.gz" \
        "https://github.com/Kitware/CMake/releases/download/v$V_CMAKE/$CMAKE_PKG.tar.gz" \
        "$SHA_CMAKE"
    if ! unpacked "$WORKENV/$CMAKE_PKG"; then
        rm -rf "$WORKENV/$CMAKE_PKG"
        tar -xzf "$DOWNLOADS/$CMAKE_PKG.tar.gz" -C "$WORKENV"
        ok "CMake $V_CMAKE unpacked"
    fi
fi

if command -v ninja >/dev/null 2>&1 || command -v ninja-build >/dev/null 2>&1; then
    ok "system Ninja $( (command -v ninja >/dev/null && ninja --version) || ninja-build --version)"
else
    [ -n "$NINJA_ZIP" ] || die "no prebuilt Ninja for $ARCH - install Ninja yourself"
    command -v unzip >/dev/null 2>&1 || die "unzip is required to unpack Ninja"
    fetch "ninja-$V_NINJA-$NINJA_ZIP" \
        "https://github.com/ninja-build/ninja/releases/download/v$V_NINJA/$NINJA_ZIP" \
        "$SHA_NINJA"
    if ! unpacked "$WORKENV/ninja"; then
        mkdir -p "$WORKENV/ninja"
        unzip -o -q "$DOWNLOADS/ninja-$V_NINJA-$NINJA_ZIP" -d "$WORKENV/ninja"
        chmod +x "$WORKENV/ninja/ninja"
        ok "Ninja $V_NINJA unpacked"
    fi
fi

# ===========================================================================
#  verify
# ===========================================================================
echo
say "Verifying..."
PATH="$WORKENV/$CMAKE_PKG/bin:$WORKENV/ninja:$PATH"
say "  $( (cc --version || gcc --version) 2>/dev/null | head -n1)"
say "  $(cmake --version | head -n1)"
say "  ninja $( (command -v ninja >/dev/null && ninja --version) || ninja-build --version)"
[ -f "$WORKENV/SDL3-$V_SDL/CMakeLists.txt" ] || die "SDL3 source is incomplete"
say "  SDL3 $V_SDL source at $WORKENV/SDL3-$V_SDL"
if command -v xscreensaver >/dev/null 2>&1; then
    say "  $(xscreensaver --version 2>&1 | head -n1)"
else
    say "  [note] XScreenSaver is not installed: the screensaver will build and run"
    say "         in a window, but there is nothing to install it into."
fi

echo
say "Done. The archives are kept in $DOWNLOADS."
echo
say "Next: ./2-build-and-install-linux.sh"
if [ "$WORKENV" != "$HOME/workenv" ]; then
    say "      (with BH_WORKENV=$WORKENV, since the tools are not in the default place)"
fi
echo
