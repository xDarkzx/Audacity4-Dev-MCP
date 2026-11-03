#!/usr/bin/env bash
# Turns the staged install from build.sh into a Debian package that brings its own Qt.
# Runs inside the build container (it needs the Qt of the image and dpkg-dev).
#
#   make_deb.sh <stage-dir> <out-dir> <target> <version>
#
#   target   trixie | noble  (goes into the package version, e.g. 4.0.2~mcp1+trixie)
#   version  upstream version without the target suffix, e.g. 4.0.2~mcp17
#
# Layout of the package:
#   /opt/audacity4/...          the install of Audacity 4 (prefix of build.sh)
#   /opt/audacity4/qt/{lib,plugins,qml}   Qt runtime from the build image
#   /usr/bin/audacity4          wrapper that points the loader at both
#   /usr/share/applications/audacity4.desktop
# The Debian package "audacity" (3.x) is left alone; both can be installed together.
set -euo pipefail

STAGE=${1:?stage dir}
OUT=${2:?out dir}
TARGET=${3:?target}
VERSION=${4:?version}
PREFIX=${AU4_PREFIX:-/opt/audacity4}
PKG=${AU4_DEB_NAME:-audacity4-mcp}
QT=${QT_DIR:?QT_DIR not set}

ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT
cp -a "$STAGE/." "$ROOT/"
APP="$ROOT$PREFIX"

BIN=$(find "$APP/bin" -maxdepth 1 -type f -name 'audacity*' -perm -u+x | head -1)
[ -n "$BIN" ] || { echo "no audacity binary under $APP/bin"; exit 1; }
BIN_NAME=$(basename "$BIN")
echo "== Binary: $PREFIX/bin/$BIN_NAME"

echo "== Bundling Qt runtime from $QT"
mkdir -p "$APP/qt"
cp -a "$QT/lib" "$APP/qt/"
cp -a "$QT/plugins" "$APP/qt/"
cp -a "$QT/qml" "$APP/qt/"
# Development files and drivers for databases nobody here has are not needed at runtime.
rm -rf "$APP/qt/lib/cmake" "$APP/qt/lib/pkgconfig" "$APP/qt/lib/metatypes" "$APP/qt/lib/objects-"*
find "$APP/qt" \( -name '*.a' -o -name '*.prl' -o -name '*.la' -o -name '*.debug' \
                  -o -name '*.qmltypes' -o -name 'plugins.qmltypes' \) -delete
find "$APP/qt/plugins/sqldrivers" -type f ! -name 'libqsqlite.so' -delete 2>/dev/null || true
cat > "$APP/bin/qt.conf" <<EOF
[Paths]
Prefix = ../qt
EOF

echo "== Wrapper and desktop entry"
mkdir -p "$ROOT/usr/bin" "$ROOT/usr/share/applications"
cat > "$ROOT/usr/bin/audacity4" <<EOF
#!/bin/sh
# Audacity 4 with the MCP bridge (listens on 127.0.0.1:2212 while it runs).
export LD_LIBRARY_PATH="$PREFIX/lib:$PREFIX/qt/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "$PREFIX/bin/$BIN_NAME" "\$@"
EOF
chmod 755 "$ROOT/usr/bin/audacity4"

ICON=$(find "$APP/share" -path '*/icons/*' -name '*.png' -size +8k 2>/dev/null | sort | tail -1 || true)
cat > "$ROOT/usr/share/applications/audacity4.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Audacity 4 (MCP)
GenericName=Audio Editor
Comment=Audacity 4 with the MCP bridge for AI assistants
Exec=audacity4 %F
Icon=${ICON:+${ICON#$ROOT}}
Terminal=false
Categories=AudioVideo;Audio;AudioVideoEditing;
MimeType=audio/x-wav;audio/flac;audio/mpeg;audio/ogg;
EOF
desktop-file-validate "$ROOT/usr/share/applications/audacity4.desktop" || true

echo "== Runtime dependencies"
# dpkg-shlibdeps resolves every ELF of the package; the bundled libraries are found
# through -l and so do not become dependencies themselves.
WORK=$(mktemp -d)
mkdir -p "$WORK/debian"
cat > "$WORK/debian/control" <<EOF
Source: $PKG
Section: sound
Priority: optional
Maintainer: Audacity4-MCP CI <ci@localhost>

Package: $PKG
Architecture: amd64
Description: x
EOF
mapfile -t ELFS < <(find "$ROOT" -type f \( -perm -u+x -o -name '*.so*' \) -exec sh -c \
    'file -b "$1" | grep -q "^ELF" && echo "$1"' _ {} \;)
LIBDIRS=$(find "$APP" -type f -name '*.so*' -printf '%h\n' | sort -u | sed 's/^/-l/')
# shellcheck disable=SC2086
DEPENDS=$(cd "$WORK" && dpkg-shlibdeps -O --ignore-missing-info --warnings=1 $LIBDIRS "${ELFS[@]}" \
          | sed -n 's/^shlibs:Depends=//p')
rm -rf "$WORK"
echo "Depends: $DEPENDS"

SIZE=$(du -sk "$ROOT" | cut -f1)
FULL_VERSION="${VERSION}+${TARGET}"
mkdir -p "$ROOT/DEBIAN"
cat > "$ROOT/DEBIAN/control" <<EOF
Package: $PKG
Version: $FULL_VERSION
Architecture: amd64
Maintainer: Audacity4-MCP CI <ci@localhost>
Installed-Size: $SIZE
Depends: $DEPENDS
Section: sound
Priority: optional
Homepage: https://github.com/xDarkzx/Audacity4-Dev-MCP
Description: Audacity 4 with the MCP bridge for AI assistants
 Audacity 4 built from the Audacity4-Dev-MCP fork. While it runs, the "mcp"
 module listens on 127.0.0.1:2212 (token-authenticated) for the Python MCP
 server "audacity4-mcp". Installed under $PREFIX with its own Qt; the
 command is "audacity4". The Debian package "audacity" (3.x) is not touched.
EOF

mkdir -p "$OUT"
DEB="$OUT/${PKG}_${FULL_VERSION}_amd64.deb"
fakeroot dpkg-deb --build --root-owner-group -Zxz "$ROOT" "$DEB"
dpkg-deb --info "$DEB" | sed -n '1,20p'
echo "== Package: $DEB ($(du -h "$DEB" | cut -f1))"
