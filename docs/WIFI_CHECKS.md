# Wi-Fi checks

Keep the C6 firmware unchanged. Test one thing at a time.

## Retest after the P4 reconnect fix

1. Install the updated P4 application using the existing app-only update flow.
   Enable Wi-Fi Diagnostics, unplug Ethernet, and capture serial logs.
2. Confirm Bus Lite can refresh. Take one Wi-Fi Snapshot as the baseline.
3. Shield the device until a request fails, then remove shielding completely.
   Mark both actions in the capture. Leave the AP selection unchanged initially.
4. Watch for association, IP acquisition and a successful refresh. If it remains
   stuck after two minutes, take a Snapshot and Run Network Checks once.
5. Then try Wi-Fi off/on and selecting another known-working AP, recording each
   action. Retain the capture even if it recovers.

The fix rejects previous-SSID disconnects and obsolete retry notifications during
a new attempt. Host regression tests reproduce the overlap before the fix; the
shielding recovery outcome still needs hardware confirmation. Snapshot/retry logs
include host DMA memory availability. Existing SDIO pool/credit errors are captured,
but healthy heap values do not rule out exhausted transport pools or C6 buffers.

| Check | Purpose |
| --- | --- |
| Capture boot/version logs | Identify the running P4 stack; unavailable C6 version means unknown. |
| Bus Lite untouched for 20–30 minutes, then closed for 20–30 minutes | Separate background app traffic from actual idle behavior. |
| Numeric IP, DNS, then HTTPS when failure occurs | Locate the failing network layer. |
| Repeat near the AP and on a hotspot | Check signal/router dependence. |
| Wi-Fi off/on and AP off/on | Check reconnection and P4–C6 errors. |
| Ethernet after capturing failure | Compare another interface without destroying initial evidence. |
| Brookesia if still needed, with verified P4 backup first | Compare host software on the same C6; partition layouts differ. |

## Capture

Close other serial monitors. Use the ESP-IDF Python environment:

```sh
python tools/wifi_check.py --port /dev/cu.usbmodem5C930124901 \
  --log docs/test-evidence/wifi-checks/RUN.log
```

Type `# Bus Lite closed` to mark actions, and `quit` to finish. Capture sends
no automatic probes. Opening the serial bridge may nevertheless reboot the
board; check the captured boot log and start the idle interval afterward.
Never open a second reader on the same port.

## On-device diagnostics

Open **Settings → Developer Tools → Wi-Fi Diagnostics**. Turn it on to show
recent Wi-Fi/network logs in a scrolling viewer. Capture stays on when leaving
the page and defaults off after reboot. Turn it off to stop capture. The viewer
shows the newest lines from a bounded, in-memory buffer; use serial for a full run.

- **Wi-Fi Snapshot:** read IP, DNS, signal, power mode and C6 version once.
- **Run Network Checks:** try gateway TCP port 80, DNS, then HTTPS once, using the
  active route (which can be Ethernet). A closed gateway port is inconclusive.
- **Clear Diagnostic Logs:** clear the recent log buffer.

Enabling capture takes a local status snapshot, without C6 queries or network
probes. Explicit checks run on a worker task. Turning capture off cancels queued
checks; a query already in progress finishes using its existing timeout. No
Wi-Fi settings, recovery policy or C6 firmware changes.

## Optional P4 UART commands

The toggle and viewer are included in normal builds. To also enable UART commands:

```sh
cp sdkconfig .cache/wifi-diagnostics-sdkconfig
idf.py -B .cache/wifi-diagnostics-build \
  -DSDKCONFIG="$PWD/.cache/wifi-diagnostics-sdkconfig" \
  -DCRYSTAL_WIFI_DIAGNOSTICS=ON build
```

This builds but does not flash. When installed, turn on **Wi-Fi Diagnostics**
in Developer Tools first. The UART capture then accepts:

| Command | Result |
| --- | --- |
| `netdiag status` | Local connection state, route, IP, DNS, memory and P4 version. |
| `netdiag ip 192.168.1.1 80` | Five-second TCP attempt without DNS/TLS; use a known reachable service. |
| `netdiag dns data.etabus.gov.hk` | Resolver result/address (may use DNS cache). |
| `netdiag https https://data.etabus.gov.hk/` | Verified TLS/HTTP HEAD result; even an HTTP 404/405 confirms a server response. |
| `netdiag radio` | C6 AP information and actual power-save mode. |
| `netdiag c6` | One version query; failure or all-zero response is reported as unknown. |

Run commands only before/after idle or once failure appears. Queries can affect
the failing state. RPC and DNS calls use their existing timeouts and run only
on the diagnostic task. No settings, retry policy, C6 image, or SDK files change.
Do not use full `flash` for a diagnostic application replacement: preserve the
P4 partition table, NVS and app data, and verify the source/build first.

Run evidence is kept in `docs/test-evidence/wifi-checks/`; unperformed tests
must remain marked pending.
