#!/usr/bin/env python3
"""Host acceptance checks for the bounded Bus Lite Script package."""
import json
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
    with tempfile.TemporaryDirectory(prefix="phase17-bus-") as temp:
        root = Path(__file__).resolve().parents[1]
        phase17_bus_fixture.OUTPUT = Path(temp) / "package"
        phase17_bus_fixture.create("192.168.1.195")
        package = Path(temp) / "bus.capp"
        manifest = crystal.pack(phase17_bus_fixture.OUTPUT, package)
        inspected = crystal.inspect(package)
        assert manifest["id"] == "com.example.bus3p"
        assert manifest["runtime"] == "lua"
        assert manifest["version"] == "0.2.5"
        assert manifest["memory_kb"] == 512 and manifest["worker_threads"] == 1
        assert inspected["manifest"]["permissions"]["network_hosts"] == ["192.168.1.195"]
        source = (phase17_bus_fixture.OUTPUT / "main.lua").read_text()
        assert "math.min(#rows, 8)" in source
        assert "math.min(#rows, 3)" in source
        assert "catalog_search_step" in source
        assert "catalog_stop_step" in source
        assert "assets/catalog/runtime.txt" in source
        assert "v.catalog_job" in source and "v.stop_catalog_job" in source
        assert "math.ceil(route_total/8)" in source
        assert "math.ceil(#stop_results/8)" in source
        assert "#query<6" in source
        assert "v.search_due=2" in source
        assert "http.cancel(v.request_id)" in source
        assert "state.set(\"favorites\", favorites)" in source
        assert "state.set(\"recent_routes\", recents)" in source
        assert "while #recents>5 do table.remove(recents) end" in source
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
        assert catalog_manifest["routes"]["records"] == 2115
        assert catalog_manifest["routes"]["joint_records"] == 160
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
        # Runtime filters must be derived from actual records, never numeric
        # first/last ranges that can omit lexical prefix matches such as 101.
        directory = phase17_bus_fixture.OUTPUT / "assets/catalog/runtime.txt"
        for line in directory.read_text().splitlines():
            fields = line.split()
            if fields[0] == "R":
                rows = [json.loads(item) for item in (directory.parent / fields[1]).read_text().splitlines()]
                assert fields[2] == "".join(sorted({row["route"][0] for row in rows}))
                assert all(row["route"].isalnum() and row["route"].isascii() for row in rows)
                assert all(part["route"] == row["route"] for row in rows for part in row.get("parts", []))
            elif fields[0] == "S":
                rows = [json.loads(item) for item in (directory.parent / fields[2]).read_text().splitlines()]
                assert fields[3:] == [rows[0]["id"], rows[-1]["id"]]
        one_a = [row for row in route_records if row["route"].startswith("1A")]
        assert one_a and len(route_records) > 8
        for checkpoint in ("launch_begin", "launch_ready", "catalog_loaded",
                           "stops_loaded", "teardown"):
            assert f'resource_checkpoint("{checkpoint}")' in source
        assert 'end, "eta_complete")' in source
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
