#!/usr/bin/env python3
"""Build deterministic compact provider stop catalogs from saved source JSON.

The converter intentionally has no network dependency. Downloading and pinning
inputs belongs to the host update job; the input hashes are recorded in the
manifest so an artifact cannot be mistaken for a live provider snapshot.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import struct
from pathlib import Path
from typing import Any

MAGIC = b"BSC2"
VERSION = 2
HEADER = struct.Struct("<4sHBBII")
ENTRY = struct.Struct("<IIIIIIffB3x")


def read_json(path: Path) -> tuple[Any, bytes, str]:
    raw = gzip.decompress(path.read_bytes()) if path.suffix == ".gz" else path.read_bytes()
    return json.loads(raw.decode("utf-8")), raw, hashlib.sha256(raw).hexdigest()


def text(obj: Any, *keys: str) -> str:
    if not isinstance(obj, dict):
        return ""
    for key in keys:
        value = obj.get(key)
        if isinstance(value, str):
            return value
        if isinstance(value, (int, float)):
            return str(value)
    return ""


def coordinate(obj: Any, *keys: str) -> float | None:
    value = text(obj, *keys)
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if result == result and abs(result) != float("inf") else None


def make_record(provider: str, obj: Any, stop_id: str | None = None) -> dict[str, Any]:
    if not isinstance(obj, dict):
        raise ValueError(f"{provider}: stop record must be an object")
    sid = stop_id or text(obj, "stop", "stopId", "stop_id", "id")
    names = obj.get("name") if isinstance(obj.get("name"), dict) else obj
    location = obj.get("location") if isinstance(obj.get("location"), dict) else obj
    en = text(obj, "name_en", "nameEn") or text(names, "en", "name")
    tc = text(obj, "name_tc", "nameTc") or text(names, "zh", "tc")
    lat = coordinate(location, "lat", "latitude")
    lon = coordinate(location, "long", "lon", "lng", "longitude")
    if not sid or not en and not tc:
        raise ValueError(f"{provider}: stop record lacks id and localized name: {obj!r}")
    if len(sid.encode("utf-8")) > 31:
        raise ValueError(f"{provider}: stop id exceeds 31 UTF-8 bytes: {sid!r}")
    return {"id": sid, "name_en": en, "name_tc": tc, "lat": lat, "lon": lon}


def kmb_records(root: Any) -> list[dict[str, Any]]:
    data = root.get("data") if isinstance(root, dict) else root
    if not isinstance(data, list):
        raise ValueError("KMB bulk source must contain a data array")
    return [make_record("KMB", item) for item in data]


def find_stop_map(root: Any) -> dict[str, Any]:
    if isinstance(root, dict):
        for key in ("stopList", "stop_list", "stops"):
            candidate = root.get(key)
            if isinstance(candidate, dict):
                return candidate
            if isinstance(candidate, list):
                result = {}
                for item in candidate:
                    sid = text(item, "stop", "stopId", "stop_id", "id")
                    if sid:
                        result[sid] = item
                if result:
                    return result
        for child in root.values():
            found = find_stop_map(child)
            if found:
                return found
    return {}


def ctb_records(root: Any) -> tuple[list[dict[str, Any]], int, int]:
    if not isinstance(root, dict):
        raise ValueError("CTB snapshot must be an object")
    routes = root.get("routeList", root.get("routes"))
    if isinstance(routes, dict):
        routes = list(routes.values())
    if not isinstance(routes, list):
        raise ValueError("CTB snapshot must contain routeList or routes with explicit operators")
    referenced_ids = set()
    ctb_routes = 0
    routes_without_ctb_stops = 0
    for route in routes:
        if not isinstance(route, dict):
            raise ValueError("CTB snapshot contains a malformed route")
        operators = route.get("co")
        has_ctb = ("ctb" in operators if isinstance(operators, list)
                   else isinstance(operators, str) and operators.lower() == "ctb")
        if not has_ctb:
            continue
        ctb_routes += 1
        stops = route.get("stops")
        if isinstance(stops, dict):
            stops = stops.get("ctb")
            if stops is None:
                routes_without_ctb_stops += 1
                continue
        if not isinstance(stops, list):
            raise ValueError("CTB route lacks a stops array")
        for item in stops:
            sid = text(item, "stop", "stopId", "stop_id", "id") if isinstance(item, dict) else item
            if not isinstance(sid, str) or not sid:
                raise ValueError("CTB route contains an invalid stop ID")
            referenced_ids.add(sid)
    if ctb_routes == 0 or not referenced_ids:
        raise ValueError("CTB snapshot has no CTB stop references")
    referenced = sorted(referenced_ids, key=lambda sid: sid.encode("utf-8"))
    stop_map = find_stop_map(root)
    missing = [sid for sid in referenced if sid not in stop_map]
    if missing:
        raise ValueError(f"CTB snapshot is incomplete; missing {len(missing)} referenced stop records: {missing[:5]}")
    return ([make_record("CTB", stop_map[sid], sid) for sid in referenced],
            len(referenced), routes_without_ctb_stops)


def encode(provider: str, records: list[dict[str, Any]]) -> tuple[bytes, dict[str, Any]]:
    records = sorted(records, key=lambda item: item["id"].encode("utf-8"))
    if any(a["id"] == b["id"] for a, b in zip(records, records[1:])):
        raise ValueError(f"{provider}: duplicate stop IDs")
    pool = bytearray()
    entries = bytearray()
    max_fields = {"id": 0, "name_en": 0, "name_tc": 0}
    for item in records:
        offsets = []
        for field in ("id", "name_en", "name_tc"):
            encoded = item[field].encode("utf-8")
            max_fields[field] = max(max_fields[field], len(encoded))
            offsets.append((len(pool), len(encoded)))
            pool.extend(encoded)
        lat = item["lat"] if item["lat"] is not None and -90 <= item["lat"] <= 90 else 0.0
        lon = item["lon"] if item["lon"] is not None and -180 <= item["lon"] <= 180 else 0.0
        has_coords = item["lat"] is not None and item["lon"] is not None and (lat != 0.0 or lon != 0.0)
        entries.extend(ENTRY.pack(offsets[0][0], offsets[0][1], offsets[1][0], offsets[1][1], offsets[2][0], offsets[2][1], lat, lon, int(has_coords)))
    header = HEADER.pack(MAGIC, VERSION, 1 if provider == "KMB" else 2, 0, len(records), len(pool))
    return header + entries + pool, {"records": len(records), "pool_bytes": len(pool), "max_utf8_bytes": max_fields, "index_bytes": len(entries)}


def build(provider: str, source: Path, output: Path) -> dict[str, Any]:
    root, raw, source_sha = read_json(source)
    records, coverage, routes_without_ctb_stops = (
        (kmb_records(root), None, None) if provider == "KMB" else ctb_records(root))
    artifact, stats = encode(provider, records)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(artifact)
    stats.update({"provider": provider, "artifact_bytes": len(artifact),
                  "source_bytes": len(raw), "source_sha256": source_sha,
                  "source_revision": f"sha256:{source_sha}"})
    if source.suffix == ".gz":
        archive = source.read_bytes()
        stats.update({"source_archive": source.name,
                      "source_archive_bytes": len(archive),
                      "source_archive_sha256": hashlib.sha256(archive).hexdigest()})
    if coverage is not None:
        stats["ctb_route_referenced_ids"] = coverage
        stats["ctb_routes_without_ctb_stops"] = routes_without_ctb_stops
    if provider == "KMB" and isinstance(root, dict):
        stats["source_time"] = root.get("generated_timestamp")
    return stats


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kmb", type=Path, required=True)
    parser.add_argument("--ctb", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--generated-at", default="1970-01-01T00:00:00Z")
    parser.add_argument("--source-revision", default="pinned-input")
    parser.add_argument("--ctb-publication-time", default=None)
    args = parser.parse_args()
    stats = {"schema": "crystal-stop-catalog-v2", "generated_at": args.generated_at, "source_revision": args.source_revision, "providers": {}}
    for provider, source in (("KMB", args.kmb), ("CTB", args.ctb)):
        output = args.out / f"{provider.lower()}-stops.bsc"
        provider_stats = build(provider, source, output)
        provider_stats["artifact_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
        provider_stats["artifact"] = output.name
        provider_stats["source_url"] = (
            "https://data.etabus.gov.hk/v1/transport/kmb/stop"
            if provider == "KMB" else
            "https://data.hkbus.app/routeFareList.min.json")
        provider_stats["attribution"] = (
            "Kowloon Motor Bus Company (1933) Limited; data.gov.hk"
            if provider == "KMB" else "HK Bus Crawling contributors")
        if provider == "CTB":
            provider_stats["source_time"] = None
            provider_stats["publication_last_modified"] = args.ctb_publication_time
        stats["providers"][provider] = provider_stats
    (args.out / "manifest.json").write_text(json.dumps(stats, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(stats, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
