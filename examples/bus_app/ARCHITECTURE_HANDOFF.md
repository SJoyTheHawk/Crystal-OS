# Architecture handoff: bus app as a demanding reference workload

**Status**: Validated against extracted code at fc20cf7  
**Validation date**: 2026-10-05  
**Outcome**: Development halted due to ESP32-S3 memory constraints; preserved as architecture evidence

## Objective and reading order

Use this app to derive reusable host contracts and a package wrapper/resolver,
while keeping bus-specific provider logic outside Crystal OS core. The immediate
goal is architecture evidence for Phase 12 reliability and Phase 13 app catalog
work, with a migration path into the later package/runtime phases.

Read the current repository plans first:

1. [`IMPLEMENTATION_PLAN.md`](../../docs/IMPLEMENTATION_PLAN.md): Phase 12 reliability,
   Phase 13 app catalog, and their existing acceptance criteria.
2. [`APP_PLATFORM.md`](../../docs/APP_PLATFORM.md): proposed manifest, streaming CAPP
   transport, host ABI, permissions, transactional install, registry reconciliation,
   and runtime tiers. This document is a design, not an implemented contract.
3. [`clock3p`](../clock3p/README.md): a complementary lightweight example against
   the proposed ABI.
4. `components/bus_app/include/bus_app.hpp` and `src/bus_app.cpp`, then
   `components/bus_service/include/` and the service implementation.
5. `docs/bus-app-memory-analysis.md`, `docs/bus-app-completion-roadmap.md`,
   and `docs/crystal-http-tls-aes-memory-fix.md` for historical context.

The existing plan places packaging/registry generalization in Phase 14, transport
in Phase 15, and declarative/Lua runtimes in Phases 16–17. Reconcile that sequence
explicitly if the next proposal brings package work forward into Phases 12–13.

## Existing boundaries to examine

| Existing coupling | Evidence | Contract the architecture needs |
| --- | --- | --- |
| Native app lifecycle and raw LVGL widgets | `BusApp::onCreate/onPause/onResume/onDestroy`, `bus_app.hpp` | Runtime adapter, widget ownership, bounded UI calls, and teardown semantics |
| Shared state persistence | `load_favorites`/`save_favorites` use `state()` | Versioned serialization, app-private data, migration, clear-data and uninstall behavior |
| HTTP transport and network events | `crystal_http.h`, `crystal_network.h`, service request handlers | Permission checks, request ownership, cancellation completion, and callback lifetime |
| Singleton service and worker resources | `bus_service_init`, static state, FreeRTOS task/queues, listener registration | Per-app resource accounting and deterministic shutdown; cancellation alone is not unloading |
| Delivery to UI | `bus_service_set_listener`, event dispatch and LVGL locking | Host-owned UI dispatch with stale-callback rejection after app destruction |
| Global cache paths and persistent catalogs | `/spiffs/bus_route_catalog.bin`, `bus_stop_catalog.c`, `bus_catalog_sync.c` | App-private filesystem handles, quotas, cache eviction and atomic updates |
| Large bodies, parsing and catalogs | Body limits, heap allocations, JSON parsing, binary catalog loader | Peak-memory budgets including concurrent copies, TLS/internal RAM, PSRAM and largest free block |
| Fixed request-owner constants | `*_OWNER_ID` in service code | Host-issued ownership scoped to app instance/generation, including unload cleanup |
| Firmware registration and SDK dependencies | `integration/main/`, component `CMakeLists.txt` files | Package identity, compatibility resolution, runtime dispatch and host ABI boundary |

KMB/CTB URLs, route normalization, route variants, stop lookup and favorites are
app/domain logic. Transport scheduling, resource ownership, UI dispatch, storage
isolation and lifecycle enforcement are candidates for shared host capabilities.
Do not put provider-specific schemas or global bus caches into the generalized OS.

Inspect both `crystal_http` request paths and any remaining direct
`esp_http_client` use: a permission/budget wrapper is incomplete if an app can
bypass it. Provider hosts and catalog hosting are visible in the source and
CMake configuration; derive a proposed allowlist from the actual paths, including
redirect policy, rather than copying test-failure URLs into a manifest.

## Decisions for the wrapper/resolver proposal

- Separate build-time dependency resolution (IDF components) from package install
  validation (format, signature, paths, quotas), runtime compatibility (ABI,
  minimum OS and runtime), and launch-time resource acquisition.
- Define the smallest host API that supports both this workload and Clock.
  Include data ownership, sizes, error values, threading and lifetime rules.
- Choose an initial runtime deliberately. These files are C/C++ with raw pointers,
  LVGL, FreeRTOS and direct ESP-IDF calls. Wrapping them in CAPP does not provide
  binary compatibility, dynamic linking, isolation, or safe unload. The proposed
  native loader still needs a feasibility spike; a Lua port is separate work.
- Keep immutable code/assets separate from mutable app data and regenerable
  caches. Full provider snapshots here are test inputs, not automatically package
  contents. Decide whether catalogs are installed, downloaded or streamed and
  measure each choice's flash/RAM cost.
- Define admission limits for body size, parsed results, task stacks, timers,
  request concurrency, persistent cache size and UI objects. Budget the peak
  overlap, not just steady-state free PSRAM. Do not invent safe numeric ceilings
  from desktop file sizes; measure on the target board.
- Define install/update rollback, boot reconciliation and crash attribution before
  exposing a generalized install path. Tie these to Phase 12 recovery and Phase
  13 catalog semantics, including disable, clear data, launch failure and removal.

## Validation findings (2026-10-05)

### Memory constraints validated

The bus app encountered three distinct failure modes on ESP32-S3 with 8MB PSRAM:

1. **Internal DRAM exhaustion** (719 bytes remaining): WiFi static RX buffers (16KB), 
   UI object allocation (~30 LVGL objects at onCreate), and catalog validation 
   converged during app launch, causing lwIP semaphore allocation failure.

2. **TLS handshake failures**: mbedTLS internal allocations require 19-27KB internal 
   DRAM; concurrent catalog sync reduced available DRAM below this threshold despite 
   `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`.

3. **Scheduler corruption** (rare): FreeRTS task scheduler crash during catalog 
   loading; root cause unclear but may indicate stack overflow in 6144-byte worker stack.

### Architecture patterns validated

The implementation successfully demonstrated:

- **Lifecycle compliance**: onCreate/onPause/onResume/onDestroy follow the four-state 
  contract; favorites persist across destruction; timers and network listeners clean up.
- **Service/UI boundary**: Worker thread on Core 0 handles HTTP; events dispatch to 
  LVGL task with ownership transfer for PSRAM arrays.
- **Request cancellation**: `bus_service_cancel_all()` aborts in-flight HTTP work; 
  app guards stale callbacks with generation counters (`stop_request_id_`).
- **Large dataset handling**: 943KB stop catalogs with A/B commit pattern; binary 
  search over memory-mapped SPIFFS files.
- **Multi-provider normalization**: KMB/CTB route variants unified into 
  `bus_route_variant_t` with provider-qualified identity.

### Singleton service pattern limits

`bus_service` uses static state (task handle, queue, listener, route cache) with no 
per-instance isolation. This works for firmware-bundled single apps but blocks:

- Multiple app instances (not a v1 requirement)
- Clean unload without OS restart (Phase 14 package uninstall requirement)
- Resource accounting per app (Phase 14 quota enforcement)

The listener callback and request ownership are implicitly tied to the live app; 
destroying the app without unregistering the listener would deliver events to freed memory.

### Real resource costs (measured)

- **Component size**: 1845 LOC app + ~4000 LOC service = 5845 LOC total
- **Flash footprint**: 4.0 MB folder (includes test artifacts and reference docs)
- **Runtime allocations**: 
  - FreeRTOS worker: 8192-byte stack
  - Catalog sync worker: 6144-byte stack  
  - Request queue: 16 slots × ~100 bytes = 1.6 KB
  - Route cache: 1048 routes × 192 bytes = ~200 KB PSRAM
  - Stop catalog: 943 KB (KMB + CTB combined) on SPIFFS
- **onCreate budget**: 275-500ms actual vs 80ms budget (overrun expected for complex apps)

### Host contract gaps identified

1. **No resource ownership API**: The app calls `bus_service_init()` which creates 
   global worker resources; there is no `bus_service_destroy()` or per-instance handle.
2. **No memory budget enforcement**: Service allocates from PSRAM without quota checks; 
   an app cannot declare its peak requirement upfront for admission control.
3. **No permission model**: Service accesses `data.etabus.gov.hk` and `rt.data.gov.hk` 
   directly; there is no manifest-declared allowlist or runtime permission check.
4. **Implicit lifecycle coupling**: `onCreate()` calls service init; `onDestroy()` 
   unregisters listener but does not tear down worker resources.
5. **NVS key collision risk**: App uses `state().set("fav_dat", ...)` with no namespace 
   isolation; two apps using the same key would collide.

## Recommendations for Phase 14+

1. **Mandatory per-app resource handles**: Replace singleton service with 
   `bus_service_create(app_id)` returning an opaque handle; all APIs take the handle 
   as first parameter. Enforce one handle per app instance; track allocations per handle.

2. **Upfront resource declaration**: Add `manifest.yaml` fields:
   ```yaml
   memory_kb: 256            # peak PSRAM budget including catalogs
   storage_kb: 1024          # persistent cache quota
   worker_threads: 1         # background task count
   network_hosts: ["data.etabus.gov.hk", "rt.data.gov.hk"]
   ```
   Reject install if resources unavailable; enforce at runtime via allocator hooks.

3. **Transactional teardown**: `onDestroy()` must block until all owned resources 
   (tasks, timers, queues, allocations) are released; host verifies no leaks before 
   marking app as destroyed.

4. **Namespaced state**: `crystal.state.set(k, v)` maps to NVS key 
   `<app_id>:<k>` automatically; deleting app folder triggers NVS prefix scan to 
   clear orphaned keys.

5. **Lazy catalog loading**: Move 943KB catalog load from onCreate() to first Search 
   tab access; apps that start on Favorites tab never pay the catalog cost.

This app validates that the CrystalApp lifecycle works for complex multi-API workloads, 
and identifies the exact boundaries (resource ownership, quotas, permissions, cleanup 
verification) that Phase 14 package infrastructure must formalize.

Preserve this extracted snapshot as evidence. Put experimental adaptations in a
separate implementation so the original behavior remains available for comparison.
