# 4R.2 device test procedure

## Current state

The sync worker is implemented, but the repository has no hosted catalog URL.
The last device log says `HTTPS catalog manifest URL unset or invalid; online
sync disabled`. Browsing routes with that firmware tests route-stop HTTP and
the absence of individual stop-detail requests. It does not test catalog sync.

First verify the HTTP resource fix below. Then test full catalog transfers
after hosting is available. 4R.2 saves candidate files only; new stop names in
the UI and durable active generations are later steps and are not pass criteria.

## 1. Verify route-stop reliability now

The device owner runs these commands from the repository, with no other build
running. Use the normal device port when flashing (add `-p PORT` if necessary).

```sh
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
idf.py flash monitor
```

1. Capture the log from boot. Check `TLS allocator=PSRAM` and
   `TLS hardware AES=disabled`; these identify the settings in the running
   binary. Current `sdkconfig` already has these settings.
2. Wait for Wi-Fi, time synchronization, and the HTTPS smoke test to finish.
3. Open CTB route 10 inbound, then outbound. Wait for each request to finish.
4. Open KMB route 101 outbound service type 1, then service type 2. Wait for
   each request to finish. Repeat this four-selection cycle five times.
5. Keep the complete log, including successful requests and heap lines.

For the pinned trace, these routes returned 40, 37, 35, and 22 stops,
respectively. Live route data can change. Check HTTP 200, a parsed nonempty
stop list, and the absence of stop-detail requests. Do not leave the stop page
while a request is retrying in this test; navigation cancellation is separate.

If a request fails, the new `transport diagnostic` line contains the underlying
TLS error, certificate flags, socket errno, and the classified error. A TLS
allocation failure (`tls_code=141` in this IDF version) should finish as
`ESP_ERR_NO_MEM` without further automatic attempts. Timeouts retain the
three-attempt limit. Failure-time heap lines include the largest PSRAM block
as well as internal free space and the largest internal block.

Success on an automatic retry is useful evidence, but do not deliberately make
the network unreliable just to obtain it. Host fault-injection checks exercise
that path deterministically:

```sh
python3 tools/test_crystal_http_retry.py
```

## 2. Establish the HTTPS test location

Publish these three files together in one directory on an HTTPS static host
with a certificate trusted by the ESP-IDF bundle:

```text
artifacts/bus_catalog/full/manifest.json
artifacts/bus_catalog/full/kmb-stops.bsc
artifacts/bus_catalog/full/ctb-stops.bsc
```

Use the `full` directory, not the small fixtures in its parent. Serve the files
directly without authentication or redirects. Do not point the firmware at the
KMB source JSON or the HK Bus Crawling source; it expects the converted binary
catalogs. No host has been selected or published by this work, so there is no
real URL to insert yet. The build command below becomes runnable once that
prerequisite is met. Hosting alone does not establish a production publishing
owner or scheduled refresh process.

Check the local artifacts first:

```sh
python3 tools/bus_catalog/test_stop_catalogs.py --full
```

After publication, set `CATALOG_MANIFEST_URL` to the actual HTTPS URL ending in
`/manifest.json`. Verify all three hosted files against the local copies before
building:

```sh
read 'CATALOG_MANIFEST_URL?Paste the actual HTTPS manifest URL: '
export CATALOG_MANIFEST_URL
python3 - <<'PY'
import os
from pathlib import Path
from urllib.request import urlopen

url = os.environ['CATALOG_MANIFEST_URL']
assert url.startswith('https://') and url.endswith('/manifest.json')
base = url.rsplit('/', 1)[0]
for name in ('manifest.json', 'kmb-stops.bsc', 'ctb-stops.bsc'):
    target = base + '/' + name
    with urlopen(target, timeout=30) as response:
        assert response.status == 200 and response.url == target, target
        body = response.read()
    assert body == (Path('artifacts/bus_catalog/full') / name).read_bytes(), name
    print(f'PASS {name}: {len(body)} bytes')
PY
```

This command uses the project's zsh shell. Desktop certificate success does
not by itself prove the device trusts the certificate; the next test does.

## 3. Download both full catalogs on the device

In the same terminal where the verified URL is set:

```sh
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py -DCRYSTAL_BUS_CATALOG_MANIFEST_URL="$CATALOG_MANIFEST_URL" reconfigure
idf.py build
idf.py flash monitor
```

Let boot, Wi-Fi, and route bootstrap finish. Stay idle on the Bus route list
until both providers complete; do not select a route during this first run.
Capture the complete log. Expected evidence:

- A `bus_catalog_sync: manifest status=200 error=ESP_OK` line.
- KMB: `body=685020` and `validated=1`.
- CTB: `body=258706` and `validated=1`.
- Each provider logs `SPIFFS before candidate` and `SPIFFS after candidate`.
- `transfer heap`, framework `heap tls-peak`, and `released heap` lines show
  memory before/after acceptance. Record actual values; no measured safe full
  catalog budget has been accepted yet.
- No reset, watchdog, out-of-memory failure, or individual stop-detail request.

If either provider reports `validated=0`, preserve the preceding diagnostics.
HTTP 200 alone is insufficient: length, header, SHA-256, and candidate save
must also succeed. Do not erase SPIFFS to manufacture a passing space result.

## 4. Check foreground navigation and recovery

After a successful idle run, reboot and select a route while a catalog transfer
is in progress. The current implementation remembers hashes only within a
boot, so a reboot permits another transfer without deleting files. Capture the
catalog owner cancellation, route-stop submission, and route-stop completion.
Confirm the route request gets the next HTTP slot and background work can
continue afterward. Measure the delay: cancellation is cooperative and an
ongoing connect/read can currently wait for its timeout. A responsive screen
alone does not prove foreground transport priority.

On another transfer, disconnect Wi-Fi and reconnect. Check that the failed or
cancelled transfer can retry, that candidate validation eventually succeeds,
and that no stop-detail requests appear. Allow the five-minute failure retry
interval if a retry is deferred. This is not a test of durable active catalog
recovery, which belongs to 4R.3.

## Remaining engineering acceptance cases

The above is the initial device test, not the entire 4R.2 exit gate. Still needed:

- Recheck an unchanged manifest within the same boot: only the manifest is
  fetched, with `KMB catalog unchanged` and `CTB catalog unchanged`.
- Publish a valid change to one provider: download only that provider.
- Make one provider unavailable or corrupt: reject it while allowing the other
  provider to complete.
- Exercise low-memory rejection and measure full-size replacement space.

The explicit trigger is `bus_service_request_stop_catalog_sync()`; there is no
catalog-refresh UI button wired to it yet. These cases need a developer test
trigger or the scheduled check and controlled hosting. Rebooting is not an
unchanged-within-session test. Do not mark these cases passed from ordinary
route browsing.
