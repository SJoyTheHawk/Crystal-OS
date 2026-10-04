# Crystal OS without the bus workload

## Branches and scope

- `bus-app-development`: preserved bus development checkpoint `fc20cf7`.
- `main`: the no-bus firmware, promoted from `baseline/no-bus`.
- `baseline/no-bus`: this experiment, derived from that checkpoint.

This is the current OS with the bus workload removed, not a rollback to an older
OS revision. Removed: Bus launcher entry/factory/icon/UI, the entire bus service
(including providers, route/stop parsing, caches and catalog synchronization),
prepared catalogs/source snapshots, converter/fixture tools, and gorhkbus reference.
Bus planning and historical HTTP integration notes remain in `docs/` as reference;
they are not built into firmware. The completion roadmap remains on both branches.

The shell, app registry, Clock, Weather, Calculator, Dev Tester, Wi-Fi/SNTP,
`crystal_http`, partition layout, and TLS/memory configuration remain unchanged.
This keeps the experiment focused on the bus workload. In particular, this does
not independently test the older hardware-AES or internal-only TLS settings.
The pre-existing modified Waveshare reference submodule is not part of this change.

## Build and flash

Use a separate build directory so an old bus binary is never mistaken for this one:

```sh
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py -B build/no-bus build
idf.py -B build/no-bus -p /dev/cu.usbmodem101 app-flash
idf.py -B build/no-bus -p /dev/cu.usbmodem101 monitor -b 2000000
```

Replace the port as needed. `app-flash` is intended for the existing Crystal OS
board with the same partition table and bootloader. It preserves NVS credentials,
settings, and SPIFFS, making comparison easier. A full `flash` also writes the
project's empty SPIFFS image and can clear saved catalogs. No erase is needed.
Old bus registry keys and cached files may remain on the board, but this firmware
has no code to load them, allocate their runtime buffers, or schedule bus requests.

## Hardware comparison

Use the same board, access point, power supply, settings, and SDK configuration
for both branches. Record the commit and whether bus caches were warm or cold.

1. Boot the baseline at least three times. Confirm there is no Bus card and no
   bus service/catalog traffic. Record boot time and main stack headroom.
2. Connect Wi-Fi. The existing framework automatically requests
   `https://example.com/` once per boot. Require `HTTPS smoke test passed`, and
   retain `HTTPS smoke heap` plus `crystal_http` before/TLS-peak/released logs.
   Record free internal RAM, largest internal block, and free PSRAM.
3. Open Weather and refresh repeatedly (allow each refresh to finish). Confirm
   location/forecast HTTPS succeeds. Weather uses the core's direct
   `esp_http_client` path; the boot smoke request exercises `crystal_http`.
4. Toggle Wi-Fi off/on five times, then refresh Weather. Check reconnect, time
   sync, responsive settings, and app switching. The framework smoke request
   runs once per boot, not once per reconnect; reboot to repeat that test.
5. Switch among Clock, Weather, Calculator, and Settings twenty times. Leave
   running for thirty minutes, refreshing Weather periodically. Record failures,
   resets, watchdog messages, and available heap diagnostics. A successful
   one-shot smoke request alone does not establish long-term memory stability.
6. Repeat the common workload on `bus-app-development`, then separately exercise
   bus catalog synchronization and route browsing. Compare the same log points
   and note the additional bus workload and cache state.

| Observation | No-bus baseline | Bus development |
| --- | --- | --- |
| Commit / SDK config / cache state | | |
| Boot time / main stack headroom | | |
| Smoke HTTPS status | | |
| Internal free / largest block before TLS | | |
| Sampled internal / largest block during TLS | | |
| PSRAM before / during / after request release | | |
| Weather successes / attempts | | |
| Wi-Fi reconnect successes / attempts | | |
| Thirty-minute errors / resets | | |

If both versions fail, investigate shared OS/network/TLS behavior. If only the
bus version fails, that supports bus-related resource pressure or a bus-specific
bug; it does not by itself prove the shared OS has no defects. Neither build
success nor the host HTTP retry test substitutes for this hardware comparison.

## Local verification (2026-10-04)

- ESP-IDF 6.1 build passed in `build/no-bus`.
- Generated component list excludes `bus_app` and `bus_service`, and retains
  `crystal_http` and `crystal_core`.
- Linked ELF symbol inspection found no `BusApp`, `make_bus_app`, or `bus_` symbols.
- `python3 tools/test_crystal_http_retry.py` passed.
- `git diff --check` passed.
- Firmware has not been flashed or hardware-tested as part of this change.

## Returning to development

After committing baseline changes, switch and build separately:

```sh
git switch bus-app-development
idf.py -B build/bus-development build
idf.py -B build/bus-development -p /dev/cu.usbmodem101 app-flash
```

Return with `git switch baseline/no-bus`. Keep each branch's build directory
separate. Ignored `sdkconfig` is shared between branches in this checkout; record
it with your measurements and avoid changing it during the comparison.
