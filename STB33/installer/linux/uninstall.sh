#!/bin/sh
# DW-303 Linux uninstaller. Removes exactly what install.sh recorded in
# <lib dir>/install-manifest.txt, then any directories install.sh created
# that are now empty.
#
#   ~/.local/lib/dw-303/uninstall.sh            per-user install
#   sudo /usr/local/lib/dw-303/uninstall.sh     --system install
#   ./uninstall.sh [--system | --prefix DIR]    from the unpacked tarball
#   add --dry-run to see what would be removed
set -eu

usage() {
    cat <<EOF
Usage: $0 [--system | --prefix DIR] [--dry-run]

Run from the installed lib directory (where install-manifest.txt lives), or
pass the same --system/--prefix option given to install.sh.
EOF
}

mode="" prefix="" dry=0
while [ $# -gt 0 ]; do
    case "$1" in
        --user) mode=user ;;
        --system) mode=system ;;
        --prefix) [ $# -ge 2 ] || { echo "--prefix needs a directory" >&2; exit 2; }
                  mode=system prefix=$2; shift ;;
        --prefix=*) mode=system prefix=${1#--prefix=} ;;
        --dry-run|-n) dry=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

HERE=$(cd "$(dirname "$0")" && pwd)
if [ -z "$mode" ] && [ -f "$HERE/install-manifest.txt" ]; then
    LIB_DIR=$HERE
elif [ "${mode:-user}" = system ]; then
    [ -n "$prefix" ] || prefix=/usr/local
    case "$prefix" in /*) ;; *) prefix=$(pwd)/$prefix ;; esac
    LIB_DIR=${prefix%/}/lib/dw-303
else
    [ -n "${HOME:-}" ] || { echo "HOME is not set" >&2; exit 1; }
    LIB_DIR=$HOME/.local/lib/dw-303
fi
MANIFEST=$LIB_DIR/install-manifest.txt
if [ ! -f "$MANIFEST" ]; then
    echo "no DW-303 install found ($MANIFEST is missing)." >&2
    [ -z "$mode" ] && echo "For a system-wide install run: sudo $0 --system" >&2
    exit 1
fi
if [ ! -w "$LIB_DIR" ] && [ "$dry" -eq 0 ]; then
    echo "$LIB_DIR is not writable; run as root (sudo $0 ${mode:+--$mode})." >&2
    exit 1
fi

run() {
    if [ "$dry" -eq 1 ]; then echo "+ $*"; else "$@"; fi
}

# Only remove names install.sh creates; anything else in a manifest is ignored.
allowed() {
    case "$1" in /*) ;; *) return 1 ;; esac
    case "$1" in */../*|*/./*|*/..|*/.) return 1 ;; esac
    case "$(basename "$1")" in
        DW-303.vst3|DW-303.clap|dw-303.lv2|dw-303|dw-303.desktop) return 0 ;;
        DW-303|dw-303.png|LICENSE.txt|THIRD-PARTY.txt|README.txt|uninstall.sh|install-manifest.txt)
            [ "$(dirname "$1")" = "$LIB_DIR" ] ;;
        *) return 1 ;;
    esac
}

[ "$dry" -eq 1 ] && echo "Dry run: nothing will be removed."
echo "Removing DW-303:"
# Read everything first: the manifest itself is one of the entries.
entries=$(cat "$MANIFEST")
dirs=""
apps_dir=""
oldifs=$IFS
set -f   # no globbing while splitting the manifest into lines
IFS='
'
for line in $entries; do
    kind=${line%% *} path=${line#* }
    case "$kind" in
        path)
            if ! allowed "$path"; then echo "  skipping unexpected entry: $path" >&2; continue; fi
            if [ -e "$path" ] || [ -L "$path" ]; then
                run rm -rf "$path"
                echo "  $path"
            fi
            case "$path" in */dw-303.desktop) apps_dir=$(dirname "$path") ;; esac ;;
        dir)
            case "$path" in /*) dirs="$dirs$path
" ;; esac ;;
    esac
done

# Directories install.sh created, deepest first, only if now empty.
for dir in $(printf '%s' "$dirs" | awk '{ print length($0) "\t" $0 }' | sort -rn | cut -f2-); do
    if [ -d "$dir" ] && [ -z "$(ls -A "$dir" 2>/dev/null)" ]; then
        run rmdir "$dir"
        echo "  $dir/"
    elif [ "$dry" -eq 1 ] && [ -d "$dir" ]; then
        echo "+ rmdir $dir (if empty)"
    fi
done
IFS=$oldifs
set +f

if [ -n "$apps_dir" ] && [ -d "$apps_dir" ] && command -v update-desktop-database >/dev/null 2>&1; then
    run update-desktop-database -q "$apps_dir" 2>/dev/null || true
fi
echo "Done."
