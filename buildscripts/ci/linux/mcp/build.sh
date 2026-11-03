#!/usr/bin/env bash
# Configures, builds and unit-tests Audacity 4 with the MCP bridge, then installs it into
# a staging directory. Runs inside the container from ./Containerfile.
#
#   build.sh <build-dir> <stage-dir>
#
# Environment:
#   AU4_PREFIX    install prefix inside the package (default /opt/audacity4)
#   AU4_JOBS      parallel jobs (default: all cores)
#   AU4_UTESTS    ON/OFF, build and run the unit tests (default ON)
set -euo pipefail

BUILD_DIR=${1:?build dir}
STAGE_DIR=${2:?stage dir}
PREFIX=${AU4_PREFIX:-/opt/audacity4}
JOBS=${AU4_JOBS:-$(nproc)}
UTESTS=${AU4_UTESTS:-ON}
SRC_DIR=$(cd "$(dirname "$0")/../../../.." && pwd)

echo "== Source:  $SRC_DIR"
echo "== Build:   $BUILD_DIR"
echo "== Prefix:  $PREFIX (staged in $STAGE_DIR)"
echo "== Qt:      $(qtpaths6 --qt-version 2>/dev/null || echo ?)"
gcc --version | head -1
cmake --version | head -1

ccache --zero-stats >/dev/null || true

# MUSE_MODULE_UPDATE=OFF: the package is updated through apt. The built-in check
# offers the official Audacity, and installing it would replace this build.

cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DAU4_BUILD_MODE=release \
    -DAU4_BUILD_CONFIGURATION=app \
    -DMUSE_APP_INSTALL_SUFFIX= \
    -DMUSE_ENABLE_UNIT_TESTS="$UTESTS" \
    -DMUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT=OFF \
    -DMUSE_MODULE_UPDATE=OFF \
    -DCMAKE_SKIP_RPATH=OFF

ninja -C "$BUILD_DIR" -j "$JOBS"

ccache --show-stats || true

if [ "$UTESTS" = "ON" ]; then
    echo "== Unit tests"
    # Headless: the tests that touch Qt GUI classes need a platform plugin.
    export QT_QPA_PLATFORM=offscreen
    (cd "$BUILD_DIR" && ctest --output-on-failure --timeout 300 -j "$JOBS" \
        --output-junit "$BUILD_DIR/ctest-junit.xml")
fi

echo "== Install"
rm -rf "$STAGE_DIR"
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR"
find "$STAGE_DIR" -maxdepth 4 -type f -path '*bin*' -print
