#!/usr/bin/env bash
# DW-303 Windows installer from Linux: Inno Setup under Wine, from the bundles
# staged by `python3 scripts/bundle-plugins.py --cross-windows` (target/plugins-windows).
# installer/windows/cross.Dockerfile provides MinGW-w64, Wine and Inno Setup.
#
#   installer/build-windows-installer.sh [--skip-build]
#
# Writes target/installer/DW-303-<version>-windows-x64.exe and
# target/installer/dw-303-win-latest.exe (the name the website serves).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
skip_build=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) skip_build=1 ;;
    *) echo "usage: $0 [--skip-build]" >&2; exit 2 ;;
  esac
done

VERSION="$(awk '
  /^[[:space:]]*\[/ { section = $0; gsub(/[][[:space:]]/, "", section); next }
  section == "workspace.package" && /^[[:space:]]*version[[:space:]]*=/ {
    sub(/^[^"]*"/, ""); sub(/".*/, ""); print; exit
  }' "$ROOT/Cargo.toml")"
[[ -n "$VERSION" ]] || { echo "could not read [workspace.package] version" >&2; exit 1; }
IFS=. read -r major minor patch <<<"${VERSION%%[-+]*}"
NUMERIC="${major:-0}.${minor:-0}.${patch:-0}.0"

TARGET="${CARGO_TARGET_DIR:-$ROOT/target}"
[[ "$TARGET" = /* ]] || TARGET="$ROOT/$TARGET"
STAGE="$TARGET/plugins-windows"
OUT="$TARGET/installer"

(( skip_build )) || python3 "$ROOT/scripts/bundle-plugins.py" --cross-windows

required=(LICENSE.txt THIRD-PARTY.txt VST3/DW-303.vst3/Contents/x86_64-win/DW-303.vst3
  VST3/DW-303.vst3/Contents/x86_64-win/WebView2Loader.dll CLAP/DW-303.clap CLAP/WebView2Loader.dll
  Standalone/DW-303.exe Standalone/WebView2Loader.dll)
for f in "${required[@]}"; do
  [[ -f "$STAGE/$f" ]] || { echo "missing $STAGE/$f" >&2; exit 1; }
done

command -v wine >/dev/null || { echo "wine is required" >&2; exit 1; }
ISCC=""
for candidate in "${ISCC_EXE:-}" "${WINEPREFIX:-$HOME/.wine}/drive_c/InnoSetup/ISCC.exe" \
  "${WINEPREFIX:-$HOME/.wine}/drive_c/Program Files (x86)/Inno Setup 6/ISCC.exe"; do
  [[ -n "$candidate" && -f "$candidate" ]] && { ISCC="$candidate"; break; }
done
[[ -n "$ISCC" ]] || { echo "ISCC.exe not found in the Wine prefix (set ISCC_EXE)" >&2; exit 1; }

mkdir -p "$OUT"
win() { winepath -w "$1" 2>/dev/null; }
wine "$ISCC" /Q "/DAppVersion=$VERSION" "/DAppVersionNumeric=$NUMERIC" \
  "/DStageDir=$(win "$STAGE")" "/DOutputDir=$(win "$OUT")" "$(win "$ROOT/installer/dw-303.iss")"
SETUP="$OUT/DW-303-$VERSION-windows-x64.exe"
[[ -f "$SETUP" ]] || { echo "Inno Setup did not produce $SETUP" >&2; exit 1; }
cp "$SETUP" "$OUT/dw-303-win-latest.exe"
echo "Installer: $SETUP"
echo "Latest:    $OUT/dw-303-win-latest.exe"
echo "Upload it to the site as downloads/dw-303-win-latest.exe."
