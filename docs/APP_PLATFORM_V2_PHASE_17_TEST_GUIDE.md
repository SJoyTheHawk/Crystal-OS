# Phase 17 board test guide

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

For the native USB path, reboot the board and reopen IDF Monitor. For Wi-Fi,
reboot only if the install response reports `restart_required: true`, then
reopen Monitor as needed. Launch **Phase 17 Runaway Test** from
the launcher and tap **Run forever**. Confirm the **App stopped** toast appears,
the test app closes back to the launcher, and the serial log records the
50 ms callback limit without a panic, watchdog reset, or board reboot. Remove
the test package through **Settings → Manage Apps** after recording the result.

**Board result (2026-11-15): PASS for the logged timeout/closure/no-reset
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

## 7. Bus app conversion and cleanup gate

This is implementation plus acceptance work, not a test that can be run on the
current Lua Clock package. Preserve the extracted native snapshot. First review
[`examples/bus_app/ARCHITECTURE_HANDOFF.md`](../examples/bus_app/ARCHITECTURE_HANDOFF.md)
and [`examples/bus_app/docs/bus-app-workflow.md`](../examples/bus_app/docs/bus-app-workflow.md);
record which user-facing scope is being migrated (at minimum route/stop lookup,
favorites and live ETA) and the required KMB/CTB hosts. Check each flow against
the current Lua HTTP, JSON response, memory, storage and UI limits. The old
full catalogs and direct native service are not automatically compatible with
the script quotas; reduce the initial catalog scope or implement and validate
a bounded host capability before proceeding if a flow exceeds them.

Create a separate script package, keep provider-specific logic inside it, and
use only current runtime APIs. Run the fixture parser/catalog check documented
in [`examples/bus_app/README.md`](../examples/bus_app/README.md):

```sh
PYTHONDONTWRITEBYTECODE=1 python3 examples/bus_app/tools/bus_catalog/test_stop_catalogs.py
```

Treat that guide's documented `--full` KMB artifact mismatch as an open issue,
not as a passing golden test. Pack and inspect the new `.capp`, then test the
agreed route/stop, favorites and network flows on the board. Finally repeat
open → load → leave → uninstall at least ten times. Record cleanup evidence
after each cycle; the registry must report no retained app resources, the
package must uninstall cleanly, and the next launch/network request must still
work. Record every unimplemented or out-of-quota Bus capability before
claiming Phase 17 complete.

## 8. Record the result

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
| Bus Script conversion and cleanup | | |

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
