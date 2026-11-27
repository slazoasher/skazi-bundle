#!/bin/sh
# DW-303 Linux installer. Run from the unpacked tarball:
#
#   ./install.sh               per user: ~/.vst3, ~/.clap, ~/.lv2,
#                              app in ~/.local/lib/dw-303, ~/.local/bin/dw-303
#   sudo ./install.sh --system everyone: /usr/local/lib/{vst3,clap,lv2},
#                              /usr/local/lib/dw-303, /usr/local/bin
#   ./install.sh --prefix DIR  system layout under DIR (DIR/lib/vst3, DIR/bin, ...)
#   ./install.sh --dry-run     print what would happen, change nothing
#
# Every installed path is recorded in <lib dir>/install-manifest.txt, and
# uninstall.sh (also copied there) removes exactly those paths.
set -eu

PRODUCT="DW-303"
SITE="https://djwaudio.co/#dw-303"
HERE=$(cd "$(dirname "$0")" && pwd)

usage() {
    cat <<EOF
Usage: $0 [--system | --prefix DIR] [--dry-run] [--force]

  (default)      install for the current user (~/.vst3, ~/.clap, ~/.lv2, ~/.local)
  --system       install for all users under /usr/local (run as root, e.g. with sudo)
  --prefix DIR   install under DIR using the system layout (DIR/lib/vst3, DIR/bin, ...)
  --dry-run      show what would be done without changing anything
  --force        install even if the package architecture does not match
EOF
}

mode=user prefix="" dry=0 force=0
while [ $# -gt 0 ]; do
    case "$1" in
        --user) mode=user ;;
        --system) mode=system ;;
        --prefix) [ $# -ge 2 ] || { echo "--prefix needs a directory" >&2; exit 2; }
                  mode=system prefix=$2; shift ;;
        --prefix=*) mode=system prefix=${1#--prefix=} ;;
        --dry-run|-n) dry=1 ;;
        --force) force=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

if [ "$mode" = system ]; then
    if [ -z "$prefix" ]; then
        prefix=/usr/local
        if [ "$(id -u)" -ne 0 ] && [ "$dry" -eq 0 ]; then
            echo "--system installs into /usr/local and needs root: sudo $0 --system" >&2
            exit 1
        fi
    fi
    case "$prefix" in /*) ;; *) prefix=$(pwd)/$prefix ;; esac
    prefix=${prefix%/}
    VST3_DIR=$prefix/lib/vst3
    CLAP_DIR=$prefix/lib/clap
    LV2_DIR=$prefix/lib/lv2
    LIB_DIR=$prefix/lib/dw-303
    BIN_DIR=$prefix/bin
    APPS_DIR=$prefix/share/applications
else
    [ -n "${HOME:-}" ] || { echo "HOME is not set" >&2; exit 1; }
    if [ "$(id -u)" -eq 0 ]; then
        echo "note: running as root without --system installs into root's home ($HOME)." >&2
    fi
    VST3_DIR=$HOME/.vst3
    CLAP_DIR=$HOME/.clap
    LV2_DIR=$HOME/.lv2
    LIB_DIR=$HOME/.local/lib/dw-303
    BIN_DIR=$HOME/.local/bin
    APPS_DIR=${XDG_DATA_HOME:-$HOME/.local/share}/applications
fi
MANIFEST=$LIB_DIR/install-manifest.txt

# --- platform ---------------------------------------------------------------
# The VST3 bundle names the architecture it was built for.
if [ "$(uname -s)" != Linux ] || [ ! -d "$HERE/VST3/DW-303.vst3/Contents/$(uname -m)-linux" ]; then
    if [ "$force" -eq 0 ]; then
        echo "This $PRODUCT package does not match this machine ($(uname -s) $(uname -m))." >&2
        echo "Use --force to install anyway." >&2
        exit 1
    fi
    echo "warning: package does not match $(uname -s) $(uname -m); installing anyway (--force)." >&2
fi

# --- what is in this package -----------------------------------------------
VST3_SRC=$HERE/VST3/DW-303.vst3
CLAP_SRC=$HERE/CLAP/DW-303.clap
LV2_SRC=$HERE/LV2/dw-303.lv2
APP_SRC=$HERE/Standalone/DW-303
ICON_SRC=$HERE/Standalone/dw-303.png
found=0
for item in "$VST3_SRC" "$CLAP_SRC" "$LV2_SRC" "$APP_SRC"; do
    if [ -e "$item" ]; then found=1; else echo "warning: $item is missing; skipping it." >&2; fi
done
[ "$found" -eq 1 ] || { echo "nothing to install: run install.sh from the unpacked $PRODUCT folder" >&2; exit 1; }

# --- runtime libraries ------------------------------------------------------
have_lib() {
    if command -v ldconfig >/dev/null 2>&1 || [ -x /sbin/ldconfig ]; then
        ldc=$(command -v ldconfig 2>/dev/null || echo /sbin/ldconfig)
        if "$ldc" -p 2>/dev/null | grep -q "[[:space:]]$1[[:space:]]"; then return 0; fi
    fi
    for d in /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /lib64 /usr/lib64 /lib /usr/lib \
             /usr/local/lib /usr/local/lib64; do
        [ -e "$d/$1" ] && return 0
    done
    return 1
}

distro_family() {
    ids=""
    if [ -r /etc/os-release ]; then
        # shellcheck source=/dev/null
        ids=$(. /etc/os-release && echo "${ID:-} ${ID_LIKE:-}")
    fi
    for id in $ids; do
        case "$id" in
            debian|ubuntu|linuxmint|pop|elementary|raspbian) echo apt; return ;;
            fedora|rhel|centos|rocky|almalinux|nobara) echo dnf; return ;;
            arch|manjaro|endeavouros|cachyos) echo pacman; return ;;
            opensuse*|suse|sles) echo zypper; return ;;
        esac
    done
    for tool in apt-get dnf pacman zypper; do
        if command -v "$tool" >/dev/null 2>&1; then
            [ "$tool" = apt-get ] && tool=apt
            echo "$tool"; return
        fi
    done
    echo unknown
}

# package name for a library on a distribution family
package_for() {
    case "$1:$2" in
        apt:libwebkit2gtk-4.1.so.0) echo libwebkit2gtk-4.1-0 ;;
        apt:libgtk-3.so.0) echo libgtk-3-0 ;;
        apt:libjson-glib-1.0.so.0) echo libjson-glib-1.0-0 ;;
        apt:libasound.so.2) echo libasound2 ;;
        dnf:libwebkit2gtk-4.1.so.0) echo webkit2gtk4.1 ;;
        dnf:libgtk-3.so.0) echo gtk3 ;;
        dnf:libjson-glib-1.0.so.0) echo json-glib ;;
        dnf:libasound.so.2) echo alsa-lib ;;
        pacman:libwebkit2gtk-4.1.so.0) echo webkit2gtk-4.1 ;;
        pacman:libgtk-3.so.0) echo gtk3 ;;
        pacman:libjson-glib-1.0.so.0) echo json-glib ;;
        pacman:libasound.so.2) echo alsa-lib ;;
        zypper:libwebkit2gtk-4.1.so.0) echo libwebkit2gtk-4_1-0 ;;
        zypper:libgtk-3.so.0) echo libgtk-3-0 ;;
        zypper:libjson-glib-1.0.so.0) echo libjson-glib-1_0-0 ;;
        zypper:libasound.so.2) echo libasound2 ;;
    esac
}

missing=""
for lib in libwebkit2gtk-4.1.so.0 libgtk-3.so.0 libjson-glib-1.0.so.0 libasound.so.2; do
    have_lib "$lib" || missing="$missing $lib"
done
if [ -n "$missing" ]; then
    family=$(distro_family)
    echo "warning: these runtime libraries were not found:$missing" >&2
    echo "  $PRODUCT needs them for its panel editor (WebKitGTK 4.1 >= 2.40, GTK 3) and audio (ALSA)." >&2
    packages=""
    for lib in $missing; do packages="$packages $(package_for "$family" "$lib")"; done
    case "$family" in
        apt) echo "  Install with: sudo apt install$packages" >&2
             echo "  (Ubuntu 24.04+/Debian 13+ may name some of these with a t64 suffix, e.g. libasound2t64.)" >&2 ;;
        dnf) echo "  Install with: sudo dnf install$packages" >&2 ;;
        pacman) echo "  Install with: sudo pacman -S --needed$packages" >&2 ;;
        zypper) echo "  Install with: sudo zypper install$packages" >&2 ;;
        *) echo "  Install WebKitGTK 4.1, GTK 3, json-glib and ALSA with your package manager." >&2 ;;
    esac
    echo "  Continuing; install them before using $PRODUCT." >&2
fi

# --- helpers ----------------------------------------------------------------
run() {
    if [ "$dry" -eq 1 ]; then echo "+ $*"; else "$@"; fi
}

# One "KIND PATH" line per entry, starting with a newline sentinel so the
# duplicate check below can match whole lines.
NEW_MANIFEST="
"
record() {  # record KIND PATH   (KIND: path = remove on uninstall, dir = rmdir if empty)
    case "$NEW_MANIFEST" in *"
$1 $2
"*) return 0 ;; esac
    NEW_MANIFEST="$NEW_MANIFEST$1 $2
"
}

# Make a directory, recording every level this run creates so uninstall can
# remove them again once they are empty.
ensure_dir() {
    [ -d "$1" ] && return 0
    case "$NEW_MANIFEST" in *"
dir $1
"*) return 0 ;; esac   # already created (or, in a dry run, would be)
    ensure_dir "$(dirname "$1")"
    run mkdir "$1"
    record dir "$1"
}

# Copy SRC to DEST through a temporary sibling, then swap it into place.
install_item() {
    src=$1 dest=$2
    parent=$(dirname "$dest")
    ensure_dir "$parent"
    tmp="$parent/.$(basename "$dest").new.$$"
    run rm -rf "$tmp"
    run cp -R "$src" "$tmp"
    run chmod -R u+rwX,go+rX,go-w "$tmp"
    run rm -rf "$dest"
    run mv "$tmp" "$dest"
    record path "$dest"
    echo "  $dest"
}

desktop_entry() {
    cat <<EOF
[Desktop Entry]
Type=Application
Name=$PRODUCT
GenericName=Bass Line Synthesizer
Comment=Circuit-modelled bass line synthesizer and step sequencer
Exec="$LIB_DIR/DW-303"
TryExec=$LIB_DIR/DW-303
EOF
    [ -e "$ICON_SRC" ] && echo "Icon=$LIB_DIR/dw-303.png"
    cat <<EOF
Terminal=false
Categories=AudioVideo;Audio;Midi;
Keywords=synth;bass;303;sequencer;
StartupWMClass=DW-303
EOF
}

# --- install ----------------------------------------------------------------
[ "$dry" -eq 1 ] && echo "Dry run: nothing will be changed."
echo "Installing $PRODUCT into:"

# Keep directories an earlier install created, so uninstall still cleans them.
if [ -f "$MANIFEST" ]; then
    while IFS=' ' read -r kind path; do
        [ "$kind" = dir ] && [ -d "$path" ] && record dir "$path"
    done <"$MANIFEST"
fi

ensure_dir "$LIB_DIR"
[ -e "$VST3_SRC" ] && install_item "$VST3_SRC" "$VST3_DIR/DW-303.vst3"
[ -e "$CLAP_SRC" ] && install_item "$CLAP_SRC" "$CLAP_DIR/DW-303.clap"
[ -e "$LV2_SRC" ] && install_item "$LV2_SRC" "$LV2_DIR/dw-303.lv2"

if [ -e "$APP_SRC" ]; then
    install_item "$APP_SRC" "$LIB_DIR/DW-303"
    run chmod 755 "$LIB_DIR/DW-303"
    [ -e "$ICON_SRC" ] && install_item "$ICON_SRC" "$LIB_DIR/dw-303.png"

    link=$BIN_DIR/dw-303
    ensure_dir "$BIN_DIR"
    if [ -e "$link" ] && [ ! -L "$link" ]; then
        echo "warning: $link exists and is not a symlink; leaving it alone." >&2
    else
        run rm -f "$link"
        run ln -s "$LIB_DIR/DW-303" "$link"
        record path "$link"
        echo "  $link -> $LIB_DIR/DW-303"
    fi

    desktop=$APPS_DIR/dw-303.desktop
    ensure_dir "$APPS_DIR"
    if [ "$dry" -eq 1 ]; then
        echo "+ write $desktop"
    else
        desktop_entry >"$desktop.new.$$"
        chmod 644 "$desktop.new.$$"
        mv "$desktop.new.$$" "$desktop"
    fi
    record path "$desktop"
    echo "  $desktop"
    if command -v update-desktop-database >/dev/null 2>&1; then
        run update-desktop-database -q "$APPS_DIR" 2>/dev/null || true
    fi
fi

for f in LICENSE.txt THIRD-PARTY.txt README.txt; do
    [ -e "$HERE/$f" ] && install_item "$HERE/$f" "$LIB_DIR/$f" >/dev/null
done
[ -e "$HERE/uninstall.sh" ] && install_item "$HERE/uninstall.sh" "$LIB_DIR/uninstall.sh" >/dev/null && run chmod 755 "$LIB_DIR/uninstall.sh"

# Anything an earlier version installed that this one did not replace stays
# listed, so uninstall.sh still removes it.
if [ -f "$MANIFEST" ]; then
    while IFS=' ' read -r kind path; do
        if [ "$kind" = path ] && { [ -e "$path" ] || [ -L "$path" ]; }; then record path "$path"; fi
    done <"$MANIFEST"
fi
record path "$MANIFEST"
if [ "$dry" -eq 1 ]; then
    echo "+ write $MANIFEST:"
    printf '%s' "${NEW_MANIFEST#?}" | sed 's/^/    /'
else
    printf '%s' "${NEW_MANIFEST#?}" >"$MANIFEST.new.$$"
    mv "$MANIFEST.new.$$" "$MANIFEST"
fi

echo "Done. Uninstall with: $LIB_DIR/uninstall.sh$( [ "$mode" = system ] && [ "$prefix" = /usr/local ] && echo ' (as root)')"
if [ "$mode" = system ] && [ "$prefix" = /usr/local ] && [ -e "$CLAP_SRC" ]; then
    echo "Note: hosts that only scan the CLAP standard paths (/usr/lib/clap, ~/.clap) need"
    echo "      CLAP_PATH=/usr/local/lib/clap to find the system-wide CLAP plug-in."
fi
if [ "$mode" = user ] && [ -e "$APP_SRC" ]; then
    case ":${PATH:-}:" in *":$BIN_DIR:"*) ;; *) echo "Note: $BIN_DIR is not on your PATH; start the app from your desktop menu or $LIB_DIR/DW-303." ;; esac
fi
echo "Panel editor: needs WebKitGTK 4.1. Hosts that load a different GTK (for example Ardour) show generic controls."
echo "$SITE"
