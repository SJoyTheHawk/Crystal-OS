#!/usr/bin/env python3
"""Create and serve the bounded Bus Lite Phase 17 acceptance fixture."""
import argparse
import json
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / "examples" / "bus3p"
HOST_TOKEN = "phase17.bus.fixture"
OUTPUT = Path("/tmp/phase17-bus")

ROUTES = [
    {"route": "1A", "operator": "KMB", "origin": "Sau Mau Ping", "destination": "Star Ferry", "service_type": "1"},
    {"route": "2", "operator": "CTB", "origin": "Shau Kei Wan", "destination": "Central", "service_type": "1"},
    {"route": "68X", "operator": "KMB", "origin": "Yuen Long", "destination": "Hung Hom", "service_type": "1"},
]
STOPS = [
    {"id": "KMB-001", "seq": 1, "name_en": "Central Market", "name_tc": "中環街市"},
    {"id": "KMB-002", "seq": 2, "name_en": "City Hall", "name_tc": "大會堂"},
    {"id": "KMB-003", "seq": 3, "name_en": "Star Ferry", "name_tc": "天星碼頭"},
]
LONG_STOPS = [
    {"id": f"fixture-{index:03}", "seq": index,
     "name_en": f"68X Test Stop {index:02}", "name_tc": f"68X 測試站 {index:02}"}
    for index in range(1, 18)
]
ETAS = [
    {"minutes": 3, "destination": "Star Ferry", "remark": "Scheduled"},
    {"minutes": 11, "destination": "Star Ferry", "remark": "Scheduled"},
    {"minutes": 19, "destination": "Star Ferry", "remark": "Last bus information available"},
]


def response(path, query):
    """Deterministic sample data, never presented as real provider arrivals."""
    if path == "/api/routes":
        needle = query.get("query", [""])[0].upper()
        return {"routes": [dict(route, bound="O") for route in ROUTES
                           if route["route"].startswith(needle)]}
    route = next((row for row in ROUTES
                  if row["route"] == query.get("route", [""])[0]
                  and row["operator"] == query.get("operator", [""])[0]), None)
    if not route:
        return {"stops": [], "etas": []}
    source_stops = LONG_STOPS if route["route"] == "68X" else STOPS
    stops = [dict(stop, id=f"{route['operator']}-{route['route']}-{i + 1:03}")
             for i, stop in enumerate(source_stops)]
    if path == "/api/stops":
        return {"route": route["route"], "stops": stops}
    if path == "/api/eta":
        stop_id = query.get("stop", [""])[0]
        stop = next((row for row in stops if row["id"] == stop_id), None)
        return {"route": route["route"], "stop": stop_id, "etas": [
            dict(item, destination=route["destination"], minutes=item["minutes"] + stop["seq"] - 1,
                 remark="Fixture only - not live") for item in ETAS] if stop else []}
    return None


def create(host: str) -> None:
    if not host or any(ch not in "abcdefghijklmnopqrstuvwxyz0123456789-." for ch in host):
        raise SystemExit("host must be a lowercase DNS name or IPv4 address")
    destination = OUTPUT
    destination.mkdir(parents=True, exist_ok=True)
    for source in TEMPLATE.rglob("*"):
        relative = source.relative_to(TEMPLATE)
        if source.is_dir() or relative.name == "README.md":
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(source.read_text(encoding="utf-8").replace(HOST_TOKEN, host), encoding="utf-8")
    print(destination)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        print(f"request {parsed.path} {parsed.query}".rstrip(), flush=True)
        if parsed.path not in ("/api/routes", "/api/stops", "/api/eta"):
            self.send_error(404)
            return
        payload = response(parsed.path, query)
        body = json.dumps(payload, separators=(",", ":")).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def serve(port: int) -> None:
    print(f"Listening on port {port}; keep this terminal open.", flush=True)
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    create_parser = subparsers.add_parser("create")
    create_parser.add_argument("--host", required=True)
    serve_parser = subparsers.add_parser("serve")
    serve_parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    if args.command == "create":
        create(args.host)
    else:
        serve(args.port)


if __name__ == "__main__":
    main()
