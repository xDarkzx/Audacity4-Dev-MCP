#!/usr/bin/env bash
# Turns the staged install from build.sh into Debian packages that use the Qt of
# the distribution. Runs inside the build container (dpkg-dev, binutils, the -dev
# packages), or on a Debian host with the same packages.
#
#   make_deb.sh <stage-dir> <out-dir> <version>
#
#   version  e.g. 4.0.0~mcp17 (the package version becomes <version>+<suite>)
#
# Packages:
#   <name>          the program, debug information stripped
#   <name>-dbgsym   the debug information under /usr/lib/debug/.build-id, as Debian
#                   does it (found by gdb and debuginfod through the build id)
#
# Layout of <name>:
#   /opt/audacity4/...                the install of Audacity 4 (prefix of build.sh)
#   /opt/audacity4/lib/forgeiec/      libforge-mcp, only when built from the submodule
#   /usr/bin/audacity4 -> /opt/audacity4/bin/audacity
#   /usr/share/applications/audacity4.desktop
# Libraries are found through the RUNPATH of the binary ($ORIGIN/../lib, and the
# forgeiec directory of libforge-mcp: the bundled one or that of the installed
# package). Qt comes from the distribution: the linked libraries through
# dpkg-shlibdeps, the QML modules (loaded at runtime, invisible to shlibdeps) from
# the imports of the QML sources. The Debian package "audacity" (3.x) is not touched.
#
# Environment:
#   AU4_PREFIX     install prefix inside the package (default /opt/audacity4)
#   AU4_DEB_NAME   package name (default audacity4-mcp)
#   AU4_DEB_SUITE  suffix of the version (default: VERSION_CODENAME of the system)
set -euo pipefail

STAGE=${1:?stage dir}
OUT=${2:?out dir}
VERSION=${3:?version}
SUITE=${AU4_DEB_SUITE:-$(. /etc/os-release && echo "$VERSION_CODENAME")}
PREFIX=${AU4_PREFIX:-/opt/audacity4}
PKG=${AU4_DEB_NAME:-audacity4-mcp}
SRC_DIR=$(cd "$(dirname "$0")/../../../.." && pwd)
FULL_VERSION="${VERSION}+${SUITE}"
MAINTAINER="Audacity4-MCP CI <ci@localhost>"

ROOT=$(mktemp -d)
DBG=$(mktemp -d)
trap 'rm -rf "$ROOT" "$DBG"' EXIT
cp -a "$STAGE/." "$ROOT/"
APP="$ROOT$PREFIX"
# Headers, CMake and pkg-config files (of libforge-mcp) are not needed at runtime.
rm -rf "$APP/include" "$APP/lib/cmake" "$APP/lib/pkgconfig"
# Files a bundled libforge-mcp installs outside our prefix (shell snippets of its
# certificate tooling) belong to the forgeiec packages; shipping them here would
# collide with forgeiec-libforge-mcp0 on the same machine.
PREFIX_TOP=$(echo "$PREFIX" | cut -d/ -f2)
for entry in "$ROOT"/*; do
    case "$(basename "$entry")" in
        "$PREFIX_TOP"|usr) ;;
        *) echo "dropping /${entry#"$ROOT"/}"; rm -rf "$entry" ;;
    esac
done

BIN=$(find "$APP/bin" -maxdepth 1 -type f -name 'audacity*' -perm -u+x | head -1)
[ -n "$BIN" ] || { echo "no audacity binary under $APP/bin"; exit 1; }
BIN_NAME=$(basename "$BIN")
echo "== Binary: $PREFIX/bin/$BIN_NAME (RUNPATH $(objdump -p "$BIN" | awk '/RUNPATH/{print $2}'))"

echo "== Command and desktop entry"
mkdir -p "$ROOT/usr/bin" "$ROOT/usr/share/applications"
ln -s "$PREFIX/bin/$BIN_NAME" "$ROOT/usr/bin/audacity4"
ICON=$(find "$APP/share" -path '*/icons/*' -name '*.png' -size +8k 2>/dev/null | sort | tail -1 || true)
cat > "$ROOT/usr/share/applications/audacity4.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Audacity 4 (MCP)
GenericName=Audio Editor
Comment=Audacity 4 with an MCP server for AI assistants
Exec=audacity4 %F
Icon=${ICON:+${ICON#$ROOT}}
Terminal=false
Categories=AudioVideo;Audio;AudioVideoEditing;
MimeType=audio/x-wav;audio/flac;audio/mpeg;audio/ogg;
DESKTOP
desktop-file-validate "$ROOT/usr/share/applications/audacity4.desktop" || true

mapfile -t ELFS < <(find "$ROOT" -type f \( -perm -u+x -o -name '*.so*' \) -exec sh -c \
    'file -b "$1" | grep -q "^ELF" && echo "$1"' _ {} \;)

echo "== Debug information -> ${PKG}-dbgsym"
BUILD_IDS=()
for f in "${ELFS[@]}"; do
    id=$(readelf -n "$f" 2>/dev/null | sed -n 's/^ *Build ID: *//p' | head -1)
    [ -n "$id" ] || continue
    readelf -S "$f" | grep -q '\.debug_info' || continue
    dest="$DBG/usr/lib/debug/.build-id/${id:0:2}/${id:2}.debug"
    mkdir -p "$(dirname "$dest")"
    objcopy --only-keep-debug --compress-debug-sections "$f" "$dest"
    chmod 644 "$dest"
    strip --remove-section=.comment --remove-section=.note --strip-unneeded "$f"
    BUILD_IDS+=("$id")
done
echo "${#BUILD_IDS[@]} of ${#ELFS[@]} ELF files split"

echo "== Library dependencies"
WORK=$(mktemp -d)
mkdir -p "$WORK/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: amd64\nDescription: x\n' "$PKG" "$PKG" > "$WORK/debian/control"
LIBDIRS=$(find "$APP" -type f -name '*.so*' -printf '%h\n' | sort -u | sed 's/^/-l/')
# shellcheck disable=SC2086
SHLIBS=$(cd "$WORK" && dpkg-shlibdeps -O --ignore-missing-info --warnings=1 $LIBDIRS "${ELFS[@]}" \
         | sed -n 's/^shlibs:Depends=//p')
rm -rf "$WORK"

# A library from the forgeiec directory of an installed package carries no shlibs
# information, and --ignore-missing-info drops it without a word. Depend on the
# package that owns it instead. When the package bundles libforge-mcp itself there
# is no such library: grep finds nothing, which must not end the script (pipefail).
FORGEIEC=$(ldd "$BIN" 2>/dev/null | awk '{print $3}' | { grep '/forgeiec/' || true; } | { grep -v "^$ROOT" || true; } \
           | while read -r path; do
                 pkg=$(dpkg -S "$path" 2>/dev/null | cut -d: -f1 | head -1)
                 [ -n "$pkg" ] || continue
                 ver=$(dpkg-query -W -f='${Version}' "$pkg" | sed 's/[~+].*//')
                 echo "$pkg (>= $ver)"
             done | sort -u | paste -sd, | sed 's/,/, /g')
# Only where shlibs did not already name the package.
FORGEIEC=$(echo "$FORGEIEC" | tr ',' '\n' | sed 's/^ *//' | while read -r dep; do
               [ -n "$dep" ] || continue
               grep -q "${dep%% *}" <<<"$SHLIBS" || echo "$dep"
           done | paste -sd, | sed 's/,/, /g')
[ -n "$FORGEIEC" ] && echo "forgeiec: $FORGEIEC"

echo "== QML module dependencies"
# Debian names a QML module package after the module: QtQuick.Controls ->
# qml6-module-qtquick-controls, Qt5Compat.GraphicalEffects ->
# qml6-module-qt5compat-graphicaleffects. A wrong name fails the install test.
QML=$(grep -rhoE --include=*.qml '^import (Qt[A-Za-z0-9.]*)' "$SRC_DIR/src" "$SRC_DIR/muse/framework" \
      | awk '{print $2}' | sort -u | tr 'A-Z.' 'a-z-' | sed 's/^/qml6-module-/' | paste -sd, | sed 's/,/, /g')
echo "QML: $QML"
DEPENDS="$SHLIBS${FORGEIEC:+, $FORGEIEC}${QML:+, $QML}"

mkdir -p "$ROOT/DEBIAN" "$OUT"
cat > "$ROOT/DEBIAN/control" <<CONTROL
Package: $PKG
Version: $FULL_VERSION
Architecture: amd64
Maintainer: $MAINTAINER
Installed-Size: $(du -sk "$ROOT" | cut -f1)
Depends: $DEPENDS
Section: sound
Priority: optional
Homepage: https://github.com/xDarkzx/Audacity4-Dev-MCP
Description: Audacity 4 with an MCP server for AI assistants
 Audacity 4 built from the Audacity4-Dev-MCP fork against the Qt of the
 distribution. While it runs, libforge-mcp serves the Audacity commands as MCP
 tools on 127.0.0.1 (default port 7536, bearer token; settings in
 ~/.local/share/Audacity/Audacity4/mcp.ini). Installed under $PREFIX; the
 command is "audacity4". The Debian package "audacity" (3.x) is not touched.
CONTROL
DEB="$OUT/${PKG}_${FULL_VERSION}_amd64.deb"
fakeroot dpkg-deb --build --root-owner-group -Zxz "$ROOT" "$DEB"
dpkg-deb --info "$DEB" | sed -n '1,20p'
echo "== Package: $DEB ($(du -h "$DEB" | cut -f1))"

if [ "${#BUILD_IDS[@]}" -gt 0 ]; then
    mkdir -p "$DBG/DEBIAN"
    cat > "$DBG/DEBIAN/control" <<CONTROL
Package: ${PKG}-dbgsym
Source: $PKG
Version: $FULL_VERSION
Architecture: amd64
Maintainer: $MAINTAINER
Installed-Size: $(du -sk "$DBG" | cut -f1)
Depends: $PKG (= $FULL_VERSION)
Section: debug
Priority: optional
Auto-Built-Package: debug-symbols
Build-Ids: ${BUILD_IDS[*]}
Description: debug symbols for $PKG
CONTROL
    DBGDEB="$OUT/${PKG}-dbgsym_${FULL_VERSION}_amd64.deb"
    fakeroot dpkg-deb --build --root-owner-group -Zxz "$DBG" "$DBGDEB"
    echo "== Package: $DBGDEB ($(du -h "$DBGDEB" | cut -f1))"
fi
