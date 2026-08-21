#!/usr/bin/env bash
# Build a universal macOS .app bundle and a DMG.
# Requirements on PATH: CMake, Qt 6 (CMAKE_PREFIX_PATH if needed), macdeployqt.
set -euo pipefail

cd "$(dirname "$0")/../.."

VERSION="$(sed -n 's/^project(RiipL VERSION \([^ ]*\).*/\1/p' CMakeLists.txt | head -1)"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build -j"$(sysctl -n hw.ncpu)"

APP="build/RiipL.app"
[ -d "$APP" ] || { echo "RiipL.app not found"; exit 1; }

macdeployqt "$APP" -always-overwrite

codesign --force --deep -s - "$APP"

DMG="RiipL-$VERSION-macos.dmg"
rm -f "$DMG"
hdiutil create -volname RiipL -srcfolder "$APP" -ov -format UDZO "$DMG"

echo "artifact: $DMG"
