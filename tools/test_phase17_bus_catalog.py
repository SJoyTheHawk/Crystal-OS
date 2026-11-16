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
        assert len(records) == 3
        assert sum(row["operator"] == "JOINT" for row in records) == 1
        assert {row["route"] for row in records} == {"1A", "2", "99"}
        catalog_files = [path for path in args.output.iterdir() if path.is_file()]
        assert all(path.stat().st_size <= catalog.PAGE_BYTES for path in catalog_files)
        assert sum(path.stat().st_size for path in catalog_files) == manifest["limits"]["total_bytes"]

        directory=(args.output/'runtime.txt').read_text()
        assert 'L A' in directory and 'R routes-000.jsonl ' in directory
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
