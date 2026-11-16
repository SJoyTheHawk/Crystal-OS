#!/usr/bin/env python3
"""Build bounded route and stop assets for the P4 Lua Bus package.

The input files are deliberately supplied by the host update job.  This tool
does no network access: a catalog is reproducible from recorded provider
responses, and the response hashes are written into the package manifest.
Every JSON asset is kept below the firmware's 64 KiB whole-file read limit;
the default 32 KiB target also leaves room for decoding overhead.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import re
import shutil
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import quote


# Keep pages below the 32 KiB contract so one scheduler tick can decode a
# bounded number of JSON Lines without approaching the 50 ms Lua callback
# deadline. The package limit remains 32 KiB; this is a workload choice.
PAGE_BYTES = 16 * 1024
MAX_FILES = 128
MAX_PACKAGE_BYTES = 5_632 * 1024  # 5.5 MiB, the existing .capp limit


def read_json(path: Path) -> tuple[Any, bytes, str]:
    archive = path.read_bytes()
    raw = gzip.decompress(archive) if path.suffix == ".gz" else archive
    return json.loads(raw.decode("utf-8")), raw, hashlib.sha256(raw).hexdigest()


def text(value: Any, *keys: str) -> str:
    if not isinstance(value, dict):
        return ""
    for key in keys:
        item = value.get(key)
        if item is None:
            continue
        if isinstance(item, bool):
            continue
        if isinstance(item, (str, int, float)):
            return str(item).strip()
    return ""


def localized(value: Any, language: str, *fallback: str) -> str:
    if isinstance(value, dict):
        result = text(value, language, *fallback)
        if result:
            return result
    return ""


def provider_name(value: Any) -> str:
    return {"kmb": "KMB", "lwb": "LWB", "ctb": "CTB"}.get(
        str(value).strip().lower(), str(value).strip().upper()
    )


def bound_name(value: Any) -> str:
    value = str(value).strip().upper()
    return {"OUTBOUND": "O", "INBOUND": "I", "OUT": "O", "IN": "I"}.get(value, value)


def source_records(root: Any, requested: str) -> list[dict[str, Any]]:
    """Read official-style ``data`` or the recorded routeFareList shape."""
    requested = provider_name(requested)
    if isinstance(root, dict) and isinstance(root.get("data"), list):
        rows = root["data"]
    elif isinstance(root, dict) and isinstance(root.get("routeList"), dict):
        rows = list(root["routeList"].values())
    elif isinstance(root, dict) and isinstance(root.get("routeList"), list):
        rows = root["routeList"]
    elif isinstance(root, list):
        rows = root
    else:
        raise ValueError("route source must contain a data or routeList array/object")

    result: list[dict[str, Any]] = []
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("route source contains a non-object record")
        operators = row.get("co", row.get("operator", row.get("operator_code", requested)))
        if isinstance(operators, list):
            has_provider = requested.lower() in {str(item).lower() for item in operators}
        else:
            has_provider = provider_name(operators) == requested
        if not has_provider:
            continue

        bounds = row.get("bound")
        bound = bounds.get(requested.lower()) if isinstance(bounds, dict) else bounds
        bound = bound_name(bound or row.get("direction", row.get("bound_code", "O")))
        route = text(row, "route", "route_no", "routeNumber")
        if not route:
            raise ValueError(f"{requested}: route record has no route number")
        service_type = text(row, "service_type", "serviceType", "service") or "1"
        orig_en = text(row, "orig_en", "origin_en") or localized(row.get("orig"), "en", "name")
        orig_tc = text(row, "orig_tc", "origin_tc") or localized(row.get("orig"), "zh", "tc", "name")
        dest_en = text(row, "dest_en", "destination_en") or localized(row.get("dest"), "en", "name")
        dest_tc = text(row, "dest_tc", "destination_tc") or localized(row.get("dest"), "zh", "tc", "name")
        if not dest_en and not dest_tc:
            raise ValueError(f"{requested} {route}: route record has no destination")
        result.append(
            {
                "operator": requested,
                "route": route.upper(),
                "bound": bound,
                "service_type": service_type,
                "origin_en": orig_en,
                "origin_tc": orig_tc,
                "destination_en": dest_en,
                "destination_tc": dest_tc,
                "source_order": len(result),
            }
        )
    if not result:
        raise ValueError(f"route source contains no {requested} records")
    return result


def normalize_text(value: str) -> str:
    return re.sub(r"[^a-z0-9一-鿿]", "", value.lower())


def similar(left: str, right: str) -> bool:
    left, right = normalize_text(left), normalize_text(right)
    return bool(left and right) and (left == right or (
        len(left) >= 4 and len(right) >= 4 and (left in right or right in left)
    ))


def identity(part: dict[str, Any]) -> str:
    return "|".join(
        [part["operator"], part["route"], part["bound"], part["service_type"]]
    )


def public_part(part: dict[str, Any]) -> dict[str, str]:
    return {key: part[key] for key in (
        "operator", "route", "bound", "service_type", "origin_en", "origin_tc",
        "destination_en", "destination_tc",
    )}


def collapse_kmb_variants(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    seen: set[tuple[str, str, str]] = set()
    result = []
    for row in rows:
        if row["operator"] not in ("KMB", "LWB"):
            result.append(row)
            continue
        key = (row["operator"], row["route"], row["bound"])
        if key in seen:
            continue
        seen.add(key)
        result.append(row)
    return result


def merge_routes(kmb: list[dict[str, Any]], ctb: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Mirror Flutter RouteMerger's last-unused destination matching."""
    used: set[int] = set()
    result: list[dict[str, Any]] = []
    for row in kmb:
        match = -1
        for index, other in enumerate(ctb):
            if index in used or other["route"] != row["route"]:
                continue
            if similar(row["destination_en"], other["destination_en"]) or similar(
                row["destination_tc"], other["destination_tc"]
            ) or similar(row["destination_en"], other["destination_tc"]) or similar(
                row["destination_tc"], other["destination_en"]
            ):
                match = index
        if match < 0:
            result.append(row)
            continue
        used.add(match)
        other = ctb[match]
        result.append(
            {
                "operator": "JOINT",
                "route": row["route"],
                "bound": row["bound"],
                "service_type": row["service_type"],
                "origin_en": row["origin_en"],
                "origin_tc": row["origin_tc"],
                "destination_en": row["destination_en"],
                "destination_tc": row["destination_tc"],
                "parts": [public_part(row), public_part(other)],
                "source_order": row["source_order"],
            }
        )
    result.extend(row for index, row in enumerate(ctb) if index not in used)
    return result


def natural_key(value: str) -> tuple[Any, ...]:
    return tuple(int(part) if part.isdigit() else part.casefold() for part in re.split(r"(\d+)", value))


def route_records(kmb: list[dict[str, Any]], ctb: list[dict[str, Any]]) -> list[dict[str, Any]]:
    merged = merge_routes(collapse_kmb_variants(kmb), ctb)
    result = []
    for row in merged:
        record = {
            key: row[key]
            for key in (
                "operator", "route", "bound", "service_type", "origin_en", "origin_tc",
                "destination_en", "destination_tc",
            )
            if row.get(key, "") != ""
        }
        record["key"] = "|".join(
            ["JOINT", *(identity(part) for part in row["parts"])]
        ) if row.get("parts") else identity(row)
        if row.get("parts"):
            record["parts"] = row["parts"]
        result.append(record)
    return sorted(result, key=lambda row: (natural_key(row["route"]), row["operator"], row["bound"], row["key"]))


def coordinate(value: Any) -> float | None:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if result == result and abs(result) != float("inf") else None


def make_stop(stop_id: str, item: Any) -> dict[str, Any]:
    if not isinstance(item, dict):
        raise ValueError("stop record must be an object")
    names = item.get("name") if isinstance(item.get("name"), dict) else item
    location = item.get("location") if isinstance(item.get("location"), dict) else item
    en = text(item, "name_en", "nameEn") or localized(names, "en", "name")
    tc = text(item, "name_tc", "nameTc") or localized(names, "zh", "tc", "name")
    if not stop_id or not en and not tc:
        raise ValueError(f"stop lacks id/name: {item!r}")
    return {
        "id": str(stop_id),
        "name_en": en,
        "name_tc": tc,
        "lat": coordinate(text(location, "lat", "latitude")),
        "lon": coordinate(text(location, "long", "lon", "lng", "longitude")),
    }


def stop_records(root: Any, provider: str) -> tuple[list[dict[str, Any]], dict[str, int]]:
    provider = provider_name(provider)
    gaps = {"routes_without_metadata": 0, "missing_records": 0}
    if isinstance(root, dict) and isinstance(root.get("data"), list):
        records = []
        for row in root["data"]:
            sid = text(row, "stop", "stopId", "stop_id", "id")
            if sid:
                records.append(make_stop(sid, row))
        if not records:
            raise ValueError(f"{provider} stop source has no records")
        return records, gaps
    if not isinstance(root, dict):
        raise ValueError(f"{provider} stop source must be an object")

    stop_list = root.get("stopList")
    stop_map = root.get("stopMap")
    if provider == "CTB" and isinstance(stop_list, dict):
        # routeFareList.stopList is a shared map.  Keep only entries that are
        # actually reachable through a CTB route and translate the shared key
        # to the operator's public stop ID.  Including every shared KMB alias
        # here would make the CTB catalog appear complete while returning
        # unusable IDs to the CTB endpoint.
        referenced: set[str] = set()
        routes = root.get("routeList", {})
        rows = routes.values() if isinstance(routes, dict) else routes
        for route in rows if isinstance(rows, Iterable) else []:
            if not isinstance(route, dict):
                continue
            operators = route.get("co", [])
            if "ctb" not in ({str(item).lower() for item in operators} if isinstance(operators, list) else {str(operators).lower()}):
                continue
            route_stops = route.get("stops", {})
            route_stops = route_stops.get("ctb") if isinstance(route_stops, dict) else route_stops
            if isinstance(route_stops, list):
                referenced.update(str(item) for item in route_stops)
        records = []
        for unified_id in sorted(referenced):
            metadata = stop_list.get(unified_id)
            if metadata is None:
                gaps["missing_records"] += 1
                continue
            # Preserve the ID used by the route-stop source.  Some archived
            # rows have a shared key and some have an operator ID; changing
            # it through stopMap would merge distinct occurrences.  The
            # selected provider remains the authority for the live sequence.
            records.append(make_stop(str(unified_id), metadata))
        if not records:
            raise ValueError("CTB stopList has no records")
        return records, gaps
    raise ValueError(f"cannot read {provider} stop records from source")


def json_bytes(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def write_pages(output: Path, prefix: str, records: list[dict[str, Any]], key: str) -> tuple[list[str], list[dict[str, Any]]]:
    pages: list[str] = []
    descriptors: list[dict[str, Any]] = []
    current: list[dict[str, Any]] = []

    def payload(items: list[dict[str, Any]]) -> bytes:
        # JSON Lines keeps each decoded record below the firmware's 512-node
        # conversion cap while retaining a 32 KiB page target. The Lua reader
        # decodes one line, copies the matching record, and releases it before
        # decoding the next line.
        return b"".join(json_bytes(item) for item in items)

    def flush() -> None:
        if not current:
            return
        name = f"{prefix}-{len(pages):03d}.jsonl"
        data = payload(current)
        if len(data) > PAGE_BYTES:
            raise ValueError(f"{name} exceeds {PAGE_BYTES} bytes")
        (output / name).write_bytes(data)
        pages.append(name)
        descriptors.append({"first": current[0][key], "last": current[-1][key], "file": name, "count": len(current)})

    for record in records:
        candidate = current + [record]
        if current and len(payload(candidate)) > PAGE_BYTES:
            flush()
            current = []
        if len(payload([record])) > PAGE_BYTES:
            raise ValueError(f"single {prefix} record exceeds {PAGE_BYTES} bytes")
        current.append(record)
    flush()
    return pages, descriptors


def compact_route_line(record: dict[str, Any]) -> bytes:
    parts = ";".join(":".join(str(part.get(key, "")) for key in
                              ("operator", "route", "bound", "service_type"))
                       for part in record.get("parts", []))
    fields = [record.get("route", ""), record.get("operator", ""),
              record.get("bound", ""), record.get("service_type", ""),
              record.get("destination_en", ""), record.get("key", "")]
    if parts:
        fields.append(parts)
    return ("\t".join(quote(str(value), safe="") for value in fields) + "\n").encode()


def write_route_search_pages(output: Path, routes: list[dict[str, Any]]) -> list[str]:
    """Write a compact startup index without requiring cJSON conversion."""
    pages: list[str] = []
    current: list[bytes] = []
    current_size = 0
    for record in routes:
        line = compact_route_line(record)
        if len(line) > PAGE_BYTES:
            raise ValueError("compact route record exceeds page limit")
        if current and current_size + len(line) > PAGE_BYTES:
            name = f"route-search-{len(pages):03d}.txt"
            (output / name).write_bytes(b"".join(current))
            pages.append(name)
            current, current_size = [], 0
        current.append(line)
        current_size += len(line)
    if current:
        name = f"route-search-{len(pages):03d}.txt"
        (output / name).write_bytes(b"".join(current))
        pages.append(name)
    return pages


def write_runtime_directory(output: Path, letters: list[str], route_count: int,
                            search_pages: list[str]) -> None:
    """Small text directory: no provenance JSON conversion on app launch."""
    lines = ["L " + "".join(letters), "C " + str(route_count)]
    prefixes: dict[str, set[str]] = {}
    for name in search_pages:
        raw = (output / name).read_bytes()
        records = raw.decode().splitlines()
        lines.append(f"Q {name} {len(raw)} {len(records)}")
        for record in records:
            route = record.split("\t", 1)[0]
            for index, character in enumerate(route):
                prefixes.setdefault(route[:index], set()).add(character)
    prefix_data = ("\n" + "".join(
        prefix + "\t" + "".join(sorted(keys)) + "\n"
        for prefix, keys in sorted(prefixes.items()))).encode()
    if len(prefix_data) > PAGE_BYTES:
        raise ValueError("keypad prefix index exceeds catalog page limit")
    (output / "keypad.txt").write_bytes(prefix_data)
    lines.append(f"K keypad.txt {len(prefix_data)}")
    for path in sorted(output.glob("routes-*.jsonl")):
        initials = sorted({json.loads(line)["route"][0] for line in path.read_text().splitlines()})
        lines.append("R " + path.name + " " + "".join(initials))
    for provider in ("KMB", "CTB"):
        for path in sorted(output.glob(f"stops-{provider.lower()}-*.jsonl")):
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            lines.append("S " + provider + " " + path.name + " " + rows[0]["id"] + " " + rows[-1]["id"])
    data = ("\n".join(lines) + "\n").encode()
    if len(data) > PAGE_BYTES:
        raise ValueError("runtime directory exceeds catalog page limit")
    (output / "runtime.txt").write_bytes(data)


def write_descriptor_pages(output: Path, prefix: str, descriptors: list[dict[str, Any]]) -> list[str]:
    pages: list[str] = []
    current: list[dict[str, Any]] = []
    for descriptor in descriptors:
        candidate = json_bytes({"pages": current + [descriptor]})
        if current and len(candidate) > PAGE_BYTES:
            name = f"{prefix}-{len(pages):03d}.json"
            (output / name).write_bytes(json_bytes({"pages": current}))
            pages.append(name)
            current = []
        if len(json_bytes({"pages": [descriptor]})) > PAGE_BYTES:
            raise ValueError(f"descriptor exceeds {PAGE_BYTES} bytes")
        current.append(descriptor)
    if current:
        name = f"{prefix}-{len(pages):03d}.json"
        (output / name).write_bytes(json_bytes({"pages": current}))
        pages.append(name)
    return pages


def source_info(
    path: Path,
    raw: bytes,
    sha: str,
    label: str | None = None,
    timestamp: str | None = None,
) -> dict[str, Any]:
    return {
        "file": path.name,
        "label": label or path.name,
        "timestamp": timestamp,
        "bytes": len(raw),
        "sha256": sha,
        "archive_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def build(args: argparse.Namespace) -> dict[str, Any]:
    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)

    kmb_root, kmb_raw, kmb_sha = read_json(args.kmb_routes)
    ctb_root, ctb_raw, ctb_sha = read_json(args.ctb_routes)
    kmb = source_records(kmb_root, "KMB")
    ctb = source_records(ctb_root, "CTB")
    routes = route_records(kmb, ctb)
    route_letters = sorted({character for row in routes for character in row["route"] if character.isalpha()})
    route_pages, route_desc = write_pages(args.output, "routes", routes, "route")
    route_search_pages = write_route_search_pages(args.output, routes)
    route_index_pages = write_descriptor_pages(args.output, "route-index", route_desc)

    stops_summary: dict[str, Any] = {}
    stop_page_names: list[str] = []
    stop_index_names: list[str] = []
    for provider, path in (("KMB", args.kmb_stops), ("CTB", args.ctb_stops)):
        if path is None:
            stops_summary[provider] = {"records": 0, "missing_source": True}
            continue
        root, raw, sha = read_json(path)
        stops, gaps = stop_records(root, provider)
        stops = sorted({row["id"]: row for row in stops}.values(), key=lambda row: row["id"])
        pages, descriptors = write_pages(args.output, f"stops-{provider.lower()}", stops, "id")
        index_pages = write_descriptor_pages(args.output, f"stop-index-{provider.lower()}", descriptors)
        stop_page_names.extend(pages)
        stop_index_names.extend(index_pages)
        stops_summary[provider] = {
            "records": len(stops),
            "pages": pages,
            "index_pages": index_pages,
            "gaps": gaps,
            "source": source_info(
                path,
                raw,
                sha,
                timestamp=getattr(args, f"{provider.lower()}_stop_timestamp", None),
            ),
        }

    manifest = {
        "schema": "crystal-bus-catalog-v1",
        "generated_at": args.generated_at,
        "source_revision": args.source_revision,
        "routes": {
            "records": len(routes),
            "pages": route_pages,
            "search_pages": route_search_pages,
            "index_pages": route_index_pages,
            "kmb_records_before_merge": len(kmb),
            "ctb_records_before_merge": len(ctb),
            "joint_records": sum(1 for row in routes if row["operator"] == "JOINT"),
            "letters": route_letters,
            "sources": {
                "KMB": source_info(
                    args.kmb_routes, kmb_raw, kmb_sha, args.kmb_route_label,
                    getattr(args, "kmb_route_timestamp", None),
                ),
                "CTB": source_info(
                    args.ctb_routes, ctb_raw, ctb_sha, args.ctb_route_label,
                    getattr(args, "ctb_route_timestamp", None),
                ),
            },
        },
        "stops": stops_summary,
        "gaps": {
            "route_stop_sequences": "fetched from the selected provider at runtime",
            "missing_stop_metadata": [provider for provider, summary in stops_summary.items() if summary.get("missing_source")],
        },
    }
    write_runtime_directory(args.output, route_letters, len(routes), route_search_pages)
    manifest_data = json_bytes(manifest)
    if len(manifest_data) > PAGE_BYTES:
        raise ValueError("manifest exceeds 32 KiB")
    (args.output / "manifest.json").write_bytes(manifest_data)

    files = sorted(path for path in args.output.rglob("*") if path.is_file())
    largest = max((path.stat().st_size for path in files), default=0)
    total = sum(path.stat().st_size for path in files)
    if largest > PAGE_BYTES:
        raise ValueError(f"catalog file exceeds {PAGE_BYTES} bytes")
    if len(files) > args.max_files:
        raise ValueError(f"catalog has {len(files)} files; limit is {args.max_files}")
    # The manifest is itself part of the catalog.  Its limits section changes
    # the manifest length, so write/recount until the recorded total includes
    # the final manifest bytes rather than the provisional one.
    limits = {
        "file_count": len(files),
        "total_bytes": total,
        "largest_file_bytes": largest,
        "max_file_bytes": PAGE_BYTES,
        "max_files": args.max_files,
        "max_package_bytes": args.max_package_bytes,
    }
    for _ in range(3):
        manifest["limits"] = limits
        (args.output / "manifest.json").write_bytes(json_bytes(manifest))
        if (args.output / "manifest.json").stat().st_size > PAGE_BYTES:
            raise ValueError("manifest exceeds 32 KiB after limits were added")
        files = sorted(path for path in args.output.rglob("*") if path.is_file())
        new_total = sum(path.stat().st_size for path in files)
        new_largest = max((path.stat().st_size for path in files), default=0)
        new_limits = {**limits, "file_count": len(files), "total_bytes": new_total, "largest_file_bytes": new_largest}
        if new_limits == limits:
            break
        limits = new_limits
    if limits["file_count"] > args.max_files:
        raise ValueError(f"catalog has {limits['file_count']} files; limit is {args.max_files}")
    if limits["total_bytes"] > args.max_package_bytes:
        raise ValueError(f"catalog is {limits['total_bytes']} bytes; package limit is {args.max_package_bytes}")
    if limits["largest_file_bytes"] > PAGE_BYTES:
        raise ValueError(f"catalog file exceeds {PAGE_BYTES} bytes")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kmb-routes", type=Path, required=True)
    parser.add_argument("--ctb-routes", type=Path, required=True)
    parser.add_argument("--kmb-stops", type=Path)
    parser.add_argument("--ctb-stops", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--generated-at", default="1970-01-01T00:00:00Z")
    parser.add_argument("--source-revision", default="recorded-input")
    parser.add_argument("--kmb-route-label")
    parser.add_argument("--ctb-route-label")
    parser.add_argument("--kmb-route-timestamp")
    parser.add_argument("--ctb-route-timestamp")
    parser.add_argument("--kmb-stop-timestamp")
    parser.add_argument("--ctb-stop-timestamp")
    parser.add_argument("--max-files", type=int, default=MAX_FILES)
    parser.add_argument("--max-package-bytes", type=int, default=MAX_PACKAGE_BYTES)
    args = parser.parse_args()
    try:
        manifest = build(args)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
