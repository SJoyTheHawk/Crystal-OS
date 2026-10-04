#!/usr/bin/env python3
"""Small deterministic and structural checks for the host converter."""

import hashlib
import json
import struct
import subprocess
import tempfile
import argparse
from pathlib import Path

from build_stop_catalogs import ctb_records, encode

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "tools/bus_catalog/build_stop_catalogs.py"
HEADER = struct.Struct("<4sHBBII")
ENTRY = struct.Struct("<IIIIIIffB3x")


def verify_binary(artifact: Path, expected_count: int, expected_provider: int) -> list[list[str]]:
    data = artifact.read_bytes()
    magic, version, tag, _, count, pool = HEADER.unpack_from(data)
    assert magic == b"BSC2" and version == 2 and tag == expected_provider
    assert count == expected_count
    assert len(data) == HEADER.size + count * ENTRY.size + pool
    string_pool = data[HEADER.size + count * ENTRY.size:]
    decoded = []
    for index in range(count):
        id_off, id_len, en_off, en_len, tc_off, tc_len, lat, lon, valid = ENTRY.unpack_from(
            data, HEADER.size + index * ENTRY.size)
        fields = []
        for offset, length in ((id_off, id_len), (en_off, en_len), (tc_off, tc_len)):
            assert offset + length <= len(string_pool)
            fields.append(string_pool[offset:offset + length].decode("utf-8"))
        decoded.append(fields)
        assert valid in (0, 1)
        if valid:
            assert -90 <= lat <= 90 and -180 <= lon <= 180
    assert decoded == sorted(decoded, key=lambda fields: fields[0].encode("utf-8"))
    assert len({fields[0] for fields in decoded}) == count
    return decoded


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--full", action="store_true", help="also verify pinned full-size sources and artifacts")
    args = parser.parse_args()
    mixed = {
        "routes": [
            {"co": "CTB", "stops": ["002536", "002554"]},
            {"co": "KMB", "stops": ["KMB-001"]},
        ],
        "stopList": {
            "002536": {"name_en": "Central"},
            "002554": {"name_tc": "灣仔"},
            "KMB-001": {"name_en": "Other provider"},
        },
    }
    records, count, missing_routes = ctb_records(mixed)
    assert count == 2 and missing_routes == 0
    assert [record["id"] for record in records] == ["002536", "002554"]
    published_shape = {
        "routeList": {
            "ctb": {"co": ["ctb"], "stops": {"ctb": ["002536"]}},
            "shared": {"co": ["kmb", "ctb"], "stops": {"kmb": ["KMB-001"]}},
        },
        "stopList": {
            "002536": {"name": {"en": "Central", "zh": "中環"},
                       "location": {"lat": 22.2, "lng": 114.1}},
        },
    }
    records, count, missing_routes = ctb_records(published_shape)
    assert count == 1 and missing_routes == 1
    assert records[0]["name_tc"] == "中環" and records[0]["lon"] == 114.1
    del mixed["stopList"]["002554"]
    try:
        ctb_records(mixed)
    except ValueError as exc:
        assert "missing 1" in str(exc)
    else:
        raise AssertionError("missing CTB stop was accepted")
    try:
        encode("KMB", [{"id": "same"}, {"id": "same"}])
    except ValueError as exc:
        assert "duplicate" in str(exc)
    else:
        raise AssertionError("duplicate stop IDs were accepted")

    with tempfile.TemporaryDirectory() as temp:
        out = Path(temp)
        command = ["python3", str(BUILD), "--kmb", str(ROOT / "tools/bus_catalog/fixtures/kmb_bulk.json"), "--ctb", str(ROOT / "tools/bus_catalog/fixtures/ctb_snapshot.json"), "--out", str(out), "--generated-at", "2026-10-02T00:00:00Z", "--source-revision", "test"]
        subprocess.run(command, check=True, capture_output=True, text=True)
        manifest = json.loads((out / "manifest.json").read_text())
        assert manifest["providers"]["KMB"]["records"] == 2
        assert manifest["providers"]["CTB"]["ctb_route_referenced_ids"] == 2
        for provider in ("KMB", "CTB"):
            artifact = out / manifest["providers"][provider]["artifact"]
            decoded = verify_binary(artifact, 2, 1 if provider == "KMB" else 2)
            assert hashlib.sha256(artifact.read_bytes()).hexdigest() == manifest["providers"][provider]["artifact_sha256"]
            assert any(fields[2] for fields in decoded)
        first = (out / "manifest.json").read_bytes()
        subprocess.run(command, check=True, capture_output=True, text=True)
        assert first == (out / "manifest.json").read_bytes()
    if args.full:
        sources = ROOT / "artifacts/bus_catalog/sources"
        published = ROOT / "artifacts/bus_catalog/full"
        command = ["python3", str(BUILD),
                   "--kmb", str(sources / "kmb-stop-20261002.json.gz"),
                   "--ctb", str(sources / "hkbus-routeFareList-20261002.json.gz"),
                   "--source-revision", "pinned-20261002",
                   "--ctb-publication-time", "2026-10-02T01:18:18Z",
                   "--generated-at", "2026-10-02T14:39:31Z"]
        with tempfile.TemporaryDirectory() as temp:
            subprocess.run(command + ["--out", temp], check=True, capture_output=True, text=True)
            for name in ("kmb-stops.bsc", "ctb-stops.bsc", "manifest.json"):
                assert (Path(temp) / name).read_bytes() == (published / name).read_bytes()
        manifest = json.loads((published / "manifest.json").read_text())
        for provider, count, tag in (("KMB", 6753, 1), ("CTB", 2586, 2)):
            artifact = published / manifest["providers"][provider]["artifact"]
            verify_binary(artifact, count, tag)
            assert hashlib.sha256(artifact.read_bytes()).hexdigest() == manifest["providers"][provider]["artifact_sha256"]
    print("stop catalog converter checks passed")


if __name__ == "__main__":
    main()
