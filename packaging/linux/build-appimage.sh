#!/usr/bin/env bash
# Build the Real Steel x86_64 AppImage + release zip from this repository.
#
# usage: packaging/linux/build-appimage.sh <version>      e.g. v1.1
#
# Inputs (override with env vars):
#   GAME_BUILD   game build folder with realsteel + librexruntime.so +
#                librexgpu-xenos.so (one matched set, never mixed)
#                default: out/build/linux-amd64-release
#   LAUNCHER_BIN TPFactualMetalLauncher built from tp-launcher/ with
#                -DTP_GAME=realsteel   default: out/launcher/TPFactualMetalLauncher
#   SEVENZIP     self-contained 7zz binary bundled for archive import
#   TOOLS        folder with linuxdeploy, linuxdeploy-plugin-qt, appimagetool
#   OUT_DIR      where the AppImage and zip are written (default: out/release)
#
# Layout: launcher, game exe and the matched runtime libs together in
# usr/bin (the launcher finds the program set next to itself), plus 7zz.
# No game data is bundled.
set -euo pipefail

VERSION="${1:?usage: build-appimage.sh <version, e.g. v1.1>}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
GAME_BUILD="${GAME_BUILD:-$REPO/out/build/linux-amd64-release}"
LAUNCHER_BIN="${LAUNCHER_BIN:-$REPO/out/launcher/TPFactualMetalLauncher}"
SEVENZIP="${SEVENZIP:-/mnt/sdb1/Games/RealSteel/appimage/7zz}"
TOOLS="${TOOLS:-/home/muse/tools}"
OUT_DIR="${OUT_DIR:-$REPO/out/release}"
WORK="$OUT_DIR/work-$VERSION"
APPDIR="$WORK/AppDir"
APPIMAGE="$OUT_DIR/RealSteel-x86_64-$VERSION.AppImage"
ZIP="$OUT_DIR/RealSteel-Linux-$VERSION.zip"

for f in "$LAUNCHER_BIN" "$GAME_BUILD/realsteel" "$GAME_BUILD/realsteel-tu" \
         "$GAME_BUILD/librexruntime.so" \
         "$GAME_BUILD/librexgpu-xenos.so" "$SEVENZIP" "$TOOLS/linuxdeploy" \
         "$TOOLS/appimagetool"; do
  [[ -f "$f" ]] || { echo "Missing input: $f" >&2; exit 1; }
done

rm -rf "$WORK"
install -d "$APPDIR/usr/bin" "$APPDIR/usr/share/licenses/realsteel"
install -m755 "$LAUNCHER_BIN" "$APPDIR/usr/bin/TPFactualMetalLauncher"
install -m755 "$GAME_BUILD/realsteel" "$APPDIR/usr/bin/realsteel"
install -m755 "$GAME_BUILD/realsteel-tu" "$APPDIR/usr/bin/realsteel-tu"
install -m755 "$GAME_BUILD/librexruntime.so" "$APPDIR/usr/bin/librexruntime.so"
install -m755 "$GAME_BUILD/librexgpu-xenos.so" "$APPDIR/usr/bin/librexgpu-xenos.so"
install -m755 "$SEVENZIP" "$APPDIR/usr/bin/7zz"

# Attribution pack ships inside the AppImage.
install -m644 "$REPO/LICENSE" "$REPO/CREDITS.md" "$APPDIR/usr/share/licenses/realsteel/"
cp -r "$REPO/licenses" "$APPDIR/usr/share/licenses/realsteel/"

cat > "$WORK/factual-metal.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=Factual Metal Launcher
Comment=Launcher for the Real Steel (Yuke's) Linux recompilation
Exec=TPFactualMetalLauncher
Icon=factual-metal
Categories=Game;
Terminal=false
DESK
cp "$HERE/factual-metal.png" "$WORK/factual-metal.png"

export QMAKE="$(command -v qmake6 || command -v qmake)"
export PATH="$TOOLS:$PATH"
"$TOOLS/linuxdeploy" \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/TPFactualMetalLauncher" \
  --executable "$APPDIR/usr/bin/realsteel" \
  --executable "$APPDIR/usr/bin/realsteel-tu" \
  --executable "$APPDIR/usr/bin/7zz" \
  --desktop-file "$WORK/factual-metal.desktop" \
  --icon-file "$WORK/factual-metal.png" \
  --plugin qt

mkdir -p "$OUT_DIR"
ARCH=x86_64 "$TOOLS/appimagetool" "$APPDIR" "$APPIMAGE"
chmod 755 "$APPIMAGE"

# Release zip: the AppImage plus README.txt (version filled in).
ZIPDIR="$WORK/zip"
mkdir -p "$ZIPDIR"
cp "$APPIMAGE" "$ZIPDIR/"
sed "s/@VERSION@/$VERSION/g" "$HERE/README.txt.in" > "$ZIPDIR/README.txt"
rm -f "$ZIP"
(cd "$ZIPDIR" && zip -q -9 "$ZIP" "$(basename "$APPIMAGE")" README.txt)
echo "Created: $APPIMAGE"
echo "Created: $ZIP"
