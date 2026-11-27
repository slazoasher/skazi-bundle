#!/usr/bin/env bash
# Sign (and optionally notarise) a DW-303 macOS bundle: .clap .vst3 .component
# .lv2 .aaxplugin or .app.
#
#   scripts/sign-macos.sh <bundle> [--notarize]
#
# Environment:
#   DW303_SIGN_IDENTITY   codesign identity (default: first "Developer ID Application")
#   DW303_NOTARY_PROFILE  notarytool keychain profile, created once with
#                         `xcrun notarytool store-credentials <profile>`
#   AAX bundles additionally require PACE wraptool:
#   DW303_WRAPTOOL        path to wraptool
#   DW303_PACE_ACCOUNT    PACE account name
#   DW303_PACE_WCGUID     wrap configuration GUID for DW-303
#
# No credentials are stored in this repository.
set -euo pipefail

bundle="${1:?usage: sign-macos.sh <bundle> [--notarize]}"
notarize="${2:-}"
[[ -d "$bundle" ]] || { echo "not a bundle: $bundle" >&2; exit 1; }

identity="${DW303_SIGN_IDENTITY:-$(security find-identity -v -p codesigning |
  sed -n 's/.*"\(Developer ID Application:[^"]*\)".*/\1/p' | head -1)}"
[[ -n "$identity" ]] || { echo "no Developer ID Application identity found" >&2; exit 1; }

sign() {
  codesign --force --timestamp --options runtime --sign "$identity" "$1"
}

case "$bundle" in
  *.aaxplugin)
    : "${DW303_WRAPTOOL:?set DW303_WRAPTOOL for AAX}"
    : "${DW303_PACE_ACCOUNT:?set DW303_PACE_ACCOUNT}"
    : "${DW303_PACE_WCGUID:?set DW303_PACE_WCGUID}"
    # PACE wraps and signs the bundle with the Developer ID identity.
    "$DW303_WRAPTOOL" sign --verbose --account "$DW303_PACE_ACCOUNT" \
      --wcguid "$DW303_PACE_WCGUID" --signid "$identity" \
      --in "$bundle" --out "$bundle"
    ;;
  *)
    sign "$bundle"
    ;;
esac

codesign --verify --deep --strict --verbose=2 "$bundle"

if [[ "$notarize" == "--notarize" ]]; then
  : "${DW303_NOTARY_PROFILE:?set DW303_NOTARY_PROFILE}"
  archive="$(mktemp -d)/$(basename "$bundle").zip"
  ditto -c -k --keepParent "$bundle" "$archive"
  xcrun notarytool submit "$archive" --keychain-profile "$DW303_NOTARY_PROFILE" --wait
  xcrun stapler staple "$bundle"
  xcrun stapler validate "$bundle"
  case "$bundle" in
    *.app) spctl --assess --type execute --verbose=2 "$bundle" ;;
    *) spctl --assess --type install --verbose=2 "$bundle" || true ;; # plug-in bundles: informational
  esac
fi
echo "signed: $bundle"
