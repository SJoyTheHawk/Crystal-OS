# App Platform v2 Phase 15 and 16 Test Guide

Use this guide to record the remaining Phase 15 and Phase 16 exit evidence on a
real ESP32-S3 board. Check a box only after the step has been observed and save
the command output, serial log, screenshot, or HTTP response as evidence.

The host tests prove implementation behavior, but they do not close the
hardware-only items. USB developer install accepts an unsigned package. Wi-Fi
install is authenticated and accepts signed packages only after the matching
trusted public key has been provisioned on the device.

## Test record

- [ ] Tester: ______________________________
- [ ] Date/time: ___________________________
- [ ] Firmware commit/build: _______________
- [ ] Board serial port: `/dev/cu.usbmodem101` (or __________________)
- [ ] Board Wi-Fi address: `http://________________/`
- [ ] Trusted package key ID: __________
- [ ] Evidence directory or issue link: ______________________________

## 1. Prepare the test environment

- [ ] Connect the board by USB Serial/JTAG and confirm the port with `ls
  /dev/cu.usb*`.
- [ ] From the repository root, activate the ESP-IDF environment:
  `source "$HOME/esp/esp-idf/export.sh"` (use the local ESP-IDF path if it
  differs).
- [ ] Confirm Python 3, `node`, and a browser are available.
- [ ] Confirm the display touch input works; it is needed to enable the local
  control panel and to compare the on-device Manage Apps page.
- [ ] Provision the release/test Ed25519 public key in the device trust store
  before attempting a Wi-Fi package install. Record the key ID above.
- [ ] Capture serial logs for boot, Wi-Fi install, and launch tests. Close any
  monitor using the transfer port before flashing or USB installation. The USB
  installer must be the only application reading `/dev/cu.usbmodem101`; a monitor
  on that port can consume protocol replies. A monitor on a separate UART port
  may remain open. Reopen the USB monitor after the installer exits.

## 2. Host regression checks

Run these from the repository root. A command must return exit code 0. The
application-platform test intentionally prints `FAIL` for malformed or
rejected negative cases; those lines are expected when the final summary and
exit code pass.

- [x] `python3 -m py_compile tools/crystal.py`
- [x] `git diff --check`
- [x] `python3 tools/test_app_platform_v2.py`
- [x] `python3 tools/test_manage_apps_page.py`
- [x] `python3 tools/test_crystal_declarative.py`
- [x] `python3 tools/test_crystal_registry.py`
- [x] `python3 tools/test_crystal_state.py`
- [x] `python3 tools/test_crystal_crypto.py`
- [x] `python3 tools/test_crystal_wifi.py`
- [x] `python3 tools/test_crystal_memory.py` (host allocator capability, failure,
  reallocation, and cleanup checks; device heap measurements still required)
- [x] `python3 tools/test_crystal_signatures.py` using the ESP-IDF Python
  environment when the system Python lacks the package dependencies.
- [x] Parse the browser panel JavaScript:

  ```sh
  node -e "const fs=require('fs');const s=fs.readFileSync('assets/control-panel/index.html','utf8').match(/<script>([\\s\\S]*)<\\/script>/)[1];new Function(s);console.log('PASS: control-panel JavaScript parses')"
  ```

- [] Save the output of every command in the evidence location above.

## 3. Build, flash, and boot smoke test

- [x] Build the firmware:

  ```sh
  source "$HOME/esp/esp-idf/export.sh"
  ninja -C build -j2
  ```

- [ ] Record the application size and partition headroom from the build output.
  ```bash
  LittleFS Configuration:
    Block Size:       4096  /  0x1000
    Image Size:    5767168  /  0x580000
    Block Count:      1408
    Name Max:           64
    Image:       /Users/szemy/Workspace/ESP32 Crystal OS/build/apps.bin
  Adding Directory: apps
  Adding File:      apps/.keep
  ```
- [x] Flash the board and open the monitor using the repository's normal
  `idf.py` command for this board.
- [x] The serial log shows a clean boot with no panic or Guru Meditation.
- [x] The log contains `LittleFS mounted at /littlefs; package storage ready`.
- [x] The log lists the built-in apps and contains
  `USB developer transport ready (CRYSTAL/1 INSTALL <bytes>)`.
- [x] The log contains `Startup baseline: automatic Phase 14 device test
  disabled` and a `first-frame baseline` line.
- [x] Record any boot warning. In particular, a missing initial package
  signing key blocks signed Wi-Fi tests until the key is configured.
- [x] With USB developer transport enabled, the Wi-Fi startup log reports
  `static_rx=10 static_tx=8` and `WiFi initialized`. Record the internal heap
  and largest DMA block from the startup lines. There must be no
  `malloc buffer fail` or partial RX-buffer allocation.
- [ ] With saved network credentials, verify an IP address is obtained after
  a cold boot. Scan for networks, disconnect/reconnect, and enable the local
  panel while Weather performs HTTPS requests. Repeat with a declarative app
  installed; verify USB package transfer still completes with Wi-Fi active.

## 4. Phase 15 USB package installation

This validates the implemented physical developer transport. It does not
validate Browser Web Serial, which remains an open item.

- [x] Pack the Phase 16 fixture as an unsigned developer package:

  ```sh
  ./tools/crystal pack examples/declarative_dashboard --unsigned \
    -o /tmp/crystal-phase15-usb.capp
  ./tools/crystal inspect /tmp/crystal-phase15-usb.capp
  ```

- [x] Install the package over USB:

  Exit IDF Monitor with `Ctrl+]` and close other serial terminals using the
  transfer port before running this command. Keep them closed until it returns.

  ```sh
  ./tools/crystal install --usb --port /dev/cu.usbmodem101 \
    /tmp/crystal-phase15-usb.capp
  ```

- [x] The CLI returns JSON with `"installed": true` and
  `"restart_required": true`.
- User-reported evidence: pack/inspect succeeded and the second USB install
  returned `{"installed": true, "restart_required": true}`. The first attempt
  was interrupted while waiting for `READY`; the monitor reported a possible
  multiple-access error. Repeat transfers with exclusive access to the port.
- [x] After the installer exits, reconnect the monitor with
  `idf.py -p /dev/cu.usbmodem101 monitor` and verify there is no panic or crash
  recovery indication. Save the successful CLI response as transfer evidence.
- [x] Reboot the board and confirm the package count increases and the
  `org.crystal.declarative-dashboard` package is discovered at boot.
- [ ] Install the package from its source folder as a second transport check:

  ```sh
  ./tools/crystal install --usb --port /dev/cu.usbmodem101 \
    examples/declarative_dashboard
  ```

- [ ] Send one malformed or truncated USB frame and confirm the device returns
  an error without leaving a partial package or changing the registry.
- [ ] Confirm a power/reboot during or immediately after installation leaves
  either the previous package or the complete new package, never a partial
  `.capp`.

## 5. Phase 15 pairing and authenticated Wi-Fi API

- [ ] On the device open `Settings -> Developer Tools -> Local Control Panel`
  and enable the panel for the current boot.
- [ ] Record the device IP address and the six-digit PIN shown on the device.
- [ ] Open `http://<device-ip>/` in a browser and confirm the static control
  panel loads.
- [ ] Exchange the PIN in the panel and record the returned bearer token.
- [ ] Confirm a second exchange with the same PIN is rejected.
- [ ] Confirm the PIN expires or is invalid after the documented enable window.
- [ ] Without a token, confirm the API rejects the request:

  ```sh
  curl -i "http://<device-ip>/api/v1/system"
  ```

  Expected result: HTTP `401`.

- [ ] With the bearer token, confirm both endpoints return HTTP `200` JSON:

  ```sh
  curl -i -H "Authorization: Bearer <TOKEN>" \
    "http://<device-ip>/api/v1/system"
  curl -i -H "Authorization: Bearer <TOKEN>" \
    "http://<device-ip>/api/v1/apps"
  ```

- [ ] Confirm an invalid, expired, or altered token returns HTTP `401` for
  every protected endpoint.
- [ ] Confirm the panel reports the same app IDs, versions, enabled state, and
  restart state as `/api/v1/apps`.

## 6. Phase 15 Wi-Fi installation, progress, run, and logs

Use a signed package made with the private key whose public key is provisioned
on the device. Do not treat an unsigned package refusal as a failure; it is the
required production policy for Wi-Fi.

- [ ] Create a signed package:

  ```sh
  ./tools/crystal pack examples/declarative_dashboard \
    --key /secure/package-private.pem --key-id <KEY_ID> \
    -o /tmp/crystal-phase15-wifi.capp
  ./tools/crystal verify /tmp/crystal-phase15-wifi.capp \
    --trusted-key <KEY_ID>=/secure/package-public.pem
  ```

- [ ] Install the signed package from the CLI using the PIN:

  ```sh
  ./tools/crystal install /tmp/crystal-phase15-wifi.capp \
    --wifi http://<device-ip>/ --pin <PIN>
  ```

- [ ] CLI output reports a successful install and returns a bearer token.
- [ ] Attempt the same Wi-Fi install with an unsigned package or an unknown
  key and confirm the API rejects it without changing the registry.
- [ ] In the browser panel upload a package larger than 1 MB and confirm a
  visible progress indicator advances before the final success response.
- [ ] Confirm the browser reports the same installed package and restart
  requirement as the CLI.
- [ ] Start a live log stream in a second terminal:

  ```sh
  ./tools/crystal logs -f --wifi http://<device-ip>/ --token <TOKEN>
  ```

- [ ] While the stream is open, launch or enable/disable an app and observe
  bounded lifecycle/debug events in the stream.
- [ ] Run the complete Wi-Fi command, using the signed key:

  ```sh
  ./tools/crystal run examples/declarative_dashboard \
    --wifi http://<device-ip>/ --pin <PIN> \
    --key /secure/package-private.pem --key-id <KEY_ID>
  ```

- [ ] Confirm `crystal run` packs, installs, launches, and produces lifecycle
  output without a reboot or crash.
- [ ] Confirm `crystal logs -f` stops cleanly when the device disables the
  panel or the network connection is closed.

## 7. Phase 15 app management and consistency

- [ ] From the external panel launch an installed app.
- [ ] Disable it, refresh the app list, then enable it again.
- [ ] Clear its data and verify the action reports success.
- [ ] Move the app up and down in the external ordering controls.
- [ ] Uninstall the test app and confirm it disappears from both the panel and
  `/api/v1/apps`.
- [ ] Reinstall it, reboot the board, and confirm boot reconciliation restores
  the same package and order.
- [ ] Open the on-device `Manage Apps` page and compare the package ID,
  version, enabled state, ordering, launch, clear-data, and uninstall results
  with the external panel after each edit.
- [ ] Record any mismatch as a Phase 15 exit-gate failure; do not close the
  phase based only on a host or API response.
- [ ] **Blocked/open:** Browser Web Serial transport is not implemented. Leave
  this box unchecked until a Web Serial implementation and board validation
  exist.
- [ ] **Blocked/open:** On-device catalog browsing and downloading are not
  implemented. Host catalog signing/list/download tests do not close this item.

## 8. Phase 16 declarative runtime host checks

- [ ] Run `python3 tools/test_crystal_declarative.py` and retain its output.
- [ ] Inspect the fixture package and confirm it contains bounded `ui.json` and
  optional `bindings.json` data:

  ```sh
  ./tools/crystal pack examples/declarative_dashboard --unsigned \
    -o /tmp/declarative-dashboard.capp
  ./tools/crystal inspect /tmp/declarative-dashboard.capp
  ```

- [ ] Confirm host validation covers containers, labels, buttons, basic
  controls, tab views, button matrices, image, list, chart, textarea,
  spinner, and dialog widgets.
- [ ] Confirm host validation covers time, network, and timer bindings.
- [ ] Confirm allowlisted timer actions are accepted and unknown actions are
  rejected.
- [ ] Confirm the visual references for Clock, Weather, and Calculator remain
  expressible without adding ABI calls.
- [ ] **Open:** dynamic list/chart operations and input focus are not complete;
  leave the corresponding Phase 16 exit checkbox unchecked.

## 9. Phase 16 device lifecycle and binding checks

**Reported failure:** USB installation returned `installed: true`, but opening
Status Dashboard crashed the device. Keep the device runtime checks open until
the launch failure is diagnosed from the serial crash log and a corrected build
passes on the board. Successful transfer does not close the launch test.

The supplied backtrace reaches picolibc's file-lock allocation from
`crystal_package_read_file` while loading the dashboard image. The same boot
connected to Wi-Fi but HTTPS failed with `ESP_ERR_NO_MEM`. The correction routes
LVGL widget/style allocations and cJSON nodes to PSRAM and uses VFS descriptor
reads for package assets, avoiding a new stdio lock per asset. Task stacks stay
internal: the USB installer and UI write flash, and IDF v6.1 can still disable
the cache during writes with flash mappings present, even with instruction and
rodata relocation enabled. Moving these stacks requires a separate flash-worker
design or proving all relevant cache-disabled paths safe.

- [ ] After app-only flashing, confirm the boot message
  `UI and JSON allocators use PSRAM; task stacks remain internal`.
- [ ] Save internal heap measurements before/after Wi-Fi and during HTTPS.
  Verify Weather and the HTTPS smoke request succeed, then open the dashboard
  with Wi-Fi active and repeat while the Local Control Panel is enabled.

- [ ] With the declarative package installed over USB or signed Wi-Fi, reboot
  the board and launch it from the on-device launcher.
- [ ] Confirm the app creates its tabs and widgets without a watchdog reset,
  allocation failure, or LVGL assertion.
- [ ] Confirm the visible time/network values update while the app remains open.
- [ ] Confirm timer actions run only when the package has the required timer
  permission and are routed only to allowlisted actions.
- [ ] Switch away from the app and back several times; confirm widgets and
  bindings are recreated without duplicated timers or stale values.
- [ ] Close, disable, uninstall, and reinstall the app; confirm cleanup and
  fresh state after reinstall.
- [ ] Capture serial and `crystal logs -f` output for launch, destroy, and
  binding events.
- [ ] Repeat the external-panel and on-device Manage Apps comparison after
  this lifecycle test.
- [ ] **Open:** complete documented property/event APIs, dynamic list/chart
  behavior, and input focus still require implementation and tests.
- [ ] **Open:** the documented device lifecycle/binding behavior is not closed
  until this hardware sequence passes.
- [ ] **Open:** ABI v1 is not frozen until the remaining facade surface is
  implemented and reviewed.

## 10. Exit record

### Phase 15

- [ ] Local HTTP API hardware validation passed.
- [ ] Wi-Fi control panel served and loaded.
- [ ] USB browser/Web Serial transport passed. **Leave unchecked: not implemented.**
- [ ] Pairing PIN was shown on-device and accepted once.
- [ ] Bearer token was issued once and enforced on all protected requests.
- [ ] Ed25519 signed Wi-Fi package was accepted with the configured trusted key.
- [ ] Unsigned/unknown-key Wi-Fi package was rejected.
- [ ] USB developer package installed, reboot-reconciled, and left no partial data.
- [ ] Browser upload progress was visible for a package larger than 1 MB.
- [ ] `crystal logs -f` streamed authenticated lifecycle/debug events.
- [ ] `crystal run` completed pack, install, launch, and log streaming.
- [ ] External panel and on-device Manage Apps stayed consistent after edits.
- [ ] On-device catalog browsing/download passed. **Leave unchecked: not implemented.**
- [ ] Phase 15 exit gate: web UI and CLI work, and unauthenticated requests are rejected.

### Phase 16

- [ ] DeclarativeApp lifecycle passed on the board.
- [ ] Bounded UI and binding parsing passed on host and device.
- [ ] All documented visual widgets rendered on the board.
- [ ] Time, network, and timer bindings updated correctly on the board.
- [ ] Allowlisted timer actions routed correctly.
- [ ] Dynamic list/chart APIs and input focus passed. **Leave unchecked while open.**
- [ ] Device lifecycle and cleanup passed across launch, hide, destroy, reinstall,
  and reboot.
- [ ] Clock, Weather, and Calculator visual references remain expressible.
- [ ] ABI v1 was reviewed and frozen. **Leave unchecked while the facade is incomplete.**

## Closure decision

- [ ] Phase 15 is closed: every required Phase 15 box above is checked, and all
  blocked/open items have been implemented and hardware-validated.
- [ ] Phase 16 is closed: every required Phase 16 box above is checked, and the
  remaining facade APIs, device lifecycle checks, and ABI review are complete.
- [ ] Any unchecked box has an owner, issue, and next test date recorded in the
  evidence location.
