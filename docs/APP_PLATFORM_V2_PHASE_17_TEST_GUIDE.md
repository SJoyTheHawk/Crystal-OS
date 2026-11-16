# Phase 17 board test guide

For **Bus transformation Phase 1 (0.2.8)**, follow
[BUS_APP_QUICK_START.md](BUS_APP_QUICK_START.md). It uses the new firmware plus
`/tmp/phase17-bus-live.capp` and direct KMB/CTB HTTPS. The Bus fixture steps later
in this document are historical platform tests and optional fixture checks;
they are not the install path for the live-provider package.


This runbook covers the Phase 17 acceptance checks that need an ESP32-P4. It
assumes the board is on the same LAN as the development Mac. Record failures
with the board revision, firmware image SHA-256, date, and relevant serial log.
These procedures are not board results; complete the result table at the end
after running them on the target.

## 1. Build and flash the Phase 17 firmware

If Clock remains at `--:--:--` and stopwatch state does not change after
reinstalling the Lua package, rebuild and application-flash the firmware with
the label-adoption and absolute storage-path fixes. Reinstalling the package
alone cannot update these C++ runtime components. After flashing, confirm that
the Clock displays time immediately and advances, a 30-second Timer preset
shows `00:30` then counts down, and Stopwatch Start advances with nonzero laps.
Check pause/resume and reset for both counters as well.

For the subsequent Stopwatch-only fix, application-flash the firmware with
integer-preserving Lua state conversion. Press Stopwatch Reset once to discard
any start timestamp rounded by the earlier firmware, then Start. After five
seconds the display and a new Lap should show about `00:05`. Pause for five
seconds (the value must hold), resume for three seconds (about `00:08`), then
reset. The countdown Timer was confirmed working separately. On P4, also check
the Settings-sized Manage Apps buttons, row dragging, and confirmation dialogs.

In a terminal at the repository root:

```sh
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
ls /dev/cu.*
```

Set `PORT` to the P4 USB port shown by `ls`, then flash the application image:

```sh
export PORT=/dev/cu.usbmodemXXXX
idf.py -p "$PORT" app-flash
```

`app-flash` preserves the partition table and stored packages. Open the serial
monitor and capture its output:

```sh
LOG="/tmp/phase17-board-$(date +%Y-%m-%d).log"
idf.py -p "$PORT" monitor -b 1000000 | tee "$LOG"
```

Reset the board if needed. Confirm it reaches the Crystal launcher, initializes
the display and storage, and shows no panic or watchdog-reset messages. Keep
this monitor open for the Wi-Fi-installed tests below. The Waveshare P4's
CH343 USB-to-UART port supports firmware flashing and logs, but the current
`crystal install --usb` handler only reads native ESP USB Serial/JTAG. Reusing
the flash/monitor port for package upload therefore does not work. Use the
signed Wi-Fi path in Steps 5 and 6 on this board.
If the board is not connected to the same LAN as the Mac, open **Settings →
Network → WiFi Networks**, connect it, and confirm its IP under **Device
Status** before continuing.

## 2. Prepare package signing for `crystal run`

`crystal run` requires a signed package. The current checkout has
`CONFIG_CRYSTAL_PACKAGE_TRUSTED_PUBLIC_KEY_HEX=""`, so a board running that
image cannot accept the signed package until it trusts the matching public key.
The boot record also reports that no initial signing key was configured, but
the key store lives in persistent NVS and can retain keys from earlier images.

First use an already trusted key if its private key is available. Otherwise,
only provision a test key on a dedicated development board where keeping that
key trusted is acceptable. Never use a release key. Key ID 240 below is an
example; use an ID known to be unused on that board. The firmware has no
Settings or CLI flow to list or remove trusted keys. Setting the initial key
causes it to be written to NVS, and restoring an empty sdkconfig value later
does not revoke it. If these conditions are not acceptable, mark the `crystal
run` item **NOT RUN** and leave trust unchanged.

The configured key replaces any key already stored under the same ID. Since
this firmware cannot list the stored IDs, use an ID confirmed unused or, on a
dedicated development board, an ID whose replacement you accept.

The host needs the packer dependencies in `tools/requirements-pack.txt`. Run
the following with the ESP-IDF environment from Step 1 active if imports are
missing:

```sh
python3 -m pip install -r tools/requirements-pack.txt
```

For a dedicated development board, create a test-only Ed25519 key outside the
repository:

```sh
mkdir -p ~/.crystal_keys
python3 - <<'PY'
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

key = Ed25519PrivateKey.generate()
private_path = Path.home() / ".crystal_keys/phase17-test-ed25519.pem"
private_path.write_bytes(key.private_bytes(
    serialization.Encoding.PEM,
    serialization.PrivateFormat.PKCS8,
    serialization.NoEncryption(),
))
public_hex = key.public_key().public_bytes(
    serialization.Encoding.Raw,
    serialization.PublicFormat.Raw,
).hex()
print(f"Public key hex for menuconfig: {public_hex}")
PY
chmod 600 "$HOME/.crystal_keys/phase17-test-ed25519.pem"
```

In the same terminal with ESP-IDF activated and `PORT` set, run
`idf.py menuconfig`. Under **Crystal OS**, set **Initial trusted app signing
key ID** to the test ID you chose and **Initial trusted app signing public key**
to the printed 32-byte public key as 64 hex characters. Save and exit, then:

```sh
idf.py build
idf.py -p "$PORT" app-flash
```

Reboot and check the serial log for `Loaded configured package signing key ID
<ID>`. The configured key is now trusted in NVS. Keep the test private key
outside the repository and do not treat reflashing an empty-key image as key
removal.

## 3. Exercise `crystal run` with the Clock package

On the board, open **Settings → Developer Tools → Local Control Panel** and
enable it. Note the device URL and one-time pairing PIN shown on screen.
Replace the placeholders below with those values and the test private key path:
set `KEY_ID` to the trusted key ID selected in Step 2.

```sh
DEVICE_URL=http://192.168.1.50/
PAIRING_PIN=123456
PACKAGE_KEY="$HOME/.crystal_keys/phase17-test-ed25519.pem"
KEY_ID=240
python3 tools/crystal.py run examples/clock3p \
  --wifi "$DEVICE_URL" --pin "$PAIRING_PIN" \
  --key "$PACKAGE_KEY" --key-id "$KEY_ID"
```

The command packs, signs, installs, launches the app, and follows device logs.
Confirm the Clock appears, the command reports a successful install and launch,
and the clock screen renders. Use the tabs to check Clock, Timer and Stopwatch;
start and pause the stopwatch and add a lap. Stop only the host log follower
with `Ctrl+C` when finished; this does not close the running app. This passes
the CLI end-to-end check when install and launch succeed and the log stream
shows the device app events. If pairing expires, re-enable the API to display a
fresh one-time PIN.

## 4. Verify the system timer survives app destruction

Start the Clock app again if needed. Open its **Timer** tab and select **5m**;
the preset starts the system timer immediately. Confirm the remaining time is
counting down. Open **Settings → Manage Apps** and uninstall **Clock** while
the countdown is still running. This exercises the actual package
stop/destroy/uninstall path. Return to the launcher and wait five minutes.

Pass conditions:

- Serial log contains `crystal_core: timer expired`.
- The system displays the **Timer finished** toast (and plays the alarm if
  sound alerts are enabled).
- Clock is uninstalled and the launcher remains responsive throughout.

**Board result (2026-11-15): PASS.** The captured P4 log shows `timer started:
300 seconds`, Clock `onDestroy`, successful package uninstall, and the catalog
dropping from six apps to five. More than five minutes later it records
`crystal_core: timer expired` and the `Timer finished` toast while the launcher
remains active; the user confirmed the launcher menu is correct after removal.
The same capture reports `speaker initialization failed`, so audible alarm
playback was not verified by this run.

## 5. Verify a runaway Lua callback closes only its app

Create the development fixture in `/tmp` (the first pack command creates an
unsigned artifact; sign it below before installing over Wi-Fi):

```sh
mkdir -p /tmp/phase17-runaway
cat > /tmp/phase17-runaway/app.yaml <<'YAML'
abi: 1
id: com.example.phase17.runaway
name: Phase 17 Runaway Test
version: 1.0.0
min_os: 0.17.0
author: Crystal OS test
license: MIT
runtime: lua
entry: main.lua
bar_style: dark
memory_kb: 128
storage_kb: 8
worker_threads: 0
permissions:
  notify: true
YAML
cat > /tmp/phase17-runaway/main.lua <<'LUA'
local ui = crystal.ui
local M = {}

function M.onCreate()
  ui.button(ui.screen(), {
    text = "Run forever",
    on_click = function()
      while true do end
    end,
  })
  return true
end

function M.onResume() return true end
function M.onPause() return true end
function M.onDestroy() return true end
return M
LUA
python3 tools/crystal.py pack /tmp/phase17-runaway --unsigned \
  -o /tmp/phase17-runaway.capp
```

### Install on the Waveshare P4 using signed Wi-Fi

Keep the serial monitor open. Use the already trusted test key from Step 2 and
the device URL from Step 3. Enable **Settings → Developer Tools → Local Control
Panel** if needed and use a fresh pairing PIN shown by the board. In the
terminal where `PACKAGE_KEY`, `KEY_ID`, `DEVICE_URL`, and `PAIRING_PIN` are set:

```sh
python3 tools/crystal.py pack /tmp/phase17-runaway \
  --key "$PACKAGE_KEY" --key-id "$KEY_ID" \
  -o /tmp/phase17-runaway-signed.capp
python3 tools/crystal.py install --wifi "$DEVICE_URL" --pin "$PAIRING_PIN" \
  /tmp/phase17-runaway-signed.capp
```

Save the returned `token` as `TOKEN` in that terminal for the Step 6 installs.
The token expires on reboot or when the local control panel is disabled; pair
again with a fresh PIN if necessary. The unsigned `.capp` is not accepted over
Wi-Fi. If no trusted test key is available, follow Step 2 or mark this check
**NOT RUN** rather than bypass signature validation.

### Optional native USB Serial/JTAG installation

Only use this alternative if the selected port actually connects to the ESP
native USB Serial/JTAG controller. A `/dev/cu.usbmodem...` name alone does not
prove this: the CH343 bridge also uses that naming on macOS. The board's other
USB OTG connector is not automatically the Serial/JTAG endpoint either.
Close any monitor using the native port and set `USB_PORT` to that endpoint:

```sh
python3 tools/crystal.py install --usb --port "$USB_PORT" /tmp/phase17-runaway.capp
```

Before the runaway test, turn **Settings → Developer Tools → Extended Lua Timeout** off (5 seconds). On selects 30 seconds and persists across reboot. The historical 50 ms result below does not certify either new timeout on hardware.

For the native USB path, reboot the board and reopen IDF Monitor. For Wi-Fi,
reboot only if the install response reports `restart_required: true`, then
reopen Monitor as needed. Launch **Phase 17 Runaway Test** from
the launcher and tap **Run forever**. Confirm the **App stopped** toast appears,
the test app closes back to the launcher, and the serial log records the
5-second callback limit without a panic, watchdog reset, or board reboot. Remove
the test package through **Settings → Manage Apps** after recording the result.

**Historical board result (2026-11-15; 50 ms callback build): PASS for the logged timeout/closure/no-reset
behavior.** The signed Wi-Fi install returned `installed: true`; after reboot,
the P4 registered `com.example.phase17.runaway`. At 28,921 ms it logged
`App stopped responding (50 ms callback limit)`, followed by the **App stopped**
toast at 28,922 ms, `onPause` at 28,923 ms, and `onDestroy` at 28,924 ms. UI
redraw records continue through 58,904 ms without a panic, watchdog reset, or
another boot in the supplied capture. Visual confirmation of the launcher and
touch responsiveness, plus test-package removal, remain to be recorded.

Evidence: user-supplied `Pasted text.txt` attachment
`e41de52c-9ee4-42b9-9561-ebc214e82220`. The log identifies ESP32-P4 silicon
revision v1.3, app version 0.17.0, build time November 15, 2026 at 18:24:17,
and flashed ELF SHA-256 prefix `6d39dc7da...`. IDF Monitor warns that the local
build differs (`c5392688...`); do not attribute this run to the current local
binary hash or rely on its decoded symbol names. The full tested firmware
image hash and physical board revision remain unrecorded.

## 6. Verify HTTP success, allowlist and quota behavior

This check uses two terminal windows and the P4 serial monitor. Keep the Mac
and P4 on the same LAN. The test package uses the Mac's IPv4 address in both its
host allowlist and request URLs.

**Terminal A — run the temporary HTTP server.** From the repository root:

```sh
python3 tools/phase17_http_fixture.py serve
```

Leave this terminal running. It prints each request it receives. If macOS
Firewall asks, allow incoming Python connections on port 8000. If you already
started another fixture server on port 8000, stop it with `Ctrl+C` first.

**Terminal B — create the test packages and check the server.** From the
repository root, get the Mac's Wi-Fi IPv4 address:

```sh
MAC_IP="$(ipconfig getifaddr en0)"
if [ -z "$MAC_IP" ]; then
  printf 'Enter the Mac LAN IPv4 address: '
  read -r MAC_IP
fi
printf 'Mac LAN IP: %s\n' "$MAC_IP"
```

Then create both packages and make a quick request:

```sh
python3 tools/phase17_http_fixture.py create --host "$MAC_IP"
curl -i "http://$MAC_IP:8000/ok"
```

The curl response should be `200 OK` with `{"ok":true}`, and Terminal A should
show `request /ok`. That confirms the server and Mac address work. The create
command writes `/tmp/phase17-http` and `/tmp/phase17-http-worker-check` and
puts the selected Mac IP into each manifest and Lua URL. The app gives `/slow`
a 60-second client timeout because the fixture deliberately waits 20 seconds.
If curl works but the P4 cannot connect, check the Mac firewall and confirm the
P4 is not on an isolated guest Wi-Fi network.

Keep these two addresses separate: `MAC_IP` is the Mac running port 8000;
`DEVICE_URL` is the P4's Local Control Panel URL shown on the device, usually
`http://<P4-IP>/` with no port 8000. The install command sends its POST request
to the P4. Terminal A should only receive the fixture's GET requests.

**Terminal B — pack and install.** Set the same signing key ID used in Step 2,
and the P4's Local Control Panel URL. These shell variables must be set in
Terminal B; variables from other terminal windows are not shared.

Package signing needs the Python dependencies from Step 2. If they are not
installed for this `python3`, install them before packing:

```sh
python3 -m pip install -r tools/requirements-pack.txt
```

```sh
PACKAGE_KEY="$HOME/.crystal_keys/phase17-test-ed25519.pem"
KEY_ID=240 # Change this if Step 2 used another key ID.
printf 'P4 device URL (not the Mac URL; omit port 8000): '
read -r DEVICE_URL
python3 tools/crystal.py pack /tmp/phase17-http --key "$PACKAGE_KEY" --key-id "$KEY_ID" -o /tmp/phase17-http.capp
python3 tools/crystal.py pack /tmp/phase17-http-worker-check --key "$PACKAGE_KEY" --key-id "$KEY_ID" -o /tmp/phase17-http-worker-check.capp
```

Choose one installation path. If the Step 5 token is still valid, install both
packages with it:

```sh
printf 'Step 5 pairing token: '
read -r TOKEN
python3 tools/crystal.py install --wifi "$DEVICE_URL" --token "$TOKEN" /tmp/phase17-http.capp
python3 tools/crystal.py install --wifi "$DEVICE_URL" --token "$TOKEN" /tmp/phase17-http-worker-check.capp
```

If the token expired, enable the Local Control Panel and pair by PIN while
installing the first package. That command already installs the HTTP Test app;
do not install that package a second time. Copy the returned token and use it
only for the worker-check package:

```sh
printf 'Fresh P4 pairing PIN: '
read -r PAIRING_PIN
python3 tools/crystal.py install --wifi "$DEVICE_URL" --pin "$PAIRING_PIN" /tmp/phase17-http.capp
printf 'Token returned by that install: '
read -r TOKEN
python3 tools/crystal.py install --wifi "$DEVICE_URL" --token "$TOKEN" /tmp/phase17-http-worker-check.capp
```

Install both packages before rebooting if either response says
`restart_required: true`; reopen the serial monitor after reboot if needed.

Launch **Phase 17 HTTP Test** from the P4 launcher. The app has no screen, so
watch the serial monitor and Terminal A. Terminal A should show `/slow`; the
device log should show:

- `PHASE17_HTTP_STARTED` with a request ID.
- `PHASE17_QUOTA nil worker quota exceeded` while the first request is active.
- `PHASE17_ALLOWLIST nil network host is not allowed` for `example.invalid`.
- About 20 seconds later, `PHASE17_HTTP_CALLBACK 200` with the JSON body and
  `PHASE17_CANCEL_REQUEST` with a request ID.

`PHASE17_CANCEL_REQUEST` means the second request was queued; it does not mean
the Mac has received it yet. Wait until Terminal A prints `request /slow` a
second time, then immediately uninstall **Phase 17 HTTP Test** from
**Settings → Manage Apps** while that request is in flight. Wait at least 25
seconds. Pass cancellation if the app is removed, the OS stays responsive, and
`PHASE17_CANCEL_CALLBACK_UNEXPECTED` does not appear. Terminal A may report a
closed client when the request is cancelled; that is expected.

Then launch **Phase 17 HTTP Worker Check**. Confirm
`PHASE17_WORKER_RECOVERY_STARTED` has a request ID and
`PHASE17_WORKER_RECOVERY_CALLBACK 200` appears. This verifies the shared worker
still completes requests after an app owner is cancelled. Uninstall the
worker-check package, stop the server with Ctrl+C, and remove any remaining
test package.

The fixture generator and server are in
[`tools/phase17_http_fixture.py`](../tools/phase17_http_fixture.py).

## 7. Stage A: Bus baseline and resource capture

Stage A uses the existing **Bus Lite 0.2.3** fixture flow before catalog work.
The package has a 160 KiB Lua arena, 16 KiB app-storage quota and one HTTP
worker. Host package and LVGL tests pass; this does not establish P4 runtime
fit. The current image was built for OS 0.17.0 but has not yet been flashed or
tested on the board in this run. Its SHA-256 is recorded in the transformation
plan.

The runtime and Bus package now emit resource snapshots at named checkpoints
only: `launch_begin`, `launch_ready`, the first `catalog_loaded`, the first
`stops_loaded`, the first `eta_complete`, and `teardown`. There is no periodic
diagnostic timer. The snapshots include arena size/current/peak/largest-free,
internal and PSRAM free/largest blocks and app deltas, HTTP/TLS and JSON peaks,
callback/resource counts, and app-storage use. The post-cleanup line is
`crystal_script: resource-checkpoint teardown ... clean=1`.

### Prepare and flash the known firmware

Use the build in this checkout so the Lua memory-failure containment and
checkpoint instrumentation are present. The board configuration is the
ESP32-P4 rev 1.x Function EV profile with hosted C6 Wi-Fi.

```sh
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
ls /dev/cu.*
```

Close any existing `idf_monitor` or serial terminal first; it holds the USB
serial port exclusively. Set `PORT` to the P4 USB serial device, then
application-flash and open a monitor in one terminal:

```sh
export PORT=/dev/cu.usbmodemXXXX
idf.py -p "$PORT" app-flash
idf.py -p "$PORT" monitor -b 1000000
```

Record `shasum -a 256 build/crystal_os.bin` with the log. Confirm boot reaches
the launcher without a panic. Application flashing preserves the package
partition. The old Bus package must be uninstalled before replacing it; that
removes its saved favorites and recent routes.

### Install the fixture package and exercise the flow

Keep the monitor open. In a second terminal, start the Mac fixture server and
leave it running:

```sh
python3 tools/phase17_bus_fixture.py serve
```

In a third terminal, set `MAC_IP` to the Mac's LAN address and `DEVICE_URL` to
the P4 Local Control Panel URL. These are different addresses: the app fetches
the Mac fixture on port 8000, while the CLI installs to the P4. Complete
Steps 2 and 3 above if a trusted signing key or fresh pairing PIN is needed.

```sh
MAC_IP=192.168.1.195
DEVICE_URL=http://192.168.1.52/
PAIRING_PIN=123456
PACKAGE_KEY="$HOME/.crystal_keys/phase17-test-ed25519.pem"
KEY_ID=240
python3 tools/phase17_bus_fixture.py create --host "$MAC_IP"
python3 tools/crystal.py run /tmp/phase17-bus \
  --wifi "$DEVICE_URL" --pin "$PAIRING_PIN" \
  --key "$PACKAGE_KEY" --key-id "$KEY_ID"
```

In Bus Lite, open **Search**, type `1A`, and choose **1A KMB**. Confirm the
numbered stop list loads, choose the second stop, and check that fixture arrivals
appear. Use Back to return to the stop list and Search, then open Favorites and
return to Search. This checks the existing launch, route, stop, ETA and
navigation baseline. The fixture output should show route/stop/ETA requests.

Capture the serial lines beginning `BUS_RESOURCE`. Each named checkpoint should
appear once during this run. Exit Bus Lite and confirm the final cleanup line
reports `clean=1` with no retained resources. Do not use the host test's mock
numbers as board measurements. Record the actual output, app version, firmware
hash, date, board revision and serial log. If this is repeated, retain the
lowest internal/PSRAM and Lua-largest-free readings for comparison with later
catalog stages.

Host verification commands are:

```sh
python3 tools/test_phase17_bus_package.py
python3 tools/test_phase17_bus_render.py
```

The render test exercises production Lua UI bindings and real LVGL at 480/720
pixels; HTTP, state and resource metrics are deterministic adapters. The
separate allocator check exercises 160 KiB memory recovery and OOM containment.
These checks do not replace the P4 run above. After Stage A passes on hardware,
continue with the complete catalog and live-provider stages in the
[transformation plan](APP_PLATFORM_V2_PHASE_17_3_BUS_MIGRATION_PLAN.md).

## 8. Stage B: build and inspect the bounded catalog assets

Stage B is a host-only catalog preparation step. It uses the recorded
2026-10-02 provider snapshots already in this checkout; it does not contact a
Mac fixture server or a live provider. Generate the package assets with:

```sh
python3 tools/build_phase17_bus_catalog.py \
  --kmb-routes examples/bus_app/artifacts/bus_catalog/sources/hkbus-routeFareList-20261002.json.gz \
  --ctb-routes examples/bus_app/artifacts/bus_catalog/sources/hkbus-routeFareList-20261002.json.gz \
  --kmb-stops examples/bus_app/artifacts/bus_catalog/sources/kmb-stop-20261002.json.gz \
  --ctb-stops examples/bus_app/artifacts/bus_catalog/sources/hkbus-routeFareList-20261002.json.gz \
  --output examples/bus3p/assets/catalog \
  --generated-at 2026-10-02T22:37:17+08:00 \
  --source-revision pinned-20261002 \
  --kmb-route-label recorded-hkbus-routeFareList-kmb \
  --ctb-route-label recorded-hkbus-routeFareList-ctb \
  --kmb-route-timestamp 2026-10-02T00:00:00Z \
  --ctb-route-timestamp 2026-10-02T00:00:00Z \
  --kmb-stop-timestamp 2026-10-02T22:37:17+08:00 \
  --ctb-stop-timestamp 2026-10-02T01:18:18Z
```

The command must report 2,115 normalized route records, 160 joint records,
6,753 KMB stops and 2,586 CTB stops. Check that `assets/catalog/manifest.json`
records source hashes/timestamps and that its limits report 115 files, every
JSON Lines page at most 16 KiB (and every asset below the 32 KiB contract), and
a total below 5.5 MiB. Run the catalog, package and UI
checks:

```sh
python3 tools/test_phase17_bus_catalog.py
python3 tools/test_phase17_bus_package.py
python3 tools/test_phase17_bus_render.py
```

Repack for inspection or later portal installation with the IDF Python
environment if the system Python does not have signing dependencies:

```sh
/Users/szemy/.espressif/python_env/idf6.1_py3.10_env/bin/python \
  tools/crystal.py pack examples/bus3p \
  --key "$HOME/.crystal_keys/phase17-test-ed25519.pem" --key-id 240 \
  -o /tmp/phase17-bus.capp
/Users/szemy/.espressif/python_env/idf6.1_py3.10_env/bin/python \
  tools/crystal.py inspect /tmp/phase17-bus.capp
```

The package contains the Stage C Lua catalog reader. Do not use this step's
asset count as a board memory measurement.

## 9. Stage C: exercise catalog-backed pages

**2026-11-16 launch failure and retest:** The 0.2.3 catalog package exhausted
its 160 KiB arena immediately after launch_begin. Teardown reported 131 KiB
peak payload; allocator headers and fragmentation also consume the arena.
Free PSRAM remained about 24 MiB. Use Bus Lite 0.2.4 with the provisional
512 KiB quota and bounded search buffers. Firmware does not change.
Uninstall the prior Bus Lite package (saved stops are deleted), upload
/tmp/phase17-bus-stagec.capp in the P4 Control Panel, then apply/restart.

First confirm launch_ready appears and launch_begin shows /512. Search 1,
wait for the catalog to finish, and use Next/Previous through several pages;
then change quickly to 1A. Continue the steps below, capturing search_ready,
stops_loaded, eta_complete and teardown. Do not mark the P4 memory target
passed from host measurements: record the board peak and largest free block.

**Follow-up watchdog retest (0.2.5):** 0.2.4 now launches without OOM, but
search exceeded the 50 ms callback deadline. Install 0.2.5 from
/tmp/phase17-bus-stagec.capp. The reader separates file IO and individual JSON
conversions over timer ticks; wait for the search status to complete. Verify
1, 1A, 101 and Next/Previous, then stop metadata and arrivals. Keep the serial
log from launch_begin through search_ready and teardown. Record whether the
870–919 ms onCreate warning persists. Memory and callback acceptance are
pending this board retest; no firmware reflash is required.

**0.2.5 board retest: partial pass.** The excerpt beginning at uptime
1,023,080 ms reaches launch_ready, search_ready, stops_loaded and eta_complete
with no quota or callback timeout. Observed peak Lua payload is 247/512 KiB;
internal free memory stays 236 KiB at checkpoints. Startup still takes 746 ms
against the 200 ms lifecycle budget. The 7,093 ms span between catalog_loaded
and search_ready includes scheduling/debounce and is not a clean latency test.
The fixture terminal history mixes runs and has no timestamps; do not assign
its earlier /api/routes requests to this launch.

For the remaining board checks, use 1A KMB or 2 CTB for stops/ETA and 68X KMB
for the 17-stop paging check. Use 101 only to inspect the joint search card and
identity: this fixture does not supply 101 stops/ETA. Unknown routes (including
1 and 101/101R) return HTTP 200 with {"stops":[],"etas":[]} (22 bytes).
Capture navigation/cancellation and a final teardown checkpoint separately;
those are not proven by this log. No package reinstall is required for these
remaining checks.

**68X follow-up: restart the fixture server before paging.** The next supplied
board excerpt (uptime 1,625,383–1,684,082 ms, request IDs 18–33) shows all
16 requests completing with HTTP 200, no Lua quota/callback failure, and stable
internal free heap of 242,211 bytes after responses. However, the three stop
responses are each 289 bytes: this exactly matches the earlier three-stop 68X
fixture. The current 17-stop fixture serializes to 1,665 bytes. The Python
server likely still has the older module loaded; editing the file does not
reload the running process. Paging is not yet verified by this excerpt.

1. In the fixture server terminal, press Ctrl+C, then restart it from this repo:

       python3 tools/phase17_bus_fixture.py serve

2. In another terminal, verify the running server returns 17 stops:

       curl -fsS 'http://192.168.1.195:8000/api/stops?route=68X&operator=KMB&bound=I&service_type=1' | python3 -c 'import json,sys; print(len(json.load(sys.stdin)["stops"]))'

3. In the existing Bus Lite install, reopen 68X and refresh the stop list using
   the top-right action. Check Page 1 / 3, Next to Page 2 / 3 (stops 9–16),
   then Page 3 / 3 (stop 17). Select stop 17 and verify the fixture logs
   stop=KMB-68X-017; return and verify Previous still works.
4. Home produces onPause, as it does at the end of the supplied log. For final
   cleanup evidence, uninstall Bus Lite through Manage Apps or the Control
   Panel after finishing (this deletes favorites/recents). Capture onDestroy
   and resource-checkpoint teardown with tracked=0 and clean=1.

No app reinstall or firmware flash is needed to correct the server data.

**68X stop-17 retest: paging and app cleanup observed; firmware fix required.**
The next board log receives 1,665-byte stop responses and the Mac logs
stop=KMB-68X-017 in both directions. Uninstall reaches onDestroy and reports
153/512 KiB payload, 247 KiB peak, tracked=0, clean=1. App resource cleanup
passes this run. The LVGL task watchdog then fires during synchronous file
deletion, about five seconds after teardown; uninstall eventually succeeds.
This is a firmware package-deletion stall, not a Lua resource leak.

The current build fixes deletion by detaching the clean app and scheduling
one bounded filesystem cleanup step per 20 ms timer tick. Flash the new
application image with the existing app-flash procedure in Section 1. Firmware
SHA-256: 07ef3ea76f3be930fad88b0db6047032e0647a6880e583690be15ce72ae1587b.
No board flash was performed by the agent. Bus Lite CAPP remains 0.2.5.

After application flashing, reinstall the same signed Bus CAPP and repeat
68X stop 17, then uninstall. The launcher should remove Bus Lite and Settings
should respond while file reclamation finishes. Capture clean=1 and the
single App storage cleanup complete log, with no task_wdt warning. Immediate
reinstall may report App storage cleanup in progress; retry after completion.
Power loss during reclamation is recovered at boot. Uninstall still removes
saved stops/recents. Full ten-cycle P4 coverage and startup tuning remain open.

**Deferred uninstall retest: PASS (single P4 run).** The user confirms
resource-checkpoint teardown at uptime 339,100 ms with tracked=0 and clean=1,
156/512 KiB current Lua payload and 248 KiB peak. Manage Apps reports success
at 339,126 ms and refreshes its list at 339,141 ms. App storage cleanup complete
appears at 347,087 ms, about 7.96 seconds after logical uninstall. No watchdog
warning appears in the supplied excerpt. The uninstall watchdog regression
is closed for this run. Ten-cycle P4 stability, startup latency and remaining
explicit Stage C interaction checks are not established by this excerpt.

Stage C keeps the deterministic fixture for the selected route's stop sequence
and ETA response, while route discovery and stop metadata use the bundled
catalog. Recreate the fixture package after changing the Mac address, then
install it through the P4 developer control panel or with `crystal run`:

```sh
MAC_IP=192.168.1.195
DEVICE_URL=http://192.168.1.52/
PAIRING_PIN=123456
python3 tools/phase17_bus_fixture.py create --host "$MAC_IP"
python3 tools/crystal.py pack /tmp/phase17-bus \
  --key "$HOME/.crystal_keys/phase17-test-ed25519.pem" --key-id 240 \
  -o /tmp/phase17-bus.capp
python3 tools/crystal.py install --wifi "$DEVICE_URL" --pin "$PAIRING_PIN" /tmp/phase17-bus.capp
```

After applying the package, keep the fixture server running in a separate
terminal (`python3 tools/phase17_bus_fixture.py serve`) and check the following
workflow on the board:

1. Open Search and enter `1A`. Wait for the catalog status to finish. The route
   cards must come from the package and show a destination beginning with
   `To`; the Mac fixture must receive no `/api/routes` request.
2. Select the fixture's `68X` KMB route, verify that its 17-stop page contains
   the complete ordered sequence, and use Next/Previous. The visible rows must
   be reused and the page indicator must move without losing earlier or later
   stops.
3. Select a stop and confirm the fixture receives `/api/stops` and `/api/eta`
   with the selected provider-qualified route and stop. The ETA page remains a
   separate board with three arrival rows.
4. Search `101` to reach a recorded joint card. The card must show `KMB/CTB` and
   retain the route's destination. Selecting it must keep both provider parts
   in the saved/recent identity even though Stage C uses the first part for the
   fixture stop request.
5. If a stop response omits its name, verify the row says `Metadata unavailable
   - Retry`; press the header refresh and confirm the operator catalog is
   consulted again without a crash.
6. Type quickly, change the query before the catalog finishes, go Back, pause or
   leave the app, and return. A late catalog page must not replace the newer
   query or the current page. Capture the named `BUS_RESOURCE` checkpoints and
   confirm no quota toast, reset or retained-resource error.

The current catalog path uses one 100 ms timer for the 200 ms debounce and
bounded page continuation. Host checks assert the source remains below 64 KiB
and the production binding remains at or below 64 callbacks. Stage D adds live
provider requests, joint ETA merging and automatic refresh; do not mark those
items as Stage C failures.

## 10. Record the result

For each acceptance item, record **PASS**, **FAIL**, or **NOT RUN**, along with
the date, board revision, firmware SHA-256 (`shasum -a 256 build/crystal_os.bin`),
and the serial log filename. A host test or successful firmware build is not a
substitute for any board result above.

| Acceptance item | Result | Evidence / notes |
| --- | --- | --- |
| `crystal run` pack/install/launch/log stream | PASS | Signed install/launch returned success; Clock tabs were subsequently confirmed working on the P4. |
| Timer fires after Clock destruction | PASS | 300-second timer expired and displayed `Timer finished` after Clock destroy/uninstall; P4 serial log dated 2026-11-15. Speaker initialization failed, so audible alarm is unverified. |
| Runaway callback closes app without reset | PASS (serial evidence) | 2026-11-15 P4 capture: 50 ms limit, App stopped toast, onPause/onDestroy, then about 30 seconds of continued UI redraws without reset. Visual launcher/touch check and uninstall pending; flashed/local ELF mismatch noted in Step 5. |
| HTTP callback, allowlist and request quota | PASS (serial evidence) | 2026-11-15 P4 capture, OS 0.17.0, silicon revision v1.3: request id 1 returned status 200 with `{"ok":true}`; quota and allowlist errors matched the expected results. |
| HTTP cancellation during app destruction | PASS (serial evidence) | Same capture: the Mac received the second `/slow`; owner cancellation was requested, request id 3 completed with `ESP_ERR_INVALID_STATE` and `cancelled=1`, with no Lua callback. Worker-check request returned 200 afterward; both test apps were uninstalled. ELF SHA prefix `177c07ebe`; full image hash and physical board revision unrecorded. |
| Bus Lite bounded Script package and cleanup | PASS (bounded fixture flow) | Earlier P4 run reported empty stops; fixed Lua append argument loss, 32-bit sentinel overflow and the saved-stop callback timeout caused by synchronous state/UI work in an ETA callback. Version 0.2.3 follows the native layout and adds persistent recent routes. The developer-panel retest completed search, stops, ETA, Favorites and reopening a saved stop without the previous crash. Host package and production-binding/LVGL checks pass at 480/720 pixels. Numeric `BUS_RESOURCE` capture, repeated cleanup cycles and the catalog/live-provider stages remain open. |
| Stage B catalog generation and package limits | PASS (host) | Recorded snapshot produced 2,115 routes (160 joint), 6,753 KMB stops and 2,586 CTB stops in 115 files; JSON Lines pages are at most 16 KiB, total catalog assets are 1,802,584 bytes, and the package remains below the 5.5 MiB limit. |
| Stage C catalog-backed search and paging | PARTIAL PASS (board) | 0.2.4 launched but search hit the 50 ms callback limit. Corrected 0.2.5 keeps 512 KiB and limits each timer tick to one file read or one needed JSON decode. Host work-bound and coverage checks pass. The latest P4 excerpt reaches search/stops/ETA without quota or callback failure (247/512 KiB peak payload); startup remains 746 ms. 68X stop 17 is now reachable in both directions and Lua teardown is clean (tracked=0 clean=1); synchronous filesystem deletion triggered the LVGL watchdog. Deferred cleanup now also passes a single P4 retest: uninstall succeeds at 339,126 ms and App storage cleanup complete appears at 347,087 ms without a watchdog warning in the excerpt. Signed `/tmp/phase17-bus-stagec.capp` is 1,847,721 bytes, SHA-256 `48cd34b505df5c898f6eefbabea927d69a5db45170242d608ca4717216326bac`. Search, stop-17 reachability in both directions and single-run cleanup have board evidence. Missing metadata retry, explicit joint identity/cancellation/late-page checks, startup latency and repeat-cycle coverage remain open. |
| Stage C deferred uninstall and storage cleanup | PASS (single P4 run) | teardown tracked=0 clean=1; 248/512 KiB peak payload. Manage Apps succeeds before incremental reclamation finishes about eight seconds later. Completion log observed; no task_wdt warning in the supplied excerpt. Ten-cycle hardware coverage remains pending. |

After all tests, uninstall remaining test packages. USB package uninstall does
not remove signing trust. If you provisioned a test key, record its ID and
whether it remains trusted; this firmware has no user-facing key-removal path.
Do not erase NVS as cleanup, since it also contains other device settings.
After archiving the test evidence, securely delete the test private key if you
no longer need it; deleting that file does not remove the public-key trust from
the board. Arrange a separately reviewed trust-store removal process before
reusing the board for another purpose.

Board record:

- Date:
- Board revision:
- Firmware SHA-256:
- Serial log file:
- Signing key ID used (if applicable):
