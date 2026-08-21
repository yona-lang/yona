#!/usr/bin/env python3
"""Subprocess smoke for yls-yona: --help plus initialize/didOpen/shutdown over stdio."""

from __future__ import annotations

import json
import os
import subprocess
import sys


def encode(msg: dict) -> bytes:
    body = json.dumps(msg, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def read_message(proc: subprocess.Popen[bytes]) -> dict:
    headers = b""
    while b"\r\n\r\n" not in headers:
        chunk = proc.stdout.read(1)
        if not chunk:
            raise SystemExit("yls-yona closed stdout before a complete LSP header")
        headers += chunk
    header_text = headers.decode("ascii", errors="replace")
    length = None
    for line in header_text.split("\r\n"):
        if line.lower().startswith("content-length:"):
            length = int(line.split(":", 1)[1].strip())
    if length is None:
        raise SystemExit(f"yls-yona response missing Content-Length: {header_text!r}")
    body = proc.stdout.read(length)
    if len(body) != length:
        raise SystemExit("yls-yona closed stdout before a complete LSP body")
    return json.loads(body.decode("utf-8"))


def read_response(proc: subprocess.Popen[bytes], expected_id: int) -> dict:
    """Read framed messages until the JSON-RPC response with expected_id."""
    for _ in range(8):
        msg = read_message(proc)
        if msg.get("id") == expected_id:
            return msg
        if "method" in msg:
            continue
        raise SystemExit(f"unexpected message while waiting for id {expected_id}: {msg}")
    raise SystemExit(f"no response with id {expected_id}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: smoke-yls-yona.py <path-to-yls-yona>", file=sys.stderr)
        return 2
    yls = sys.argv[1]
    if not os.path.isfile(yls):
        print(f"yls-yona not found: {yls}", file=sys.stderr)
        return 1

    help_run = subprocess.run(
        [yls, "--help"],
        check=False,
        capture_output=True,
        text=True,
    )
    if help_run.returncode != 0:
        print(help_run.stderr or help_run.stdout, file=sys.stderr)
        return help_run.returncode
    if "language server" not in (help_run.stdout + help_run.stderr).lower():
        print("yls-yona --help did not mention the language server", file=sys.stderr)
        return 1

    payload = (
        encode(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"capabilities": {}},
            }
        )
        + encode({"jsonrpc": "2.0", "method": "initialized", "params": {}})
        + encode(
            {
                "jsonrpc": "2.0",
                "method": "textDocument/didOpen",
                "params": {
                    "textDocument": {
                        "uri": "file:///tmp/smoke.yona",
                        "languageId": "yona",
                        "version": 1,
                        "text": "let x = 1 in x\n",
                    }
                },
            }
        )
        + encode(
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "textDocument/hover",
                "params": {
                    "textDocument": {"uri": "file:///tmp/smoke.yona"},
                    "position": {"line": 0, "character": 4},
                },
            }
        )
        + encode({"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": None})
        + encode({"jsonrpc": "2.0", "method": "exit"})
    )
    proc = subprocess.Popen(
        [yls, "--stdio"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.stdin is None or proc.stdout is None:
        raise SystemExit("yls-yona subprocess pipes were not created")
    proc.stdin.write(payload)
    proc.stdin.close()

    init = read_response(proc, 1)
    result = init.get("result") or {}
    caps = result.get("capabilities") or {}
    if caps.get("textDocumentSync") != 1:
        print(f"initialize missing textDocumentSync=1: {init}", file=sys.stderr)
        proc.kill()
        return 1
    if caps.get("hoverProvider") is True:
        print("yls-yona must not claim hoverProvider (C++ yls remains the editor default)", file=sys.stderr)
        proc.kill()
        return 1
    info = result.get("serverInfo") or {}
    if info.get("name") != "yls-yona":
        print(f"initialize serverInfo.name should be yls-yona: {init}", file=sys.stderr)
        proc.kill()
        return 1

    hover = read_response(proc, 3)
    if hover.get("result") is not None:
        print(f"unknown/unimplemented method should return null: {hover}", file=sys.stderr)
        proc.kill()
        return 1

    shutdown = read_response(proc, 2)
    if shutdown.get("id") != 2:
        print(f"unexpected shutdown response: {shutdown}", file=sys.stderr)
        proc.kill()
        return 1
    if shutdown.get("result") is not None:
        print(f"shutdown result should be null: {shutdown}", file=sys.stderr)
        proc.kill()
        return 1

    code = proc.wait(timeout=10)
    if code != 0:
        err = (proc.stderr.read() if proc.stderr else b"").decode("utf-8", errors="replace")
        print(f"yls-yona exited {code}: {err}", file=sys.stderr)
        return code or 1
    print("yls-yona subprocess smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
