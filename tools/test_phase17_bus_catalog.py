#!/usr/bin/env python3
"""Deterministic host checks for the Stage B catalog builder."""

from __future__ import annotations

import json
import tempfile
from pathlib import Path

import build_phase17_bus_catalog as catalog


def write(path: Path, value: object) -> Path:
    path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
    return path


def main() -> None:
    # Service 2 occurring first must never displace the standard variant.
    base = {"route": "101", "bound": "O", "orig_en": "East", "dest_en": "West"}
    stats = {}
    standard = catalog.source_records({"data": [
        {**base, "service_type": "2"},
        {**base, "service_type": 1},
        {**base, "route": "999", "service_type": "3"},
        {**base, "co": "LWB", "route": "A31", "service_type": "1"},
    ]}, "KMB", stats)
    assert [row["route"] for row in standard] == ["101", "A31"]
    assert standard[1]["operator"] == "LWB"
    assert all(row["service_type"] == "1" for row in standard)
    assert stats == {"source_records": 4, "excluded_nonstandard": 2, "normalized_records": 2}
    try:
        catalog.source_records({"data": [base]}, "KMB")
    except ValueError as error:
        assert "missing service_type" in str(error)
    else:
        raise AssertionError("missing KMB service was silently treated as standard")
    assert catalog.source_records({"data": [{**base, "service_type": "2"}]}, "KMB") == []
    ctb = catalog.source_records({"data": [
        {"company_id": "CTB", "route": "101", "orig_en": "East", "dest_en": "West"},
    ]}, "CTB")
    assert [(r["bound"], r["destination_en"], r["service_type"]) for r in ctb] == [("O", "West", "1"), ("I", "East", "1")]
    snapshot = catalog.source_records({"routeList": [
        {**base, "co": ["ctb"], "bound": {"ctb": "I"}, "serviceType": 1},
        {**base, "co": ["ctb"], "bound": {"ctb": "I"}, "serviceType": 2},
    ]}, "CTB")
    assert len(snapshot) == 1 and snapshot[0]["bound"] == "I"
    merged = catalog.route_records(standard, ctb)
    joint = next(r for r in merged if r["operator"] == "JOINT")
    assert all(p["service_type"] == "1" for p in joint["parts"])
    assert any(r["operator"] == "CTB" and r["bound"] == "I" for r in merged)
    with tempfile.TemporaryDirectory(prefix="phase17-catalog-") as directory:
        root = Path(directory)
        kmb_routes = write(
            root / "kmb-routes.json",
            {
                "data": [
                    {
                        "route": "1A", "bound": "O", "service_type": "1",
                        "orig_en": "Sau Mau Ping", "orig_tc": "秀茂坪",
                        "dest_en": "Star Ferry", "dest_tc": "天星碼頭",
                    },
                    {
                        "route": "1A", "bound": "O", "service_type": "2",
                        "orig_en": "Sau Mau Ping", "orig_tc": "秀茂坪",
                        "dest_en": "Star Ferry", "dest_tc": "天星碼頭",
                    },
                    {
                        "route": "2", "bound": "O", "service_type": "1",
                        "orig_en": "North", "orig_tc": "北",
                        "dest_en": "South", "dest_tc": "南",
                    },
                ]
            },
        )
        ctb_routes = write(
            root / "ctb-routes.json",
            {
                "data": [
                    {
                        "route": "1A", "orig_en": "Sau Mau Ping", "orig_tc": "秀茂坪",
                        "dest_en": "Star Ferry", "dest_tc": "天星碼頭",
                    },
                    {
                        "route": "99", "orig_en": "East", "orig_tc": "東",
                        "dest_en": "West", "dest_tc": "西",
                    },
                ]
            },
        )
        kmb_stops = write(root / "kmb-stops.json", {"data": [
            {"stop": "0001", "name_en": "One", "name_tc": "一", "lat": "22.3", "long": "114.1"},
            {"stop": "0002", "name_en": "Two", "name_tc": "二", "lat": "22.4", "long": "114.2"},
        ]})
        ctb_stops = write(root / "ctb-stops.json", {"data": [
            {"stop": "001", "name_en": "One", "name_tc": "一", "lat": 22.3, "lng": 114.1},
        ]})

        args = type("Args", (), {
            "kmb_routes": kmb_routes, "ctb_routes": ctb_routes,
            "kmb_stops": kmb_stops, "ctb_stops": ctb_stops,
            "output": root / "catalog", "generated_at": "2026-11-16T00:00:00Z",
            "source_revision": "test", "kmb_route_label": "kmb-test",
            "ctb_route_label": "ctb-test", "max_files": catalog.MAX_FILES,
            "max_package_bytes": catalog.MAX_PACKAGE_BYTES,
        })()
        manifest = catalog.build(args)
        records = []
        for name in manifest["routes"]["pages"]:
            records.extend(
                json.loads(line)
                for line in (args.output / name).read_text().splitlines()
                if line
            )
        assert len(records) == 5
        assert sum(row["operator"] == "JOINT" for row in records) == 1
        assert {row["route"] for row in records} == {"1A", "2", "99"}
        catalog_files = [path for path in args.output.iterdir() if path.is_file()]
        assert all(path.stat().st_size <= catalog.PAGE_BYTES for path in catalog_files)
        assert sum(path.stat().st_size for path in catalog_files) == manifest["limits"]["total_bytes"]

        directory=(args.output/'runtime.txt').read_text()
        assert 'L A' in directory and 'C 5' in directory and 'Q route-search-000.txt' in directory
        assert 'R routes-000.jsonl ' in directory
        search_lines=(args.output/'route-search-000.txt').read_text().splitlines()
        assert search_lines and search_lines[0].startswith('1A\t')
        expected_prefixes = {}
        for record in records:
            for index, character in enumerate(record['route']):
                expected_prefixes.setdefault(record['route'][:index], set()).add(character)
        keypad = (args.output / 'keypad.txt').read_text()
        actual_prefixes = dict(line.split('\t') for line in keypad.splitlines()[1:])
        assert actual_prefixes == {prefix: ''.join(sorted(keys))
                                   for prefix, keys in expected_prefixes.items()}
        for line in directory.splitlines():
            if line.startswith('Q '):
                _, name, size, count = line.split()
                raw = (args.output / name).read_bytes()
                assert len(raw) == int(size) and len(raw.splitlines()) == int(count)
        assert 'S KMB stops-kmb-000.jsonl 0001 0002' in directory
        assert 'S CTB stops-ctb-000.jsonl 001 001' in directory

        # A hard package limit is a build error; records are never silently
        # dropped to fit the device.
        args.output = root / "too-small"
        args.max_package_bytes = 1
        try:
            catalog.build(args)
        except ValueError as error:
            assert "package limit" in str(error)
        else:
            raise AssertionError("catalog builder accepted an oversized package")

    print("phase17 catalog tests: ok")


if __name__ == "__main__":
    main()
