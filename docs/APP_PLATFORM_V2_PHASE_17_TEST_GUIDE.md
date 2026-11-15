# Phase 17 board test guide

This runbook covers the Phase 17 acceptance checks that need an ESP32-P4. It
assumes the board is on the same LAN as the development Mac. Record failures
with the board revision, firmware image SHA-256, date, and relevant serial log.
These procedures are not board results; complete the result table at the end
after running them on the target.

## 1. Build and flash the Phase 17 firmware

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
this monitor open for the USB-installed tests below. Exit with `Ctrl+]` before
running a USB package install, then reopen it after install and reboot with
`idf.py -p "$PORT" monitor -b 1000000 | tee -a "$LOG"`.
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

## 5. Verify a runaway Lua callback closes only its app

Create an unsigned development package in `/tmp`:

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

Exit IDF Monitor with `Ctrl+]`, then install over USB (physical USB install
accepts unsigned packages for development):

```sh
python3 tools/crystal.py install --usb --port "$PORT" /tmp/phase17-runaway.capp
```

Reboot the board and reopen IDF Monitor. Launch **Phase 17 Runaway Test** from
the launcher and tap **Run forever**. Confirm the **App stopped** toast appears,
the test app closes back to the launcher, and the serial log records the
50 ms callback limit without a panic, watchdog reset, or board reboot. Remove
the test package through **Settings → Manage Apps** after recording the result.

## 6. Verify HTTP success, allowlist and quota behavior

Use the Mac's LAN IPv4 address, not `127.0.0.1`. For Wi-Fi `en0`, find it with
`ipconfig getifaddr en0`; use the active interface if the result is empty. Start
this temporary HTTP fixture in a separate terminal. Allow incoming TCP traffic
to port 8000 in the Mac firewall if it is enabled. The fixture responds
immediately to `/ok` and delays `/slow` long enough to test cancellation:

```sh
python3 -u - <<'PY'
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from time import sleep

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        print(f"request {self.path}", flush=True)
        if self.path == "/slow": sleep(20)
        body = b'{"ok":true}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try: self.wfile.write(body)
        except BrokenPipeError: pass
    def log_message(self, *_): pass

ThreadingHTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
PY
```

In another terminal, create the test packages. Replace every `192.168.1.20`
in both package directories with the Mac's LAN IPv4 address. The host in
`network_hosts` must exactly match the host in the Lua URLs; the port is not
part of the allowlist entry.

```sh
mkdir -p /tmp/phase17-http
cat > /tmp/phase17-http/app.yaml <<'YAML'
abi: 1
id: com.example.phase17.http
name: Phase 17 HTTP Test
version: 1.0.0
min_os: 0.17.0
author: Crystal OS test
license: MIT
runtime: lua
entry: main.lua
bar_style: dark
memory_kb: 160
storage_kb: 8
worker_threads: 1
permissions:
  network_hosts:
    - 192.168.1.20
YAML
cat > /tmp/phase17-http/main.lua <<'LUA'
local http = crystal.service.http
local base = "http://192.168.1.20:8000"
local M = {}

function M.onCreate()
  local id, err = http.fetch(base .. "/slow", {}, function(_, status, body)
    print("PHASE17_HTTP_CALLBACK", status, body)
    local cancel_id, cancel_err = http.fetch(base .. "/slow", {}, function()
      print("PHASE17_CANCEL_CALLBACK_UNEXPECTED")
    end)
    print("PHASE17_CANCEL_REQUEST", cancel_id or "nil", cancel_err or "")
  end)
  print("PHASE17_HTTP_STARTED", id or "nil", err or "")

  local extra, extra_err = http.fetch(base .. "/ok", {}, function()
    print("PHASE17_QUOTA_CALLBACK_UNEXPECTED")
  end)
  print("PHASE17_QUOTA", extra or "nil", extra_err or "")

  local denied, denied_err = http.fetch("http://example.invalid/ok", {}, function()
    print("PHASE17_ALLOWLIST_CALLBACK_UNEXPECTED")
  end)
  print("PHASE17_ALLOWLIST", denied or "nil", denied_err or "")
  return true
end

function M.onResume() return true end
function M.onPause() return true end
function M.onDestroy() return true end
return M
LUA
```

Create a second package to prove that cancelling the first app's owner request
does not leave the shared worker stuck:

```sh
mkdir -p /tmp/phase17-http-worker-check
cat > /tmp/phase17-http-worker-check/app.yaml <<'YAML'
abi: 1
id: com.example.phase17.httpworker
name: Phase 17 HTTP Worker Check
version: 1.0.0
min_os: 0.17.0
author: Crystal OS test
license: MIT
runtime: lua
entry: main.lua
bar_style: dark
memory_kb: 160
storage_kb: 8
worker_threads: 1
permissions:
  network_hosts:
    - 192.168.1.20
YAML
cat > /tmp/phase17-http-worker-check/main.lua <<'LUA'
local http = crystal.service.http
local M = {}

function M.onCreate()
  local id, err = http.fetch("http://192.168.1.20:8000/ok", {}, function(_, status, body)
    print("PHASE17_WORKER_RECOVERY_CALLBACK", status, body)
  end)
  print("PHASE17_WORKER_RECOVERY_STARTED", id or "nil", err or "")
  return true
end

function M.onResume() return true end
function M.onPause() return true end
function M.onDestroy() return true end
return M
LUA
```

Pack unsigned, exit IDF Monitor, upload and reboot as in Step 5:

```sh
python3 tools/crystal.py pack /tmp/phase17-http --unsigned \
  -o /tmp/phase17-http.capp
python3 tools/crystal.py pack /tmp/phase17-http-worker-check --unsigned \
  -o /tmp/phase17-http-worker-check.capp
python3 tools/crystal.py install --usb --port "$PORT" /tmp/phase17-http.capp
python3 tools/crystal.py install --usb --port "$PORT" /tmp/phase17-http-worker-check.capp
```

After reboot, open Monitor and launch **Phase 17 HTTP Test**. Confirm the Mac
server sees `/slow` and the device logs show:

- `PHASE17_HTTP_STARTED` with a request ID.
- `PHASE17_QUOTA nil worker quota exceeded` while the first request is active.
- `PHASE17_ALLOWLIST nil network host is not allowed` for
  `example.invalid`.
- After about 20 seconds, `PHASE17_HTTP_CALLBACK 200` with the JSON body and a
  `PHASE17_CANCEL_REQUEST` line with a request ID.

As soon as `PHASE17_CANCEL_REQUEST` appears, uninstall **Phase 17 HTTP Test**
from **Settings → Manage Apps** while its second `/slow` request is in flight.
Wait at least 25 seconds. Pass the cancellation check if the app is removed,
the OS stays responsive, and `PHASE17_CANCEL_CALLBACK_UNEXPECTED` never
appears. The server may report a closed client when the request is cancelled;
that is expected. Then launch **Phase 17 HTTP Worker Check**. Confirm
`PHASE17_WORKER_RECOVERY_STARTED` has a request ID and
`PHASE17_WORKER_RECOVERY_CALLBACK 200` appears. This confirms the shared worker
still accepts and completes a request after owner cancellation. Uninstall the
worker-check package. Stop the local fixture with `Ctrl+C` and remove either
package if it is still installed.

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
| `crystal run` pack/install/launch/log stream | | |
| Timer fires after Clock destruction | | |
| Runaway callback closes app without reset | | |
| HTTP callback, allowlist and request quota | | |
| HTTP cancellation during app destruction | | |
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
