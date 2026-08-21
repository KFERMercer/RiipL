#!/usr/bin/env bash
# Build a self-contained x86_64 AppImage.
# Requirements: linuxdeploy + linuxdeploy-plugin-qt AppImages (see TOOLS_DIR below),
#               qmake6 available for the Qt plugin, FUSE2 or APPIMAGE_EXTRACT_AND_RUN=1.
set -euo pipefail

cd "$(dirname "$0")/../.."

VERSION="$(sed -n 's/^project(RiipL VERSION \([^ ]*\).*/\1/p' CMakeLists.txt | head -1)"
BUILD_DIR="build"
APPDIR="build/AppDir"
TOOLS_DIR="${TOOLS_DIR:-build/tools}"
QMAKE="${QMAKE:-/usr/lib/qt6/bin/qmake}"

[ -x "$TOOLS_DIR/linuxdeploy-x86_64.AppImage" ] || { echo "missing $TOOLS_DIR/linuxdeploy-x86_64.AppImage"; exit 1; }
[ -x "$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage" ] || { echo "missing $TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"; exit 1; }

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/scalable/apps"

cp "$BUILD_DIR/RiipL" "$APPDIR/usr/bin/"
cp packaging/linux/riipl.desktop "$APPDIR/usr/share/applications/"
cp resources/icons/app.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/riipl.svg"

export QMAKE
export APPIMAGE_EXTRACT_AND_RUN=1
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

"$TOOLS_DIR/linuxdeploy-x86_64.AppImage" --appimage-extract-and-run \
    --appdir "$APPDIR" \
    -e "$APPDIR/usr/bin/RiipL" \
    -d "$APPDIR/usr/share/applications/riipl.desktop" \
    -i "$APPDIR/usr/share/icons/hicolor/scalable/apps/riipl.svg" \
    --plugin qt \
    --output appimage

echo "artifact: $(ls -1 RiipL-*.AppImage)"
