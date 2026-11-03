#!/usr/bin/env python3
"""End-to-end check of the MCP bridge of a running Audacity 4.

Uses the published Python client (pip package "audacity4-mcp", module server4) the
way an MCP server does, so a pass means: the Linux build starts, the bridge listens on
127.0.0.1:2212, the token file is found and accepted, and real editing commands work.

Steps: list the tools, make a test tone as WAV, import it, read the project back,
export it again and compare the exported file with the original.

Exit code 0 on success; every step prints one line.
"""
import asyncio
import json
import math
import os
import socket
import struct
import sys
import wave
from pathlib import Path

from server4.bridge_client import BridgeClient

WORK = Path(os.environ.get("SMOKE_DIR", Path.home() / "au4-smoke"))
RATE = 44100
SECONDS = 2.0


def step(msg: str) -> None:
    print(f"[smoke] {msg}", flush=True)


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


def list_tools(token: str) -> list[str]:
    """tools/list over the raw protocol (the client only wraps tools/call)."""
    with socket.create_connection(("127.0.0.1", 2212), timeout=30) as s:
        s.sendall((json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/list",
                               "token": token, "params": {}}) + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    resp = json.loads(buf)
    return [t["name"] for t in resp["result"]["tools"]]


async def main() -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    client = BridgeClient()
    token = client._resolve_token()  # same lookup an MCP server does
    step("token found")

    tools = list_tools(token)
    step(f"tools/list: {len(tools)} tools")
    for needed in ("mcp_project-import", "mcp_export-wav", "mcp_project-get-info"):
        if needed not in tools:
            raise SystemExit(f"tool missing: {needed}")

    src = WORK / "tone-in.wav"
    out = WORK / "tone-out.wav"
    write_tone(src)
    out.unlink(missing_ok=True)

    try:
        await client.call("project-new", {})
        step("project-new: ok")
    except RuntimeError as e:  # a project may already be open after start
        step(f"project-new: {e} (continuing with the open project)")

    res = await client.call("project-import", {"path": str(src)})
    step(f"project-import: {json.dumps(res)[:200]}")

    info = await client.call("project-get-info", {})
    step(f"project-get-info: {json.dumps(info)[:300]}")

    await client.call("select-all", {})
    res = await client.call("export-wav", {"path": str(out), "overwrite": True})
    step(f"export-wav: {json.dumps(res)[:200]}")

    for _ in range(60):
        if out.exists() and out.stat().st_size > 44:
            break
        await asyncio.sleep(0.5)
    if not out.exists():
        raise SystemExit("export-wav reported success but no file was written")

    got = wav_seconds(out)
    step(f"exported {out} ({got:.3f} s, expected {SECONDS:.3f} s)")
    if abs(got - SECONDS) > 0.05:
        raise SystemExit("exported length does not match the imported tone")

    await client.close()
    step("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
