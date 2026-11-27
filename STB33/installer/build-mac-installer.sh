#!/bin/bash
# DW-303 macOS installer: one .pkg with a choice per format, inside a DMG.
#
#   AU         → /Library/Audio/Plug-Ins/Components/
#   VST3       → /Library/Audio/Plug-Ins/VST3/
#   CLAP       → /Library/Audio/Plug-Ins/CLAP/
#   LV2        → /Library/Audio/Plug-Ins/LV2/
#   AAX        → /Library/Application Support/Avid/Audio/Plug-Ins/
#   Standalone → /Applications/
#
# Usage: installer/build-mac-installer.sh
# Builds universal bundles with scripts/bundle-plugins.py unless SKIP_BUILD=1.
#
# Environment (no credentials are stored in this repository):
#   DW303_SIGN_IDENTITY       "Developer ID Application: …" (default: first found)
#   DW303_INSTALLER_IDENTITY  "Developer ID Installer: …"   (default: first found)
#   DW303_NOTARY_PROFILE      notarytool keychain profile; notarizes pkg and DMG
#   DW303_WRAPTOOL, DW303_PACE_ACCOUNT, DW303_PACE_WCGUID
#                             PACE signing for AAX (see scripts/sign-macos.sh)
#   SKIP_SIGN=1               unsigned local test build (AAX will not load in Pro Tools)
#   SKIP_AAX=1                leave AAX out (for example, before PACE is set up)
#   SDK_ROOT                  external SDKs (default: target/sdks)
#
# Settings may also live in ~/.config/djwaudio/dw303-signing.env (or the file
# named by DW303_SIGNING_ENV), which is sourced first; keep it out of the repo.
set -euo pipefail
export COPYFILE_DISABLE=1
signing_env="${DW303_SIGNING_ENV:-$HOME/.config/djwaudio/dw303-signing.env}"
# shellcheck source=/dev/null
[[ -f "$signing_env" ]] && source "$signing_env"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(sed -n 's/^version = "\(.*\)"/\1/p' "$ROOT/Cargo.toml" | head -1)"
IDENTIFIER="co.djwaudio.dw303"
PLUGINS="$ROOT/target/plugins"
WORK="$ROOT/target/installer/work"
OUT="$ROOT/target/installer"
PKG="$OUT/DW-303-$VERSION.pkg"
DMG="$OUT/DW-303 $VERSION.dmg"
truthy() { [[ "${1:-}" =~ ^(1|true|yes|on)$ ]]; }

rm -rf "$WORK" "$PKG" "$DMG"
mkdir -p "$WORK" "$OUT"

if ! truthy "${SKIP_BUILD:-}"; then
  python3 "$ROOT/scripts/bundle-plugins.py" --universal --check --sdk-root "${SDK_ROOT:-$ROOT/target/sdks}"
fi

# kind|bundle|install location|title
COMPONENTS=(
  "au|AU/DW-303.component|/Library/Audio/Plug-Ins/Components|Audio Unit"
  "vst3|VST3/DW-303.vst3|/Library/Audio/Plug-Ins/VST3|VST3"
  "clap|CLAP/DW-303.clap|/Library/Audio/Plug-Ins/CLAP|CLAP"
  "lv2|LV2/dw-303.lv2|/Library/Audio/Plug-Ins/LV2|LV2"
  "aax|AAX/DW-303.aaxplugin|/Library/Application Support/Avid/Audio/Plug-Ins|AAX (Pro Tools)"
  "app|Standalone/DW-303.app|/Applications|Standalone app"
)

if ! truthy "${SKIP_SIGN:-}"; then
  INSTALLER_IDENTITY="${DW303_INSTALLER_IDENTITY:-$(security find-identity -v |
    sed -n 's/.*"\(Developer ID Installer:[^"]*\)".*/\1/p' | head -1)}"
  [[ -n "$INSTALLER_IDENTITY" ]] || { echo "no Developer ID Installer identity found" >&2; exit 1; }
fi

choices="" refs="" outline=""
for entry in "${COMPONENTS[@]}"; do
  IFS="|" read -r kind bundle location title <<<"$entry"
  if [[ "$kind" == aax ]] && truthy "${SKIP_AAX:-}"; then continue; fi
  source="$PLUGINS/$bundle"
  [[ -e "$source" ]] || { echo "missing $source (run scripts/bundle-plugins.py)" >&2; exit 1; }
  name="$(basename "$bundle")"
  root="$WORK/$kind/root"
  mkdir -p "$root$location"
  /usr/bin/ditto --noextattr --norsrc "$source" "$root$location/$name"
  if ! truthy "${SKIP_SIGN:-}"; then
    if [[ "$kind" == lv2 ]]; then
      # LV2 bundles are plain directories; sign the binary inside.
      codesign --force --timestamp --options runtime --sign "${DW303_SIGN_IDENTITY:-Developer ID Application}" "$root$location/$name/dw-303.dylib"
    else
      "$ROOT/scripts/sign-macos.sh" "$root$location/$name"
    fi
  fi
  # Replace any earlier copy, system-wide or per user, so hosts see one version.
  scripts="$WORK/$kind/scripts"
  mkdir -p "$scripts"
  cat >"$scripts/preinstall" <<PRE
#!/bin/bash
rm -rf "$location/$name"
for home in /Users/*; do rm -rf "\$home$location/$name"; done
exit 0
PRE
  if [[ "$kind" == au ]]; then
    printf '#!/bin/bash\nkillall -9 AudioComponentRegistrar >/dev/null 2>&1 || true\nexit 0\n' >"$scripts/postinstall"
  fi
  chmod 755 "$scripts"/*
  /usr/bin/xattr -cr "$root"
  args=(--root "$root" --identifier "$IDENTIFIER.$kind" --version "$VERSION" --install-location / --scripts "$scripts")
  truthy "${SKIP_SIGN:-}" || args+=(--sign "$INSTALLER_IDENTITY" --timestamp)
  pkgbuild "${args[@]}" "$WORK/dw303-$kind.pkg"
  outline+="        <line choice=\"$kind\"/>"$'\n'
  choices+="    <choice id=\"$kind\" title=\"$title\" description=\"Install the DW-303 $title.\"><pkg-ref id=\"$IDENTIFIER.$kind\"/></choice>"$'\n'
  refs+="    <pkg-ref id=\"$IDENTIFIER.$kind\" version=\"$VERSION\">dw303-$kind.pkg</pkg-ref>"$'\n'
done

cat >"$WORK/welcome.html" <<HTML
<!DOCTYPE html>
<html><head><meta charset="utf-8"></head>
<body style="font-family:-apple-system,Helvetica Neue,sans-serif;padding:20px">
<h1>DW-303</h1>
<p>A circuit-modelled bass line synthesizer and step sequencer, built on RAVET.</p>
<p>Choose <strong>Customize</strong> to install only the formats you use.</p>
<p>DW-303 is free software under the GNU General Public License v3.0 or later.
Source: <a href="https://github.com/mo0kid/DW-303">github.com/mo0kid/DW-303</a></p>
<p style="color:#666;font-size:12px">TB-303 and Roland are trademarks of Roland Corporation. DW-303 is an
independent project and is not affiliated with or endorsed by Roland.</p>
<p style="color:#666;font-size:12px">Version $VERSION</p>
</body></html>
HTML
cp "$ROOT/LICENSE" "$WORK/LICENSE.txt"
cat >"$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>DW-303 $VERSION</title>
    <organization>$IDENTIFIER</organization>
    <welcome file="welcome.html"/>
    <license file="LICENSE.txt"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
$outline    </choices-outline>
$choices$refs</installer-gui-script>
XML

args=(--distribution "$WORK/distribution.xml" --resources "$WORK" --package-path "$WORK")
truthy "${SKIP_SIGN:-}" || args+=(--sign "$INSTALLER_IDENTITY" --timestamp)
productbuild "${args[@]}" "$PKG"

notarize() {
  xcrun notarytool submit "$1" --keychain-profile "$DW303_NOTARY_PROFILE" --wait
  xcrun stapler staple "$1"
  xcrun stapler validate "$1"
}
if ! truthy "${SKIP_SIGN:-}" && [[ -n "${DW303_NOTARY_PROFILE:-}" ]]; then
  notarize "$PKG"
fi

stage="$WORK/dmg"
mkdir -p "$stage"
cp "$PKG" "$stage/Install DW-303.pkg"
cp "$ROOT/LICENSE" "$stage/LICENSE.txt"
cp "$PLUGINS/THIRD-PARTY.txt" "$stage/THIRD-PARTY.txt"
hdiutil create -volname "DW-303 $VERSION" -srcfolder "$stage" -ov -format UDZO "$DMG" >/dev/null
if ! truthy "${SKIP_SIGN:-}"; then
  codesign --force --timestamp --sign "${DW303_SIGN_IDENTITY:-Developer ID Application}" "$DMG"
  [[ -z "${DW303_NOTARY_PROFILE:-}" ]] || notarize "$DMG"
fi
rm -rf "$WORK"
echo "Installer: $PKG"
echo "DMG:       $DMG"
echo "Upload the DMG to the site as downloads/dw-303-mac-latest.dmg."
