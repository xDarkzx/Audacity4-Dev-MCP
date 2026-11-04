#!/usr/bin/env bash
# Installs the built package into a clean Debian base image (no build tools, no Qt)
# and runs the MCP smoke test against it. A pass proves both the package
# dependencies (incl. the runtime QML modules) and the MCP server. Runs inside a
# fresh container of the target base image, as root.
#
#   test_deb.sh <deb-file> <log-dir>
#
# Environment:
#   AU4_SMOKE_WAIT  seconds to wait for the MCP port (default 180)
#   FORGEIEC_APT    apt repository of the forgeiec packages (libforge-mcp), which
#                   the package depends on (default https://apt.forgeiec.io)
#   FORGEIEC_SUITE  its suite (default trixie)
set -euo pipefail

DEB=${1:?deb file}
LOGS=${2:?log dir}
WAIT=${AU4_SMOKE_WAIT:-180}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$LOGS"

export DEBIAN_FRONTEND=noninteractive
FORGEIEC_APT=${FORGEIEC_APT:-https://apt.forgeiec.io}
FORGEIEC_SUITE=${FORGEIEC_SUITE:-trixie}
apt-get update -qq
apt-get install -y -qq --no-install-recommends ca-certificates curl >/dev/null
# The package depends on forgeiec-libforge-mcp0, published in the forgeiec repository.
install -d /etc/apt/keyrings
curl -fsSL "$FORGEIEC_APT/forgeiec.gpg" -o /etc/apt/keyrings/forgeiec.gpg
echo "deb [signed-by=/etc/apt/keyrings/forgeiec.gpg] $FORGEIEC_APT/$FORGEIEC_SUITE $FORGEIEC_SUITE main" \
  > /etc/apt/sources.list.d/forgeiec.list
apt-get update -qq
# Only what the test itself needs; everything Audacity needs must come from the
# Depends of the package.
apt-get install -y -qq --no-install-recommends xvfb xauth python3 ca-certificates >/dev/null
apt-get install -y --no-install-recommends "$DEB"

echo "== Installed"
dpkg -s audacity4-mcp | sed -n '1,12p'
command -v audacity4
# libforge-mcp must come from forgeiec-libforge-mcp0, found through the RPATH.
APPBIN=$(readlink -f "$(command -v audacity4)")
ldd "$APPBIN" | grep 'libforge-mcp'
if ldd "$APPBIN" | grep -q 'libforge-mcp.*not found'; then echo 'libforge-mcp not found'; exit 1; fi

# Audacity runs as an ordinary user: settings and token live in that user's home.
useradd -m smoke
# The log directory may sit on a mount the test user cannot write to: log in its
# home and copy the logs out afterwards.
SLOGS=/home/smoke/logs
su smoke -s /bin/bash -c "mkdir -p $SLOGS"

# The test's exit code is taken from PIPESTATUS below: do not let errexit end
# this script before the logs are copied out.
set +e
su smoke -s /bin/bash -c "
  set -euo pipefail
  export HOME=/home/smoke
  xvfb-run -a -s '-screen 0 1600x1000x24' audacity4 >'$SLOGS/audacity4.log' 2>&1 &
  APP=\$!
  PORT=
  for i in \$(seq 1 $WAIT); do
    # Under pipefail a failing find (the directory does not exist before the
    # first start) would end the script without a word: tolerate it.
    INI=\$(find \$HOME/.local/share -name mcp.ini 2>/dev/null | head -1 || true)
    [ -n \"\$INI\" ] && PORT=\$(sed -n 's/^port=//p' \"\$INI\" | head -1)
    if (exec 3<>/dev/tcp/127.0.0.1/\${PORT:-7536}) 2>/dev/null; then echo \"MCP server up after \${i}s\"; break; fi
    if ! kill -0 \$APP 2>/dev/null; then echo 'audacity4 exited early'; tail -50 '$SLOGS/audacity4.log'; exit 1; fi
    sleep 1
  done
  (exec 3<>/dev/tcp/127.0.0.1/\${PORT:-7536}) 2>/dev/null || { echo 'MCP server did not come up'; tail -50 '$SLOGS/audacity4.log'; exit 1; }
  sleep 3
  RC=0
  SMOKE_DIR=/home/smoke/au4-smoke python3 -I '$HERE/smoke_test.py' || RC=\$?
  kill \$APP 2>/dev/null || true
  wait \$APP 2>/dev/null || true
  exit \$RC
" 2>&1 | tee "$LOGS/smoke.log"
RC=${PIPESTATUS[0]}
set -e
cp -r "$SLOGS/." "$LOGS/" 2>/dev/null || true
exit "$RC"
