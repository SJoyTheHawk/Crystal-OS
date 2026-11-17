#!/usr/bin/env python3
"""Host acceptance checks for the bounded Bus Lite Script package."""
import json
import hashlib
import subprocess
import tempfile
from pathlib import Path

import crystal
import phase17_bus_fixture


def main():
    query = {"route": ["2"], "operator": ["CTB"]}
    stops = phase17_bus_fixture.response("/api/stops", query)["stops"]
    assert stops[1]["id"] == "CTB-2-002"
    query["stop"] = [stops[1]["id"]]
    eta = phase17_bus_fixture.response("/api/eta", query)["etas"]
    assert eta[0]["destination"] == "Central" and eta[0]["minutes"] == 4
    query["stop"] = ["central"]
    assert phase17_bus_fixture.response("/api/eta", query)["etas"] == []
    long_stops = phase17_bus_fixture.response(
        "/api/stops", {"route": ["68X"], "operator": ["KMB"]}
    )["stops"]
    assert len(long_stops) == 17 and long_stops[-1]["seq"] == 17
    for path, operator, count in (("/kmb/route-stop/1A/outbound/1", "KMB", 3),
                                  ("/ctb/route-stop/CTB/2/inbound", "CTB", 3),
                                  ("/kmb/route-stop/68X/inbound/1", "KMB", 17)):
        data = phase17_bus_fixture.provider_response(path)["data"]
        assert len(data) == count and data[-1]["seq"] == str(count)
        stop_id = data[1]["stop"]
        prefix = "/ctb" if operator == "CTB" else "/kmb"
        detail = phase17_bus_fixture.provider_response(f"{prefix}/stop/{stop_id}")["data"]
        assert detail["stop"] == stop_id and detail["name_en"]
        route = "68X" if count == 17 else "2" if operator == "CTB" else "1A"
        eta_path = f"/ctb/eta/CTB/{stop_id}/{route}" if operator == "CTB" else f"/kmb/eta/{stop_id}/{route}/1"
        assert len(phase17_bus_fixture.provider_response(eta_path)["data"]) == 3
    assert phase17_bus_fixture.provider_response("/ctb/route-stop/CTB/2/O") is None
    with tempfile.TemporaryDirectory(prefix="phase17-bus-") as temp:
        root = Path(__file__).resolve().parents[1]
        samples = root / "tools/fixtures/bus-phase1-providers"
        for row in json.loads((samples / "provenance.json").read_text())["responses"]:
            raw = (samples / row["file"]).read_bytes()
            assert len(raw) == row["bytes"]
            assert hashlib.sha256(raw).hexdigest() == row["sha256"]
        live = crystal.pack(root / "examples/bus3p", Path(temp) / "live.capp")
        assert live["memory_kb"] == 4096
        assert live["permissions"]["network_hosts"] == ["data.etabus.gov.hk", "rt.data.gov.hk"]
        assert "phase17.bus.fixture" not in (root / "examples/bus3p/main.lua").read_text()
        phase17_bus_fixture.OUTPUT = Path(temp) / "package"
        phase17_bus_fixture.create("192.168.1.195")
        package = Path(temp) / "bus.capp"
        manifest = crystal.pack(phase17_bus_fixture.OUTPUT, package)
        inspected = crystal.inspect(package)
        assert "README.md" not in inspected["files"]
        assert manifest["id"] == "com.example.bus3p"
        assert manifest["runtime"] == "lua"
        assert manifest["icon"] == "assets/icon.bin"
        assert "assets/icon.bin" in inspected["files"]
        assert "fixture" in manifest["name"]
        assert manifest["version"] == live["version"]
        assert manifest["memory_kb"] == 4096 and manifest["worker_threads"] == 1
        assert inspected["manifest"]["permissions"]["network_hosts"] == ["192.168.1.195"]
        source = (phase17_bus_fixture.OUTPUT / "main.lua").read_text()
        assert "catalog_search_step" in source
        assert "catalog_load_step" in source
        assert "catalog_add_next_keys(route)" in source
        assert "Catalog.ready" in source and "update_keypad_state" in source
        assert "possible:find(value,1,true)" in source
        assert "catalog_stop_step" in source
        assert "assets/catalog/runtime.txt" in source
        assert "v.catalog_job" in source and "v.stop_catalog_job" in source
        assert "math.ceil(route_total/8)" in source
        assert "v.stop_list_job={index=1,generation=v.catalog_generation}" in source
        assert "local function stop_list_step(job)" in source
        assert "render_stop_row = function(index)" in source
        assert "ui.list_add(v.stop_list," in source
        assert "ui.list_set_item_height(v.stop_list," not in source
        assert "gap=math.floor(8*scale)" in source
        assert "local stop_list_h=math.max(math.floor(80*scale),H-math.floor(132*scale))" in source
        assert "local detail_pad=pad" in source
        assert 'v.recent_heading=text(page,"Recent:"' in source
        assert "on_select=function(index)" in source
        assert "move_stop_page" not in source
        assert "#query<6" in source
        assert "v.search_due=2" in source
        assert "http.cancel(old)" in source
        assert "local Store = {}" in source
        assert 'store_write("favorites",candidate)' in source
        assert 'store_write("recent_routes",candidate)' in source
        assert "function Store.load_favorites()" in source
        assert "function Store.remove_favorite(index)" in source
        assert "function Store.add_recent(route)" in source
        assert "Could not save favorite" in source
        assert "Could not remove favorite" in source
        assert "state.flush" not in source
        source_bytes = len(source.encode("utf-8"))
        assert 0 < source_bytes <= 64 * 1024
        assert len(inspected["files"]) <= 128
        assert package.stat().st_size <= 5_632 * 1024 + 66
        catalog_files = [path for path in (phase17_bus_fixture.OUTPUT / "assets" / "catalog").rglob("*") if path.is_file()]
        assert catalog_files and max(path.stat().st_size for path in catalog_files) <= 32 * 1024
        catalog_manifest = json.loads(
            (phase17_bus_fixture.OUTPUT / "assets" / "catalog" / "manifest.json").read_text()
        )
        assert catalog_manifest["limits"]["file_count"] == len(catalog_files)
        assert catalog_manifest["limits"]["file_count"] <= 128
        assert catalog_manifest["limits"]["total_bytes"] <= 5_632 * 1024
        assert catalog_manifest["routes"]["records"] == 1968
        assert catalog_manifest["routes"]["selection"]["KMB"]["excluded_nonstandard"] == 288
        assert catalog_manifest["routes"]["selection"]["CTB"]["normalized_records"] == 818
        assert catalog_manifest["routes"]["search_pages"]
        assert all((phase17_bus_fixture.OUTPUT / "assets/catalog" / name).stat().st_size <= 16 * 1024
                   for name in catalog_manifest["routes"]["search_pages"])
        assert catalog_manifest["routes"]["joint_records"] == 135
        assert catalog_manifest["stops"]["KMB"]["records"] == 6753
        assert catalog_manifest["stops"]["CTB"]["records"] == 2586
        assert catalog_manifest["limits"]["largest_file_bytes"] <= 16 * 1024
        # The bundled route pages are JSON Lines so a P4 callback can decode
        # one record at a time; confirm prefix coverage is not truncated to
        # the first visible UI page.
        route_records = []
        for page in catalog_manifest["routes"]["pages"]:
            route_records.extend(json.loads(line) for line in
                                 (phase17_bus_fixture.OUTPUT / "assets" / "catalog" / page).read_text().splitlines())
        assert len(route_records) == catalog_manifest["routes"]["records"]
        assert all(row["service_type"] == "1" for row in route_records)
        assert all(part["service_type"] == "1" for row in route_records for part in row.get("parts", []))
        assert len({row["key"] for row in route_records}) == len(route_records)
        # Runtime filters must be derived from actual records, never numeric
        # first/last ranges that can omit lexical prefix matches such as 101.
        directory = phase17_bus_fixture.OUTPUT / "assets/catalog/runtime.txt"
        expected_keys = {}
        for row in route_records:
            for index, character in enumerate(row['route']):
                expected_keys.setdefault(row['route'][:index], set()).add(character)
        keypad = (directory.parent / 'keypad.txt').read_text()
        assert dict(line.split('\t') for line in keypad.splitlines()[1:]) == {
            prefix: ''.join(sorted(keys)) for prefix, keys in expected_keys.items()}
        compact_count = 0
        for line in directory.read_text().splitlines():
            fields = line.split()
            if fields[0] == "Q":
                raw = (directory.parent / fields[1]).read_bytes()
                assert len(raw) == int(fields[2])
                assert len(raw.splitlines()) == int(fields[3])
                compact_count += int(fields[3])
            elif fields[0] == "K":
                assert len((directory.parent / fields[1]).read_bytes()) == int(fields[2])
            elif fields[0] == "R":
                rows = [json.loads(item) for item in (directory.parent / fields[1]).read_text().splitlines()]
                assert fields[2] == "".join(sorted({row["route"][0] for row in rows}))
                assert all(row["route"].isalnum() and row["route"].isascii() for row in rows)
                assert all(part["route"] == row["route"] for row in rows for part in row.get("parts", []))
            elif fields[0] == "S":
                rows = [json.loads(item) for item in (directory.parent / fields[2]).read_text().splitlines()]
                assert fields[3:] == [rows[0]["id"], rows[-1]["id"]]
        assert compact_count == len(route_records)
        one_a = [row for row in route_records if row["route"].startswith("1A")]
        assert one_a and len(route_records) > 8
        for checkpoint in ("launch_begin", "launch_ready", "catalog_loaded",
                           "stops_loaded", "teardown"):
            assert f'resource_checkpoint("{checkpoint}")' in source
        assert 'resource_checkpoint("eta_complete")' in source
        # Syntax-check against the same Lua sources as firmware. Behavioral
        # and rendering coverage lives in test_phase17_bus_render.py.
        lua = root / "managed_components/georgik__lua"
        sources = sorted(path for path in (lua / "lua").glob("*.c")
                         if path.name not in ("luac.c", "onelua.c", "ltests.c"))
        binary = Path(temp) / "lua"
        subprocess.run(["cc", "-O1", "-DLUA_USE_POSIX",
                        f"-I{lua / 'include'}", f"-I{lua / 'lua'}",
                        *(str(path) for path in sources), "-lm", "-ldl",
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary), "-e",
                        "assert(loadfile(arg[0])); os.exit(0)", str(phase17_bus_fixture.OUTPUT / "main.lua")], check=True)
        print(json.dumps({"package": str(package), "bytes": package.stat().st_size, "files": inspected["files"]}, indent=2))


if __name__ == "__main__":
    main()
