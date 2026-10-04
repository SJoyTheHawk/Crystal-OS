# Bus app — extracted native reference

This is a source snapshot for designing Crystal OS's generalized app architecture,
package wrapper, and `.capp` resolver. It is not an installable package or a
standalone ESP-IDF project. The current `main` firmware remains bus-free:
ESP-IDF does not automatically discover components nested under `examples/`.
Do not add these directories to the firmware component search path just to study them.

## Provenance and status

Extracted from `reference/Crystal-OS-bus-app-development/` on 2026-10-05.
The copied app, service, tools, catalogs, and historical documents match the local
`bus-app-development` checkpoint `fc20cf7d1c205991bb15a45a269ecbbd7b74ec0a`.
`SOURCE_MANIFEST.json` records the source paths and SHA-256 of every copied file.
The download remains untouched. Original source and CMake files are preserved.

This snapshot is an unfinished reference implementation with known memory/TLS
problems, not proof that the app is production-ready. Historical documentation
may describe older behavior or planned features; inspect the implementation when
it disagrees. In particular, do not treat roadmap checkmarks as test evidence.
The original license and attribution are included in `LICENSE.md` and `NOTICE`;
catalog source attribution is also recorded in the catalog manifests.

## Contents

| Path | Use |
| --- | --- |
| `components/bus_app/` | Native `CrystalApp` UI, icon, lifecycle, favorites, and event handling |
| `components/bus_service/` | KMB/CTB providers, normalization, route/stop catalogs, sync, caching, requests, and parser fixtures |
| `tools/bus_catalog/` | Host catalog converter and reproducibility checks |
| `tools/ctb_*_check.c` | Original host parser check sources |
| `artifacts/bus_catalog/` | Small fixtures, full binary catalogs, pinned compressed inputs, and measurements |
| `docs/` | Historical bus implementation, memory, and HTTP migration notes |
| `integration/` | Original startup, component registration, SDK defaults, and partition layout for context only |
| `ARCHITECTURE_HANDOFF.md` | Entry point for the next architecture session |

The inner `components/`, `tools/`, and `artifacts/` layout is intentional: the
original catalog tests resolve these paths relative to this example directory.
Paths in historical documents generally refer to that original project layout.
`integration/main/main.cpp` demonstrates the old factory/registry wiring; it is
not the current firmware entry point.

Shared OS implementations are not duplicated here. Study the current repository's
[`crystal_app`](../../components/crystal_app/),
[`crystal_core`](../../components/crystal_core/),
[`crystal_http`](../../components/crystal_http/),
[`crystal_hal`](../../components/crystal_hal/), and
[`crystal_registry`](../../components/crystal_registry/).
Use the preserved development branch if you need to reproduce the original full
firmware rather than treating these files as a drop-in package.

## Verification

From the repository root:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 examples/bus_app/tools/bus_catalog/test_stop_catalogs.py
# Also check the pinned full artifacts (known mismatch; see below):
PYTHONDONTWRITEBYTECODE=1 python3 examples/bus_app/tools/bus_catalog/test_stop_catalogs.py --full
```

This validates fixtures, binary layout, hashes, and deterministic regeneration
of the full catalogs from pinned local sources. It does not contact providers or
exercise device Wi-Fi, TLS, app lifecycle, or heap behavior. The C parser checks
need their original IDF/cJSON host harness; they are preserved as source only.

Extraction validation on 2026-10-05: all copied file hashes match their sources,
and the small fixture test passes. The `--full` test fails its byte equality
assertion: CTB regenerates exactly, but the generated KMB binary SHA-256 is
`f57987f95733a2c6deb6e5e96185b7c68bcde3249de8ef9d4bcb5453f2acb540`, while the
saved artifact is `6fa8f9e3896d1464f3ecb646b732e8095c78e8168c78749055cbaa5f638d10b4`.
The corresponding manifest hash differs too. The copied converter, inputs and
artifacts match the development checkpoint; the cause of this reproducibility
failure has not been established. Investigate it before using the saved artifact
as a golden package test. The snapshot has deliberately not been regenerated.

No `.capp`, manifest, loader, or wrapper implementation is supplied by this
extraction. Define those contracts in the platform work before claiming this
native app can be installed or safely unloaded.
