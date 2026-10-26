#!/usr/bin/env bash
# Builds every SKAZI plugin for Linux and packages the VST3 plugins and
# standalone applications into one archive:
#   dist/SKAZI Bundle Linux.tar.gz
#
#   bash linux/build_linux.sh
#
# Needs a C++ toolchain, CMake and Git. JUCE is downloaded automatically.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-linux}"
DIST="${DIST_DIR:-$ROOT/dist}"
JOBS="${JOBS:-$(nproc)}"
VERSION="${VERSION:-1.1}"

mkdir -p "$BUILD" "$DIST"

get_juce() {
    local dir="$BUILD/JUCE-$1"
    if [ ! -f "$dir/CMakeLists.txt" ]; then
        git clone -q --depth 1 --branch "$1" https://github.com/juce-framework/JUCE.git "$dir"
    fi
    printf '%s\n' "$dir"
}

J4="$(get_juce 8.0.4)"
J15="$(get_juce 8.0.15)"
STAGE="$BUILD/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/VST3/SKAZI" "$STAGE/Standalone/SKAZI"

# folder | CMake target | product name | JUCE flag | JUCE checkout
build() {
    local folder="$1" target="$2" name="$3" flag="$4" juce="$5"
    local b="$BUILD/b-$target"
    local art="$b/${target}_artefacts/Release"

    echo "== Building $name"
    cmake -S "$ROOT/$folder" -B "$b" -DCMAKE_BUILD_TYPE=Release "-D$flag=$juce"
    cmake --build "$b" --config Release \
        --target "${target}_VST3" "${target}_Standalone" \
        --parallel "$JOBS"

    cp -R "$art/VST3/$name.vst3" "$STAGE/VST3/SKAZI/"
    cp -R "$art/Standalone/$name" "$STAGE/Standalone/SKAZI/"
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

ARCHIVE="$DIST/SKAZI Bundle Linux.tar.gz"
rm -f "$ARCHIVE"
tar -C "$STAGE" -czf "$ARCHIVE" .
echo "== Done: $ARCHIVE"

# Debian-family package. The VST3 bundle follows the system-wide Linux VST3
# location; standalone applications remain grouped under /usr/lib/skazi-bundle.
DEB_ROOT="$BUILD/deb-root"
DEB="$DIST/SKAZI Bundle Linux.deb"
rm -rf "$DEB_ROOT"
mkdir -p "$DEB_ROOT/DEBIAN" "$DEB_ROOT/usr/lib/vst3" "$DEB_ROOT/usr/lib/skazi-bundle"
cp -R "$STAGE/VST3/SKAZI" "$DEB_ROOT/usr/lib/vst3/"
cp -R "$STAGE/Standalone/SKAZI" "$DEB_ROOT/usr/lib/skazi-bundle/"
printf '%s\n' \
    'Package: skazi-bundle' \
    "Version: ${VERSION#v}" \
    'Section: sound' \
    'Priority: optional' \
    'Architecture: amd64' \
    'Maintainer: SKAZI' \
    'Description: SKAZI Bundle audio plugins and standalone applications' \
    ' Ten SKAZI VST3 plugins and their standalone applications.' \
    > "$DEB_ROOT/DEBIAN/control"
rm -f "$DEB"
dpkg-deb --build --root-owner-group "$DEB_ROOT" "$DEB" >/dev/null
echo "== Done: $DEB"
