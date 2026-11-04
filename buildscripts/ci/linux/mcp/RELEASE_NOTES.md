Audacity 4 (from [Audacity4-Dev-MCP](https://github.com/xDarkzx/Audacity4-Dev-MCP)) for **Debian 13 (trixie)**. It is built against the Qt the distribution ships (6.8) and has an MCP server, so AI assistants can drive it directly.

This package was built and tested by CI. A clean `debian:trixie` installed it, started it headless, and drove it over MCP: import, project info, select, WAV export, length check.

## Install

```sh
sudo apt install ./audacity4-mcp_*_amd64.deb
audacity4
```

- Audacity 4 is installed under `/opt/audacity4`, and the command is `audacity4`. The Debian package `audacity` (3.x) is not touched, so both can be installed.
- Qt and the QML modules come from Debian as dependencies.
- `audacity4-mcp-dbgsym` contains the debug information (`/usr/lib/debug/.build-id`). It is only needed for debugging.

## MCP

While Audacity runs, [libforge-mcp](https://git.forgeiec.io/ForgeIEC/forge-mcp) serves the Audacity commands as MCP tools (`mcp_project-import`, `mcp_export-wav`, `mcp_apply-effect`, …):

- URL: `http://127.0.0.1:7536/mcp/v1/audacity/rpc` (SSE: `…/audacity/sse`)
- Auth: `Authorization: Bearer <token>`, where the token is `mcp/access_token` in `~/.local/share/Audacity/Audacity4/mcp.ini`. It is created on first start.
- Settings in the same file: `mcp/enabled`, `mcp/port`, `mcp/access_slug`, `mcp/call_timeout_ms`

Claude Code example:

```sh
claude mcp add --transport http audacity4 http://127.0.0.1:7536/mcp/v1/audacity/rpc \
  --header "Authorization: Bearer $(sed -n 's/^access_token=//p' ~/.local/share/Audacity/Audacity4/mcp.ini)"
```

## Build it yourself

The workflow `.github/workflows/mcp_linux_debs.yml` runs in any fork on GitHub's `ubuntu-24.04` runner, inside a `debian:trixie` container. Set the repository variable `AU4_RUNS_ON` to use a self-hosted runner instead.
