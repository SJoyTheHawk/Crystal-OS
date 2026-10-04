# Phase 3 Step 0 — KMB catalog baseline

**Status:** Build verified; device gate pending. Do not start Step 1.
**Plan:** [Phase 3 KMB catalog plan](crystal-http-phase3-kmb-catalog.md)

## Firmware identity

Captured on 2026-09-28. Runtime sources match commit
`883269523c73b623f4fa85e4e436f73bbab1b06b` (phase 2 complete).
The existing dirty reference submodule remains untouched. Documentation edits
are not firmware changes. The local generated `sdkconfig` is identified below;
the commit alone does not identify this configuration.

```text
ESP-IDF: 6.1
build: idf.py build — passed
application bytes: 2836560 (0x2b4850)
application partition bytes: 5242880 (46% free)
firmware SHA-256: e11daec9e379de0a97643b75404dca987d5b08103f90727026ab90191af7d21e
sdkconfig SHA-256: dae74a630f1bc45982785032205a178cb15f9ba55fe8e3e2b0ae29754a51cf0d
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768
```

This is a host build record, not proof that this image is running on the device.
The earlier checkpoint-A binary was replaced by this baseline build. No flash
or storage erasure was performed during this baseline preparation.

## Expected behavior from source inspection

These are expectations, not measured device results.

The catalog operation uses a bus request id and `type=4`. It has no framework
request id: both provider fetches still use the direct HTTP helper. Framework
smoke-test logs do not measure this operation.

Successful progress events occur in this order (each includes “Please wait”
where applicable):

1. Preparing route data fetch.
2. Downloading KMB route data (repeated for each attempt).
3. KMB data downloaded / Resolving route data.
4. Resolving KMB route data.
5. Downloading CTB route data (repeated for each attempt).
6. CTB data downloaded / Resolving route data.
7. Resolving CTB route data.
8. Saving route data.
9. Final `BUS_EVT_ROUTE_CATALOG` event.

Each provider currently allows ten attempts, with 500 ms between failures and
a 15-second client timeout. A failed provider has no downloaded/resolving
events. CTB is attempted after KMB even if KMB fails.

| Provider result | Initial cache | Expected final result, assuming cache writes succeed |
|---|---|---|
| Both succeed | Any | 2 succeeded, 0 failed; complete cache; `ESP_OK` |
| KMB fails, CTB succeeds | None or incomplete | 1 succeeded, 1 failed; CTB partial cache; `ESP_ERR_NOT_FINISHED` |
| KMB succeeds, CTB fails | None or incomplete | 1 succeeded, 1 failed; KMB partial cache; `ESP_ERR_NOT_FINISHED` |
| Only one succeeds | Complete cache present | 1 succeeded, 1 failed; previous cache restored; `ESP_FAIL` |
| Both fail | Any | 0 succeeded, 2 failed; previous cache restored if available; `ESP_FAIL` |

Record the initial cache state for every run. Counts must be captured from
the run; live provider catalogs can change. KMB variants are retained in PSRAM,
so a cold fetch can legitimately retain more memory than it started with.

## Device prerequisites

- Confirm that the device runs the baseline image above, not checkpoint A.
- A complete cache younger than seven days skips the request and returns id 0.
  Rebooting does not remove `/spiffs/bus_route_catalog.bin`. A cache-hit run
  cannot satisfy this gate.
- To keep Step 0 free of runtime changes, use an expired cache, or arrange a
  backed-up storage reset before capture. The firmware has no cache-delete
  console command or force-refresh UI. Do not assume a host `rm` can delete a
  device file. A full `idf.py flash` includes the generated storage image and
  replaces existing SPIFFS data; it must not be used as an incidental cache reset.
- Controlled failures require an external network rule scoped to this device:
  deny KMB (`data.etabus.gov.hk`) while allowing CTB (`rt.data.gov.hk`), then
  the reverse for the third run. Keep Wi-Fi, time synchronization, and the
  other provider available. Record the actual router/firewall rule and remove
  it after each run. No router controls have been configured or verified here.
- The unmodified bus path does not emit catalog-specific before/after internal
  heap, largest-block, or PSRAM samples. An external debugger measurement must
  be arranged, or the Step 0 plan must explicitly allow temporary measurement
  instrumentation before those fields can be completed. Smoke-test heap logs
  are not substitutes. Record the same UI/weather conditions for each sample.
- At preparation time `/dev/cu.usbmodem101` was held by another Python process.
  Use one serial monitor for capture; no existing monitor was interrupted.

## Device evidence

| Required run | Device log | Counts/cache/events | Catalog memory samples | Result |
|---|---|---|---|---|
| Fresh KMB + CTB success | Pending | Pending | Pending | Not run |
| KMB failure, CTB available | Pending | Pending | Pending | Not run |
| KMB success, CTB failure | Pending | Pending | Pending | Not run |

Earlier supplied logs show cache hits and stop-list operations, so none of
these three catalog cases is accepted from those logs.

For each run attach the complete serial log and record:

```text
firmware/config hashes=
initial cache state and refresh method=
fault rule and removal confirmation=
bus request id=  framework request id=N/A
KMB attempts/result/routes/variants=
CTB attempts/result/routes=
progress order and final status=
providers succeeded/failed=
cache result=complete|partial|restored|none
internal free before/after=
largest internal block before/after=
PSRAM free before/after=
memory measurement method and UI/weather state=
result=pass|fail
```

Step 0 remains pending until all three runs and required measurements are
recorded. A successful build alone does not pass the gate.
