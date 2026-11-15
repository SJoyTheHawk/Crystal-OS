#!/usr/bin/env python3
"""Create and serve the temporary HTTP fixtures used by Phase 17 board tests."""

from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
from pathlib import Path
from time import sleep


FIXTURE_ROOT = Path("/tmp")

HTTP_APP_YAML = """abi: 1
id: com.example.phase17.http
name: Phase 17 HTTP Test
version: 1.0.0
min_os: 0.17.0
author: Crystal OS test
license: MIT
runtime: lua
entry: main.lua
bar_style: dark
memory_kb: 160
storage_kb: 8
worker_threads: 1
permissions:
  network_hosts:
    - __MAC_HOST__
"""

HTTP_MAIN_LUA = '''local http = crystal.service.http
local base = "http://__MAC_HOST__:8000"
local M = {}

function M.onCreate()
  local id, err = http.fetch(base .. "/slow", { timeout_sec = 60 }, function(_, status, body)
    print("PHASE17_HTTP_CALLBACK", status, body)
    local cancel_id, cancel_err = http.fetch(base .. "/slow", { timeout_sec = 60 }, function()
      print("PHASE17_CANCEL_CALLBACK_UNEXPECTED")
    end)
    print("PHASE17_CANCEL_REQUEST", cancel_id or "nil", cancel_err or "")
  end)
  print("PHASE17_HTTP_STARTED", id or "nil", err or "")

  local extra, extra_err = http.fetch(base .. "/ok", {}, function()
    print("PHASE17_QUOTA_CALLBACK_UNEXPECTED")
  end)
  print("PHASE17_QUOTA", extra or "nil", extra_err or "")

  local denied, denied_err = http.fetch("http://example.invalid/ok", {}, function()
    print("PHASE17_ALLOWLIST_CALLBACK_UNEXPECTED")
  end)
  print("PHASE17_ALLOWLIST", denied or "nil", denied_err or "")
  return true
end

function M.onResume() return true end
function M.onPause() return true end
function M.onDestroy() return true end
return M
'''

WORKER_APP_YAML = """abi: 1
id: com.example.phase17.httpworker
name: Phase 17 HTTP Worker Check
version: 1.0.0
min_os: 0.17.0
author: Crystal OS test
license: MIT
runtime: lua
entry: main.lua
bar_style: dark
memory_kb: 160
storage_kb: 8
worker_threads: 1
permissions:
  network_hosts:
    - __MAC_HOST__
"""

WORKER_MAIN_LUA = '''local http = crystal.service.http
local M = {}

function M.onCreate()
  local id, err = http.fetch("http://__MAC_HOST__:8000/ok", {}, function(_, status, body)
    print("PHASE17_WORKER_RECOVERY_CALLBACK", status, body)
  end)
  print("PHASE17_WORKER_RECOVERY_STARTED", id or "nil", err or "")
  return true
end

function M.onResume() return true end
function M.onPause() return true end
function M.onDestroy() return true end
return M
'''


class Handler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        print(f"request {self.path}", flush=True)
        if self.path == "/slow":
            sleep(20)
        body = b'{"ok":true}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except BrokenPipeError:
            pass

    def log_message(self, _format: str, *args: object) -> None:
        del args


def create_fixtures(host: str) -> None:
    address = ipaddress.ip_address(host)
    if address.version != 4:
        raise ValueError("--host must be the Mac's LAN IPv4 address")

    packages = (
        ("phase17-http", HTTP_APP_YAML, HTTP_MAIN_LUA),
        ("phase17-http-worker-check", WORKER_APP_YAML, WORKER_MAIN_LUA),
    )
    for name, manifest, entry in packages:
        package_dir = FIXTURE_ROOT / name
        package_dir.mkdir(parents=True, exist_ok=True)
        (package_dir / "app.yaml").write_text(
            manifest.replace("__MAC_HOST__", str(address)), encoding="utf-8"
        )
        (package_dir / "main.lua").write_text(
            entry.replace("__MAC_HOST__", str(address)), encoding="utf-8"
        )
        print(f"Created {package_dir}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("serve", help="run the temporary HTTP server on port 8000")
    create = commands.add_parser("create", help="write both test packages under /tmp")
    create.add_argument("--host", required=True, help="Mac LAN IPv4 address")
    args = parser.parse_args()

    if args.command == "serve":
        print("Listening on port 8000; keep this terminal open.", flush=True)
        ThreadingHTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
    else:
        try:
            create_fixtures(args.host)
        except ValueError as error:
            parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
