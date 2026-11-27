#!/bin/bash
# DW-303 Linux release tarball (x86_64).
#
#   target/installer/DW-303-<version>-linux-x86_64.tar.gz
#   target/installer/dw-303-linux-latest.tar.gz      (the name the website serves)
#
# The tarball holds VST3/, CLAP/, LV2/, Standalone/, install.sh, uninstall.sh,
# LICENSE.txt, THIRD-PARTY.txt and README.txt in DW-303-<version>-linux-x86_64/.
#
# Usage: installer/build-linux-tarball.sh [--skip-build]
#   --skip-build   package what is already staged under target/plugins
# Environment:
#   SOURCE_DATE_EPOCH  timestamp for every archive entry (default: last commit time)
set -euo pipefail
export COPYFILE_DISABLE=1   # no AppleDouble files when testing with bsdtar on macOS

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
skip_build=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) skip_build=1 ;;
    -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

# Version from [workspace.package] in the workspace Cargo.toml.
VERSION="$(awk '
  /^[[:space:]]*\[/ { section = $0; gsub(/[][[:space:]]/, "", section); next }
  section == "workspace.package" && /^[[:space:]]*version[[:space:]]*=/ {
    sub(/^[^"]*"/, ""); sub(/".*/, ""); print; exit
  }' "$ROOT/Cargo.toml")"
[[ -n "$VERSION" ]] || { echo "could not read [workspace.package] version from Cargo.toml" >&2; exit 1; }

# Cargo's target directory (bundle-plugins.py stages there), honouring CARGO_TARGET_DIR.
TARGET="${CARGO_TARGET_DIR:-$ROOT/target}"
[[ "$TARGET" = /* ]] || TARGET="$ROOT/$TARGET"
ARCH="$(uname -m)"
PLUGINS="$TARGET/plugins"
OUT="$TARGET/installer"
NAME="DW-303-$VERSION-linux-$ARCH"
WORK="$OUT/work-linux"
STAGE="$WORK/$NAME"
TARBALL="$OUT/$NAME.tar.gz"
LATEST="$OUT/dw-303-linux-latest.tar.gz"

if (( ! skip_build )); then
  python3 "$ROOT/scripts/bundle-plugins.py"
fi

# Licence fallback: the repository LICENSE is the GPL text the bundles ship.
[[ -f "$PLUGINS/LICENSE.txt" || ! -f "$ROOT/LICENSE" ]] || cp "$ROOT/LICENSE" "$PLUGINS/LICENSE.txt"

required=(
  LICENSE.txt
  THIRD-PARTY.txt
  "VST3/DW-303.vst3/Contents/$ARCH-linux/DW-303.so"
  CLAP/DW-303.clap
  LV2/dw-303.lv2/manifest.ttl
  LV2/dw-303.lv2/dw-303.ttl
  LV2/dw-303.lv2/dw-303.so
  LV2/dw-303.lv2/LICENSE.txt
  Standalone/DW-303
)
missing=()
for f in "${required[@]}"; do [[ -f "$PLUGINS/$f" ]] || missing+=("$f"); done
if (( ${#missing[@]} )); then
  echo "missing staged files under $PLUGINS (run python3 scripts/bundle-plugins.py):" >&2
  printf '  %s\n' "${missing[@]}" >&2
  exit 1
fi
[[ -x "$PLUGINS/Standalone/DW-303" ]] || { echo "$PLUGINS/Standalone/DW-303 is not executable" >&2; exit 1; }

rm -rf "$WORK" "$TARBALL"
mkdir -p "$STAGE/VST3" "$STAGE/CLAP" "$STAGE/LV2" "$STAGE/Standalone"
cp -R "$PLUGINS/VST3/DW-303.vst3" "$STAGE/VST3/"
cp "$PLUGINS/CLAP/DW-303.clap" "$STAGE/CLAP/"
cp -R "$PLUGINS/LV2/dw-303.lv2" "$STAGE/LV2/"
cp "$PLUGINS/Standalone/DW-303" "$STAGE/Standalone/"
[[ ! -f "$PLUGINS/Standalone/dw-303.png" ]] || cp "$PLUGINS/Standalone/dw-303.png" "$STAGE/Standalone/"
cp "$ROOT/installer/linux/install.sh" "$ROOT/installer/linux/uninstall.sh" "$STAGE/"
cp "$PLUGINS/LICENSE.txt" "$PLUGINS/THIRD-PARTY.txt" "$STAGE/"

cat >"$STAGE/README.txt" <<EOF
DW-303 $VERSION for Linux ($ARCH)
==================================

A circuit-modelled bass line synthesizer and step sequencer: VST3, CLAP and
LV2 plug-ins plus a standalone app. https://djwaudio.co/#dw-303

Requirements
  - $ARCH Linux with glibc
  - GTK 3, WebKitGTK 4.1 (2.40 or newer), json-glib and ALSA
      Debian/Ubuntu: sudo apt install libwebkit2gtk-4.1-0 libgtk-3-0 libjson-glib-1.0-0 libasound2
      Fedora:        sudo dnf install webkit2gtk4.1 gtk3 json-glib alsa-lib
      Arch:          sudo pacman -S webkit2gtk-4.1 gtk3 json-glib alsa-lib
      openSUSE:      sudo zypper install libwebkit2gtk-4_1-0 libgtk-3-0 libjson-glib-1_0-0 libasound2
  - X11 or XWayland for the panel editor
  The panel needs GTK 3 and WebKitGTK; without them the plug-ins still load and
  play, with the host's generic controls. Flatpak hosts (Bitwig Studio's
  Flatpak, for one) cannot see these system libraries, so they show generic
  controls; use the host's native package for the panel.

Install
  ./install.sh                 for your user (~/.vst3, ~/.clap, ~/.lv2, ~/.local)
  sudo ./install.sh --system   for everyone (/usr/local)
  ./install.sh --dry-run       show what would be installed
  install.sh checks for the libraries above and prints the command to install
  any that are missing.

Uninstall
  ~/.local/lib/dw-303/uninstall.sh
  sudo /usr/local/lib/dw-303/uninstall.sh      (system-wide install)

Notes
  - The panel editor is a WebKitGTK page. Hosts that load a different GTK
    version (for example Ardour, which uses GTK 2) cannot show it; they show
    their generic parameter controls instead. Sound and state are unaffected.
  - Hosts that scan only the CLAP standard paths need CLAP_PATH=/usr/local/lib/clap
    to find a --system install.

DW-303 is free software under the GNU General Public License v3.0 or later
(LICENSE.txt); third-party notices are in THIRD-PARTY.txt.
Source: https://github.com/mo0kid/DW-303
TB-303 and Roland are trademarks of Roland Corporation. DW-303 is an
independent project and is not affiliated with or endorsed by Roland.
EOF

# Normalised permissions: directories, shared objects and executables 755, everything else 644.
find "$STAGE" -type d -exec chmod 755 {} +
find "$STAGE" -type f -exec chmod 644 {} +
find "$STAGE" -type f \( -name '*.so' -o -name '*.clap' \) -exec chmod 755 {} +
chmod 755 "$STAGE/install.sh" "$STAGE/uninstall.sh" "$STAGE/Standalone/DW-303"

EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null || date +%s)}"

TAR=tar
if ! tar --version 2>/dev/null | grep -q 'GNU tar'; then
  if command -v gtar >/dev/null 2>&1; then TAR=gtar; else TAR=""; fi
fi
mkdir -p "$OUT"
if [[ -n "$TAR" ]]; then
  "$TAR" --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$EPOCH" \
    --format=gnu -C "$WORK" -cf - "$NAME" | gzip -n -9 >"$TARBALL"
else
  # bsdtar (macOS): sorted entries and root ownership, but not GNU's fixed mtimes.
  echo "warning: GNU tar not found; the archive will not be byte-reproducible." >&2
  (cd "$WORK" && find "$NAME" -print | LC_ALL=C sort |
    tar --uid 0 --gid 0 --uname root --gname root -n -cf - -T -) | gzip -n -9 >"$TARBALL"
fi
cp "$TARBALL" "$LATEST"
rm -rf "$WORK"

echo "Tarball: $TARBALL"
echo "Latest:  $LATEST"
echo "Upload it to the site as downloads/dw-303-linux-latest.tar.gz."
