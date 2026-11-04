#!/usr/bin/env python3
"""End-to-end check of the MCP server (libforge-mcp) of a running Audacity 4.

Talks plain MCP over HTTP/JSON-RPC the way an MCP client does, so a pass means:
the build starts, libforge-mcp listens, the bearer token from mcp.ini is
accepted, the Audacity commands are offered as tools and really edit a project.

Steps: initialize, tools/list, make a test tone as WAV, import it, read the
project back, export it again and compare the exported file with the original.
Only the standard library is used. Exit code 0 on success; one line per step.
"""
import configparser
import json
import math
import os
import struct
import sys
import time
import urllib.request
import wave
from pathlib import Path

WORK = Path(os.environ.get("SMOKE_DIR", Path.home() / "au4-smoke"))
DATA_HOME = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
PORT_DEFAULT = 7536          # McpModuleContext: mcp/port
SLUG_DEFAULT = "audacity"    # AudacityMcpHost: mcp/access_slug
RATE = 44100
SECONDS = 2.0


def step(msg: str) -> None:
    print(f"[smoke] {msg}", flush=True)


def settings() -> tuple[str, int, str]:
    """Token, port and slug from the mcp.ini the app wrote on start."""
    inis = sorted(DATA_HOME.rglob("mcp.ini"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not inis:
        raise SystemExit(f"no mcp.ini below {DATA_HOME}")
    cp = configparser.ConfigParser()
    cp.read(inis[0])
    token = cp.get("mcp", "access_token", fallback="")
    if not token:
        raise SystemExit(f"no mcp/access_token in {inis[0]}")
    port = cp.getint("mcp", "port", fallback=PORT_DEFAULT)
    slug = cp.get("mcp", "access_slug", fallback=SLUG_DEFAULT)
    step(f"settings: {inis[0]} (port {port}, slug {slug})")
    return token, port, slug


class Client:
    def __init__(self, token: str, port: int, slug: str):
        self.url = f"http://127.0.0.1:{port}/mcp/v1/{slug}/rpc"
        self.token = token
        self.id = 0

    def rpc(self, method: str, params: dict | None = None) -> dict:
        self.id += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.id, "method": method,
                           "params": params or {}}).encode()
        req = urllib.request.Request(self.url, data=body, headers={
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "Authorization": f"Bearer {self.token}"})
        with urllib.request.urlopen(req, timeout=120) as r:
            resp = json.loads(r.read())
        if "error" in resp:
            raise SystemExit(f"{method}: {resp['error']}")
        return resp["result"]

    def call(self, tool: str, args: dict | None = None) -> dict:
        """tools/call; returns the forge envelope ({ok, data} / {ok, code, message})."""
        result = self.rpc("tools/call", {"name": tool, "arguments": args or {}})
        text = "".join(c.get("text", "") for c in result.get("content", []))
        try:
            env = json.loads(text)
        except json.JSONDecodeError:
            env = {"ok": not result.get("isError", False), "data": text}
        return env


def write_tone(path: Path) -> None:
    frames = int(RATE * SECONDS)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(
            struct.pack("<h", int(12000 * math.sin(2 * math.pi * 440 * i / RATE)))
            for i in range(frames)))


def wav_seconds(path: Path) -> float:
    with wave.open(str(path), "rb") as w:
        return w.getnframes() / w.getframerate()


def expect_ok(name: str, env: dict) -> dict:
    step(f"{name}: {json.dumps(env)[:240]}")
    if not env.get("ok"):
        raise SystemExit(f"{name} failed")
    return env.get("data") or {}


def main() -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    c = Client(*settings())

    init = c.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                "clientInfo": {"name": "au4-smoke", "version": "1"}})
    step(f"initialize: {json.dumps(init.get('serverInfo'))}")

    names = [t["name"] for t in c.rpc("tools/list")["tools"]]
    step(f"tools/list: {len(names)} tools, {sum(n.startswith('mcp_') for n in names)} Audacity commands")
    for needed in ("mcp_project-import", "mcp_export-wav", "mcp_project-get-info", "mcp_select-all"):
        if needed not in names:
            raise SystemExit(f"tool missing: {needed}")

    src, out = WORK / "tone-in.wav", WORK / "tone-out.wav"
    write_tone(src)
    out.unlink(missing_ok=True)

    expect_ok("project-import", c.call("mcp_project-import", {"path": str(src)}))
    info = expect_ok("project-get-info", c.call("mcp_project-get-info"))
    duration = (info.get("result") or {}).get("durationSec")
    if duration is not None and abs(float(duration) - SECONDS) > 0.05:
        raise SystemExit(f"project duration {duration} s after import, expected {SECONDS} s")

    expect_ok("select-all", c.call("mcp_select-all"))
    expect_ok("export-wav", c.call("mcp_export-wav", {"path": str(out), "overwrite": True}))

    for _ in range(60):
        if out.exists() and out.stat().st_size > 44:
            break
        time.sleep(0.5)
    if not out.exists():
        raise SystemExit("export-wav reported success but no file was written")
    got = wav_seconds(out)
    step(f"exported {out} ({got:.3f} s, expected {SECONDS:.3f} s)")
    if abs(got - SECONDS) > 0.05:
        raise SystemExit("exported length does not match the imported tone")

    step("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
