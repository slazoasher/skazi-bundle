#!/bin/bash
# Builds every SKAZI plugin for macOS (Apple Silicon + Intel) and wraps them in one installer:
#   dist/SKAZI Bundle.pkg
#
#   bash mac/build_mac.sh
#
# Needs Xcode command line tools, CMake and Git. JUCE is downloaded automatically.
# The installer puts:
#   VST3  -> /Library/Audio/Plug-Ins/VST3/SKAZI/
#   AU    -> /Library/Audio/Plug-Ins/Components/
#   Apps  -> /Applications/SKAZI/   (plus "Uninstall SKAZI Bundle.command")
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-mac}"
DIST="$ROOT/dist"
VERSION="1.1"
JOBS="$(sysctl -n hw.ncpu)"
MAC_ARCH="${MAC_ARCH:-universal}"

case "$MAC_ARCH" in
    universal) CMAKE_ARCHITECTURES="arm64;x86_64" ;;
    arm64|x86_64) CMAKE_ARCHITECTURES="$MAC_ARCH" ;;
    *) echo "Unsupported MAC_ARCH: $MAC_ARCH (use universal, arm64, or x86_64)" >&2; exit 2 ;;
esac

echo "== macOS architecture: $MAC_ARCH ($CMAKE_ARCHITECTURES)"
mkdir -p "$BUILD" "$DIST"
chmod +x "$ROOT/mac/scripts-au/postinstall" # pkgbuild only runs executable scripts

get_juce() {
    local dir="$BUILD/JUCE-$1"
    [ -f "$dir/CMakeLists.txt" ] || git clone -q --depth 1 --branch "$1" https://github.com/juce-framework/JUCE.git "$dir"
    echo "$dir"
}
J4="$(get_juce 8.0.4)"
J15="$(get_juce 8.0.15)"

STAGE="$BUILD/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/VST3/SKAZI" "$STAGE/Components" "$STAGE/Applications/SKAZI"

# folder | CMake target | product name | JUCE flag | JUCE dir
build() {
    local folder="$1" target="$2" name="$3" flag="$4" juce="$5"
    local b="$BUILD/b-$target"
    echo "== Building $name"
    cmake -S "$ROOT/$folder" -B "$b" -DCMAKE_BUILD_TYPE=Release "-D$flag=$juce" \
          "-DCMAKE_OSX_ARCHITECTURES=$CMAKE_ARCHITECTURES" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
    cmake --build "$b" --config Release --target "${target}_VST3" "${target}_AU" "${target}_Standalone" -j "$JOBS"
    local art="$b/${target}_artefacts/Release"
    cp -R "$art/VST3/$name.vst3" "$STAGE/VST3/SKAZI/"
    cp -R "$art/AU/$name.component" "$STAGE/Components/"
    cp -R "$art/Standalone/$name.app" "$STAGE/Applications/SKAZI/"
}

build SKAZI-Synth SKAZI      "SKAZI"          JUCE_DIR                     "$J15"
build Shaksuka    Shaksuka   "SHAKSUKA"       FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build PitaPump    PitaPump   "PITA PUMP"      FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build FilterFish  FilterFish "The FilterFish" FETCHCONTENT_SOURCE_DIR_JUCE "$J15"
build Subich      Subich     "SUBICH"         FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build Bamba       Bamba      "BAMBA"          FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build Tahini      Tahini     "TAHINI"         FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build Zhug        Zhug       "ZHUG"           FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build Bully       Bully      "BULLY"          FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build Kickass     Kickass    "KICKASS"        FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build Salat       Salat      "SALAT"          FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build TBON303     Skazi303   "SKAZI 303"      FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build MidiGod     MidiGod    "MIDI GOD"       FETCHCONTENT_SOURCE_DIR_JUCE "$J4"
build Kube        Kube       "KUBE"           FETCHCONTENT_SOURCE_DIR_JUCE "$J4"

cp "$ROOT/mac/Uninstall SKAZI Bundle.command" "$STAGE/Applications/SKAZI/"
chmod +x "$STAGE/Applications/SKAZI/Uninstall SKAZI Bundle.command"

# Ad-hoc sign everything so Apple Silicon Macs will load it (not an Apple Developer signature)
echo "== Signing (ad-hoc)"
for b in "$STAGE"/VST3/SKAZI/*.vst3 "$STAGE"/Components/*.component "$STAGE"/Applications/SKAZI/*.app; do
    codesign --force --deep --sign - "$b"
done

# One package per destination; bundles must not be "relocatable" or macOS may install
# them wherever an older copy happens to live.
echo "== Packaging"
make_pkg() {
    local root="$1" location="$2" id="$3" out="$4" scripts="${5:-}"
    local plist="$BUILD/$id.plist"
    pkgbuild --analyze --root "$root" "$plist"
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null \
            || /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$plist"
        i=$((i + 1))
    done
    local extra=()
    [ -n "$scripts" ] && extra=(--scripts "$scripts")
    pkgbuild --root "$root" --component-plist "$plist" --install-location "$location" \
             --identifier "$id" --version "$VERSION" ${extra[@]+"${extra[@]}"} "$BUILD/$out" # macOS bash 3.2: empty arrays need this form
}
make_pkg "$STAGE/VST3"         "/Library/Audio/Plug-Ins/VST3"       com.skazi.bundle.vst3 vst3.pkg
make_pkg "$STAGE/Components"   "/Library/Audio/Plug-Ins/Components" com.skazi.bundle.au   au.pkg "$ROOT/mac/scripts-au"
make_pkg "$STAGE/Applications" "/Applications"                      com.skazi.bundle.apps apps.pkg

productbuild --distribution "$ROOT/mac/distribution.xml" --resources "$ROOT/mac" \
             --package-path "$BUILD" "$DIST/SKAZI Bundle.pkg"
echo "== Done: $DIST/SKAZI Bundle.pkg"
