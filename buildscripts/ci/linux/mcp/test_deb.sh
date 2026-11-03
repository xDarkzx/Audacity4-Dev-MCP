#!/usr/bin/env bash
# Installs the built package into a clean base image (no build tools, no Qt) and runs
# the MCP smoke test against it. A pass proves both the package dependencies and the
# bridge. Runs inside a fresh container of the target base image, as root.
#
#   test_deb.sh <deb-file> <log-dir>
#
# Environment:
#   AU4_MCP_CLIENT  pip requirement for the client (default: a pinned commit of
#                   xDarkzx/Audacity4-MCP; PyPI 0.1.0 predates the token the bridge requires)
set -euo pipefail

DEB=${1:?deb file}
LOGS=${2:?log dir}
CLIENT=${AU4_MCP_CLIENT:-audacity4-mcp @ git+https://github.com/xDarkzx/Audacity4-MCP@280c7a3f46c19c8686c53a37f788264630aa4b8c}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$LOGS"
chmod 777 "$LOGS"

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
# Only what the test itself needs; everything Audacity needs must come from the
# Depends of the package.
apt-get install -y -qq --no-install-recommends xvfb xauth python3 python3-venv ca-certificates git >/dev/null
apt-get install -y --no-install-recommends "$DEB"

echo "== Installed"
dpkg -s audacity4-mcp | sed -n '1,12p'
command -v audacity4

# Audacity runs as an ordinary user: the token file lives in that user's home.
useradd -m smoke
python3 -m venv /opt/smoke-venv
/opt/smoke-venv/bin/pip install -q "$CLIENT"

su smoke -s /bin/bash -c "
  set -euo pipefail
  export HOME=/home/smoke
  export AUDACITY_NO_UPDATE_CHECK=1
  xvfb-run -a -s '-screen 0 1600x1000x24' audacity4 >'$LOGS/audacity4.log' 2>&1 &
  APP=\$!
  for i in \$(seq 1 180); do
    if (exec 3<>/dev/tcp/127.0.0.1/2212) 2>/dev/null; then echo \"bridge up after \${i}s\"; break; fi
    if ! kill -0 \$APP 2>/dev/null; then echo 'audacity4 exited early'; tail -50 '$LOGS/audacity4.log'; exit 1; fi
    sleep 1
  done
  (exec 3<>/dev/tcp/127.0.0.1/2212) 2>/dev/null || { echo 'bridge did not come up'; tail -50 '$LOGS/audacity4.log'; exit 1; }
  RC=0
  SMOKE_DIR=/home/smoke/au4-smoke /opt/smoke-venv/bin/python '$HERE/smoke_test.py' || RC=\$?
  kill \$APP 2>/dev/null || true
  wait \$APP 2>/dev/null || true
  exit \$RC
" 2>&1 | tee "$LOGS/smoke.log"
exit "${PIPESTATUS[0]}"
