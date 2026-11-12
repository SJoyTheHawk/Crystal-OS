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
- [x] Firmware build identified: `crystal_os` version `0.12.14`, compiled
  `Nov 12 2026 04:53:57`, ESP-IDF `v6.1`, ELF SHA256 prefix `27bdd384f...`.
  Git commit is not identified by the supplied log.
- [x] Board serial port: `/dev/cu.usbmodem1101`
- [x] Board Wi-Fi address: `http://192.168.1.158/` (assigned in the supplied boot log).
- [ ] Trusted package key ID: __________
- [x] Evidence directory: [test-evidence/phase15-16](test-evidence/phase15-16/).
  [User-supplied boot log, recorded 2026-11-12](test-evidence/phase15-16/2026-11-12-boot.txt).
  [Application flash and subsequent boot, recorded 2026-11-12](test-evidence/phase15-16/2026-11-12-app-flash-boot.txt).

## 1. Prepare the test environment

- [x] Connect the board by USB Serial/JTAG and confirm the port with `ls
  /dev/cu.usb*`.
  User-reported evidence: `ls /dev | grep cu.usb` returned `cu.usbmodem1101`.
- [x] From the repository root, activate the ESP-IDF environment:
  `source "$HOME/.espressif/v6.1/esp-idf/export.sh"`.
  User-reported evidence: activation and Python dependency checks succeeded;
  Python 3.10.9 was detected and `idf.py --version` returned `ESP-IDF v6.1`.
  The unused-tools notice did not prevent activation.
- [x] Confirm Python 3, `node`, and a browser are available.
  User-reported evidence: `python3 --version` returned `Python 3.10.9`,
  `node -v` returned `v24.15.0`, and Safari and Chrome are available.
- [x] Confirm the display touch input works; it is needed to enable the local
  control panel and to compare the on-device Manage Apps page.
  User-reported evidence: display touch input confirmed working.
- [ ] Provision the release/test Ed25519 public key in the device trust store
  before attempting a Wi-Fi package install. Record the key ID above.
  Verification pending: the current `sdkconfig` and generated
  `build/config/sdkconfig.h` both select key ID `1` but leave
  `CONFIG_CRYSTAL_PACKAGE_TRUSTED_PUBLIC_KEY_HEX` empty. The trust-store
  implementation exists; this configuration does not provision a public key.
  A key provisioned by an earlier build may remain in NVS, so this finding
  alone does not establish the device's stored keys. Confirm the actual key
  ID with provisioning evidence or successful signed-package verification
  before marking this item complete.
  The supplied device boot log also reports `No initial package signing key
  configured; signed packages will be rejected`. No successful provisioning
  message or signed-package verification is present in this capture.
- [ ] Capture serial logs for boot, Wi-Fi install, and launch tests. Close any
  monitor using the transfer port before flashing or USB installation. The USB
  installer must be the only application reading `/dev/cu.usbmodem1101`; a monitor
  on that port can consume protocol replies. A monitor on a separate UART port
  may remain open. Reopen the USB monitor after the installer exits.
  Partial evidence: the boot log is saved in the evidence directory above.
  Wi-Fi installation and installed-dashboard launch logs are still pending;
  the application-flash transcript confirms port access for flashing and
  subsequent monitor startup. Exclusive access during USB package transfer
  still requires confirmation in the transfer test.

## 2. Host regression checks

Run these from the repository root. A command must return exit code 0. The
application-platform test intentionally prints `FAIL` for malformed or
rejected negative cases; those lines are expected when the final summary and
exit code pass.

- [x] `python3 -m py_compile tools/crystal.py`
- [x] `git diff --check`
  User-reported evidence: both commands returned to the shell prompt with no
  errors or diagnostic output in the supplied terminal transcript.
- [x] `python3 tools/test_app_platform_v2.py`
  User-supplied [host integration output](test-evidence/phase15-16/2026-11-12-app-platform-v2.txt)
  reaches the final expected PASS and returns to the shell prompt without a
  traceback or sanitizer diagnostic. An explicit numeric exit code was not
  included. Checks pass for package validation, rollback, resource ownership,
  repeated Calculator lifecycle/cleanup, dashboard layout/bindings/permissions,
  and the Clock/Weather/Calculator visual references. The three `FAIL` lines
  are intentional rejection cases: an existing fixture, corrupt reboot state,
  and refusal to resume a failed run. Each is followed by its expected PASS.
  The device harness runs on the host with simulated reboot; this result does
  not close the corresponding board tests.
- [x] `python3 tools/test_manage_apps_page.py`
  User-supplied [Manage Apps host output](test-evidence/phase15-16/2026-11-12-manage-apps.txt)
  shows a completed build and all four PASS summaries: clear-data confirmation
  and isolation; row dragging, persistence, and rollback; uninstall
  confirmation/retry and catalog refresh; and LVGL layout, toggles, last-app
  protection, scrolling, rebuild, and teardown. No test failure or traceback
  is shown. The LVGL CMake `CMP0177` policy warning did not stop the build.
  An explicit numeric exit code was not included. On-device navigation and
  consistency with the external panel remain separate checks.
- [x] `python3 tools/test_crystal_declarative.py`
  User-supplied [declarative/registry transcript](test-evidence/phase15-16/2026-11-12-declarative-registry.txt)
  shows the declarative check returning silently without an error or traceback,
  as expected for this test. This checks example structure, bindings, timer
  actions, visual-reference documents, and package creation/inspection.
- [x] `python3 tools/test_crystal_registry.py`
  The same transcript reaches the final registry PASS summary and returns to
  the prompt. Null/empty IDs, storage write failures, invalid table entries,
  and failed app factories/installation are deliberately exercised by the test;
  their error messages are expected. The assertions cover defaults, enabled
  state and slot persistence, bounds, ordering, and failure handling. Neither
  command's explicit numeric exit code was included in this transcript.
- [x] `python3 tools/test_crystal_state.py`
  User-supplied [state/crypto/Wi-Fi/memory transcript](test-evidence/phase15-16/2026-11-12-state-crypto-wifi-memory.txt)
  includes all three state PASS summaries: failed-create resource cleanup,
  namespace-isolated state clearing, and app clear-data lifecycle guards/retry.
  The NVS open/enumeration/erase/commit errors are deliberate injected failures.
- [x] `python3 tools/test_crystal_crypto.py`
  The same transcript reports PASS for provider selection, boot entropy
  lifecycle, idempotence, pairing randomness, offline Ed25519, and tamper
  rejection. This host result does not establish device key provisioning.
- [x] `python3 tools/test_crystal_wifi.py`
  The same transcript reports `Wi-Fi scan/reconnect regression checks passed`.
  Actual board scan/reconnect and concurrent-transfer checks remain separate.
- [x] `python3 tools/test_crystal_memory.py` (host allocator capability, failure,
  reallocation, and cleanup checks; device heap measurements still required)
  The same transcript reaches PASS for LVGL/JSON PSRAM allocation capabilities,
  realloc preservation, allocation failure without internal fallback, and
  cleanup. Six managed-cJSON `sprintf` deprecation warnings did not prevent
  completion. All four commands return to the prompt without a traceback;
  explicit numeric exit codes were not included in this transcript.
- [x] `python3 tools/test_crystal_signatures.py` using the ESP-IDF Python
  environment when the system Python lacks the package dependencies.
  User-supplied [host command output](test-evidence/phase15-16/2026-11-12-host-command-checks.txt)
  reports PASS for Ed25519 package/catalog signing, trusted-key lookup, tamper
  rejection, and HTTPS policy. This test generates temporary host keys; it
  does not provision the device trust store or validate a signed Wi-Fi install.
- [x] Parse the browser panel JavaScript:

  ```sh
  node -e "const fs=require('fs');const s=fs.readFileSync('assets/control-panel/index.html','utf8').match(/<script>([\\s\\S]*)<\\/script>/)[1];new Function(s);console.log('PASS: control-panel JavaScript parses')"
  ```

  The same transcript reports `PASS: control-panel JavaScript parses`. This
  confirms script syntax; browser behavior remains a separate device test.
  Explicit numeric exit codes were not included for these two commands.

- [x] Save the output of every command in the evidence location above.
  All supplied host-check transcripts are archived under
  [test-evidence/phase15-16](test-evidence/phase15-16/), including the two
  initial silent checks in `2026-11-12-host-command-checks.txt`.

## 3. Build, flash, and boot smoke test

- [x] Build the firmware:

  ```sh
  source "$HOME/.espressif/v6.1/esp-idf/export.sh"
  idf.py build
  ```

  User-supplied [firmware build output](test-evidence/phase15-16/2026-11-12-firmware-build.txt)
  reports `Project build complete` and `Exit code: 0`. The LittleFS image
  `build/apps.bin` was generated at `0x580000` bytes (5767168 bytes), with
  4096-byte blocks and 1408 blocks. Image generation is distinct from the
  previously observed device mount.
- [x] Record the application size and partition headroom from the build output.
  Application: `0x2f1000` bytes in a `0x400000`-byte partition, leaving
  `0x10f000` bytes (26%) free. Bootloader: `0x58b0` bytes, leaving
  `0x2750` bytes (31%) free.
- [x] Flash the board and open the monitor using the repository's normal
  `idf.py` command for this board.
  User-supplied [app-flash/boot output](test-evidence/phase15-16/2026-11-12-app-flash-boot.txt)
  shows `idf.py -p /dev/cu.usbmodem1101 app-flash monitor`, successful
  verification of `crystal_os.bin` at `0x00030000`, hardware reset, and monitor
  startup on the same port. This app-only operation targets the application
  image; no filesystem image is flashed.
- [x] The serial log shows a clean boot with no panic or Guru Meditation.
  No reset, panic, or watchdog failure is shown in the supplied excerpt, which
  ends at the first HTTPS request attempt about 33.65 seconds after startup.
  The later app-flash capture also shows no panic or watchdog failure, with
  `No coredump found`. Its reset is labeled `USB_UART_CHIP_RESET` following
  the flash/monitor command; it ends mid-line after 7366 ms, so later runtime
  behavior is not established by this excerpt.
- [x] The log contains `LittleFS mounted at /littlefs; package storage ready`.
  Observed at 3173 ms, with `1 packages`.
  Reconfirmed after app-only flash at 3172 ms, again with one package.
- [x] The log lists the built-in apps and contains
  `USB developer transport ready (CRYSTAL/1 INSTALL <bytes>)`.
  Built-ins `dev_tester`, `clock`, `weather`, and `calculator` are registered;
  the installed dashboard is discovered at slot 16. USB transport is ready
  at 3584 ms. Package discovery does not establish successful dashboard launch.
  The app-flash boot reconfirms the same apps and USB readiness at 3583 ms.
- [x] The log contains `Startup baseline: automatic Phase 14 device test
  disabled` and a `first-frame baseline` line.
  First-frame baseline: `2551 ms`; main-task stack headroom: `4592 bytes`.
  The log also confirms `UI and JSON allocators use PSRAM; task stacks remain internal`.
  After app-only flash: first frame `2550 ms`, main-task stack headroom
  `4544 bytes`, and the same PSRAM allocator message.
- [x] Record any boot warning. In particular, a missing initial package
  signing key blocks signed Wi-Fi tests until the key is configured.
  Observed: no initial package signing key configured; RTC unavailable/unset;
  ST7701 command overrides (36h and 3Ah); SPI panel CS retained inactive;
  I2S RX mode adjustment; and default launcher icon used for the package.
  Wi-Fi retries and a scan error are recorded below. None of these observations
  closes the pending provisioning or signed-install tests.
  After app-only flash the signing-key warning remains. The system initializes
  time from PCF85063 on this boot, so the earlier RTC warning does not recur
  in this capture.
- [x] With USB developer transport enabled, the Wi-Fi startup log reports
  `static_rx=10 static_tx=8` and `WiFi initialized`. Record the internal heap
  and largest DMA block from the startup lines. There must be no
  `malloc buffer fail` or partial RX-buffer allocation.
  Before initialization: internal `92735`, largest internal `31744`, DMA
  `84947`, largest DMA `31744` bytes. After initialization: internal `41219`,
  largest internal `31744` bytes. RX/TX counts match; no buffer-allocation
  failure appears in the supplied log.
  After app-only flash: before Wi-Fi, internal `92719`, largest internal
  `31744`, DMA `84931`, largest DMA `31744` bytes; after Wi-Fi, internal
  `41207`, largest internal `31744` bytes. Buffer counts still match and no
  buffer-allocation failure is shown.
- [ ] With saved network credentials, verify an IP address is obtained after
  a cold boot. Scan for networks, disconnect/reconnect, and enable the local
  panel while Weather performs HTTPS requests. Repeat with a declarative app
  installed; verify USB package transfer still completes with Wi-Fi active.
  Partial evidence: address `192.168.1.158` was obtained at 33019 ms with the
  dashboard installed. Earlier disconnects report reasons `201` and `36`;
  `sta is connecting, cannot stop scan process` appears at 28364 ms. The log
  ends before HTTPS completes and does not prove the full scan/reconnect,
  panel-concurrency, or USB-transfer sequence.
  After app-only flash the same address is obtained at 5900 ms, with no
  disconnect/retry shown in this excerpt. Weather request 1 starts at 6666 ms;
  its pre-request heap is internal `37707`, largest internal `28672`, PSRAM
  `4478128`, largest PSRAM `4325376` bytes. The capture ends before TLS or
  HTTP completion and contains no HTTPS smoke-test result. This is a reset
  after flashing, not evidence of the separate cold-power-on/concurrency tests.
  The subsequent [Weather/HTTPS continuation](test-evidence/phase15-16/2026-11-12-weather-https.txt)
  confirms both requests succeed as recorded below. Cold boot, manual
  scan/reconnect, panel concurrency, and USB transfer remain to be verified.
  Later cold-boot evidence is recorded separately below: saved-credential
  connection succeeds in both attempts, but HTTPS has mixed results. The
  later manual scan and reconnect pass as recorded below; panel concurrency
  and USB transfer remain pending.
- [x] Scan for nearby networks from Settings → Network → WiFi Networks.
  User-reported evidence: the scan displays nearby networks correctly, with
  no new serial output. This confirms the visible scan result; manual
  disconnect/reconnect and concurrent panel/HTTPS/USB operation remain
  separate checks.
- [x] Switch WiFi off and on and verify reconnection with saved credentials.
  User-supplied [manual reconnect output](test-evidence/phase15-16/2026-11-12-wifi-manual-reconnect.txt)
  shows disconnection at 1396846–1396849 ms (`reason=8`, association leave),
  followed by address `192.168.1.158` at 1405392 ms. The connected signal and
  toast are emitted, SNTP restarts, and the system clock/RTC synchronize at
  1409764 ms. The interval includes the time WiFi was switched off; it is
  not a measurement of connection time after switching it on. No failure or
  reset is shown in this excerpt. Concurrent panel/HTTPS/USB checks remain open.
- [x] Observe saved-credential Wi-Fi recovery after cold boot.
  The user reports two power-cycle attempts in the saved
  [two-attempt cold-boot log](test-evidence/phase15-16/2026-11-12-cold-boot-two-attempts.txt).
  Both obtain `192.168.1.158`: attempt 1 at 5985 ms; attempt 2 at 19131 ms
  after one automatic reconnect (`reason=204`, Wi-Fi handshake timeout).
  Neither excerpt shows a panic, watchdog, or memory-allocation error.
  This confirms recovery in these attempts, not retry-free connection or
  completion of the combined scan/panel/USB checks above.
- [ ] Cold-boot HTTPS reliability: mixed result; follow-up required.
  In attempt 1, Weather fails with `ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT`
  after 15707 ms; the smoke request fails with
  `ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED`, mbedTLS `-0x7280` (connection EOF),
  after 15117 ms. Both have HTTP status `0`, and the boot statistics show
  two failed requests. No `ESP_ERR_NO_MEM` appears; the failure samples show
  free internal heap of `30375` and `27311` bytes respectively. The root
  cause is not established by this capture. SNTP is started but no sync
  completion is shown before the next power cycle.
  In attempt 2, SNTP synchronizes at 22559 ms. Weather returns HTTP `200`,
  `ESP_OK`, `468` bytes in `7534 ms`; the smoke request returns HTTP `200`,
  `ESP_OK`, `577` bytes in `5130 ms`, followed by `HTTPS smoke test passed`.
  Both HTTPS requests use one attempt; the earlier Wi-Fi reconnect is a
  separate operation. The first boot schedules Weather retry in 60 seconds,
  but this log does not capture that retry before the power cycle.
  Keep the failure in the record; a passing second boot does not close it.
  The later [two-minute cold-boot capture](test-evidence/phase15-16/2026-11-12-cold-boot-two-minute.txt)
  is another successful run: Wi-Fi connects at 6302 ms; Weather requests 1
  and 3 and smoke request 2 all return HTTP `200` and `ESP_OK`. No startup
  failure occurs in that run, so it does not exercise the scheduled
  failure-recovery retry or establish the cause of the earlier failures.
- [x] Observe one cold-boot run through at least two minutes.
  The supplied capture extends through 120251 ms without a panic, watchdog,
  allocation error, or further boot sequence. Weather request 1 receives
  `468` bytes in `2881 ms`; the smoke request receives `577` bytes in
  `6474 ms`, followed by `HTTPS smoke test passed`; Weather request 3
  receives `468` bytes in `2565 ms`. Final HTTP statistics are three
  successes, zero retries, zero failures, and zero cancellations. SNTP
  synchronizes at 10394 ms. The lowest reported internal-heap sample is
  `27667` bytes, with a largest-block sample of `14848` bytes; after request
  3 is released, internal heap is `36135` bytes, largest block `19456` bytes,
  and free PSRAM `4478144` bytes. Selected log lines are saved as evidence.
  Later transitions pause/destroy Dev Tester and Clock and create Clock and
  Weather. The app-manager warnings describe its one-running-app limit.
  Status Dashboard is discovered at boot but is not launched in this capture.
- [x] Verify the startup Weather HTTPS request completes successfully.
  Request 1 returns HTTP `200`, `ESP_OK`, `468` bytes in `7918 ms`; the JSON
  response is accepted and Weather data is parsed. SNTP synchronizes at
  10718 ms. The response buffer is allocated in PSRAM.
  This successful baseline is retained; the separate cold-boot reliability
  check above records the later failure and subsequent success.
- [x] Verify the startup HTTPS smoke request completes successfully.
  Request 2 returns HTTP `200`, `ESP_OK`, `577` bytes in `5484 ms`, followed
  by `HTTPS smoke test passed`. Statistics report two successes, zero retries,
  zero failures, and zero cancellations. No panic, watchdog, or memory error
  is shown in this continuation, which extends to 61492 ms.
  This is the earlier successful run; cold-boot success is not consistent
  across both later attempts, as recorded above.
- [x] Record baseline internal-heap measurements during successful HTTPS.
  All sizes are bytes; these are samples from this boot, not guarantees for
  other app/panel loads. Full PSRAM and allocation data are in the saved log.

  | Request / point | Free internal | Largest internal block |
  | --- | ---: | ---: |
  | Weather, before | 37707 | 28672 |
  | Weather, reported TLS peak | 28771 | 19456 |
  | Weather, response released | 36755 | 21504 |
  | HTTPS smoke, before | 36907 | 21504 |
  | HTTPS smoke, reported TLS peak | 28027 | 16384 |
  | HTTPS smoke, response released | 36815 | 21504 |

## 4. Phase 15 USB package installation

This validates the implemented physical developer transport. It does not
validate Browser Web Serial, which remains an open item.

- [ ] Pack the Phase 16 fixture as an unsigned developer package:

  ```sh
  ./tools/crystal pack examples/declarative_dashboard --unsigned \
    -o /tmp/crystal-phase15-usb.capp
  ./tools/crystal inspect /tmp/crystal-phase15-usb.capp
  ```

- [ ] Install the package over USB:

  Exit IDF Monitor with `Ctrl+]` and close other serial terminals using the
  transfer port before running this command. Keep them closed until it returns.

  ```sh
  ./tools/crystal install --usb --port /dev/cu.usbmodem1101 \
    /tmp/crystal-phase15-usb.capp
  ```

- [ ] The CLI returns JSON with `"installed": true` and
  `"restart_required": true`.
- User-reported evidence: pack/inspect succeeded and the second USB install
  returned `{"installed": true, "restart_required": true}`. The first attempt
  was interrupted while waiting for `READY`; the monitor reported a possible
  multiple-access error. Repeat transfers with exclusive access to the port.
- [ ] After the installer exits, reconnect the monitor with
  `idf.py -p /dev/cu.usbmodem1101 monitor` and verify there is no panic or crash
  recovery indication. Save the successful CLI response as transfer evidence.
- [ ] Reboot the board and confirm the package count increases and the
  `org.crystal.declarative-dashboard` package is discovered at boot.
- [ ] Install the package from its source folder as a second transport check:

  ```sh
  ./tools/crystal install --usb --port /dev/cu.usbmodem1101 \
    examples/declarative_dashboard
  ```

- [ ] Send one malformed or truncated USB frame and confirm the device returns
  an error without leaving a partial package or changing the registry.
- [ ] Confirm a power/reboot during or immediately after installation leaves
  either the previous package or the complete new package, never a partial
  `.capp`.

## 5. Phase 15 pairing and authenticated Wi-Fi API

The Local Control Panel row now has a right-aligned Settings switch. The
2026-11-12 host regression `python3 tools/test_developer_tools_page.py`
passes with real LVGL for row layout, initial state, pending-start lock,
pairing callback, disable, missing/disconnected Wi-Fi rollback, worker/server
failure and retry, and leaving/reopening Developer Tools. Network and task
creation use fixtures; the new switch still needs these physical checks:
The user accepted the toggle change on 2026-11-12 ("OK now"). Individual
startup, failure, and service-disable checks below remain open.

- [ ] Turn the switch on while connected to Wi-Fi. Confirm Starting is shown,
  the switch cannot be pressed again during startup, and successful startup
  leaves it on and displays the one-time pairing code.
- [ ] Dismiss the pairing dialog, leave and reopen Developer Tools, and confirm
  the switch remains on for the enabled service. Turn it off and confirm the
  browser panel can no longer reach the API.
- [ ] With Wi-Fi disconnected, try enabling the switch. Confirm it returns off
  and requests a Wi-Fi connection. Reconnect and confirm startup can be retried.

- [x] On the device open `Settings -> Developer Tools -> Local Control Panel`
  and enable the panel for the current boot.
  User-supplied [panel startup evidence](test-evidence/phase15-16/2026-11-12-panel-enable.txt)
  reports successful enablement at 1528176 ms. Startup values: sockets `16`,
  clients `4`, free internal heap `36595`, largest internal block `19456`,
  and free PSRAM `3994460` bytes. No startup error is shown.
- [x] Record the device IP address and the six-digit PIN shown on the device.
  The supplied photo `IMG_7914.JPG` shows the Pair Control Panel dialog with
  `http://192.168.1.158/` and a six-digit PIN. The PIN value is omitted from
  this persistent record. The dialog states a 10-minute, single-use window;
  actual exchange, reuse rejection, and expiry behavior remain untested.
- [x] Open `http://<device-ip>/` in a browser and confirm the static control
  panel loads.
  The user screenshot from 2026-11-12 at 5:40 PM shows the original panel
  loaded in Safari at 192.168.1.158. Its pairing-error message does not establish
  a successful token exchange. The redesigned page requires a fresh browser
  check after updating the storage asset partition.
- [x] Exchange the PIN in the panel and confirm the authenticated panel opens.
  On 2026-11-12 the user explicitly confirmed that the first pairing in the
  normal browser succeeds; the private-window retry using the same code is
  rejected. The bearer token value is not included in this evidence.
- [ ] Retain the returned bearer token locally for the authenticated API checks;
  do not commit or paste the token into the test record.
- [x] Confirm a second exchange with the same PIN is rejected.
  On 2026-11-12, following the same-code retry instructions, the user reported:
  “That code could not be accepted. Turn Local Control Panel off and on in
  Developer Tools to get a new code.” This confirms rejection of the second
  pairing attempt. Initial normal-browser pairing is confirmed above.
- [x] Confirm the rejected retry leaves the original browser session usable.
  On 2026-11-12, the user clicked the panel's Refresh button in the original
  paired browser and received “Device information updated.” This confirms a
  successful authenticated refresh after the private-window rejection.
- [ ] Confirm the PIN expires or is invalid after the documented enable window.
- [x] Without a token, confirm the API rejects the request:

  ```sh
  curl -i "http://<device-ip>/api/v1/system"
  ```

  Expected result: HTTP `401`.
  On 2026-11-12 the user ran the request against 192.168.1.158 and received
  HTTP/1.1 401 Unauthorized with Content-Type: application/json,
  Cache-Control: no-store, and body {"error":"unauthorized"}. This confirms
  unauthenticated access to the system endpoint is rejected.

- [x] With the bearer token, confirm both endpoints return HTTP `200`:
  On 2026-11-12 the user supplied the paired browser Network details for
  `http://192.168.1.158/api/v1/system` and
  `http://192.168.1.158/api/v1/apps`. Both report Status: 200 OK,
  Source: Network, and Address: 192.168.1.158:80. Both JSON response bodies
  were subsequently supplied and validated below.

  ```sh
  curl -i -H "Authorization: Bearer <TOKEN>" \
    "http://<device-ip>/api/v1/system"
  curl -i -H "Authorization: Bearer <TOKEN>" \
    "http://<device-ip>/api/v1/apps"
  ```

- [x] Inspect the successful system and apps response bodies and confirm they
  contain the expected JSON objects and app list.
  User-supplied [authenticated API evidence](test-evidence/phase15-16/2026-11-12-authenticated-api.json)
  on 2026-11-12 contains schema 1 for both endpoints, ABI 1, system version
  0.12.14, and five enabled apps: four built-ins and Status Dashboard 1.0.0.
  Built-in versions are null as expected. Slots are unique: Dashboard 0,
  dev_tester 2, clock 3, weather 4, calculator 16. The user confirmed that
  firmware 0.15.0 had not yet been flashed when these responses were captured,
  so 0.12.14 is expected for this evidence. Agreement with the displayed app list is confirmed below.
- [x] Confirm a made-up bearer token is rejected by the system and apps queries.
  On 2026-11-12 the user sent `Authorization: Bearer invalid-test-token` to
  `GET /api/v1/system` and `GET /api/v1/apps` at 192.168.1.158. Both returned
  HTTP/1.1 401 Unauthorized, Content-Type: application/json,
  Cache-Control: no-store, and body {"error":"unauthorized"}.
- [ ] Confirm an invalid, expired, or altered token returns HTTP `401` for
  every protected endpoint.
  The made-up-token checks above cover the two GET endpoints only; other
  protected endpoints and expired/altered-token cases remain unverified.
- [x] Confirm the panel reports the same app names, IDs, versions, enabled
  states, and slot-based order as the apps API response.
  On 2026-11-12 the user supplied the displayed list: Status Dashboard,
  dev_tester, clock, weather, calculator. All five show Enabled; Dashboard
  shows Installed app and version 1.0.0 with its full package ID. The other
  four show Built in. The displayed order matches ascending slots 0, 2, 3,
  4, 16 from the captured API response.
- [x] Confirm restart-required feedback after changing app settings.
  On 2026-11-12, following the dev_tester Disable then Enable instructions,
  the user reported the same message after each action: “App setting saved.
  Restart your Crystal to apply it.” This confirms the settings feedback;
  it does not establish behavior after reboot.
- [ ] Confirm restart-required feedback after installing a package against
  the installation response. The apps query does not include this field.

### Sign out and confirmed reboot

The 2026-11-12 page update places Sign out beside Refresh above all mobile
tabs, and Reboot in the former Disconnect location in the Device section.
Deploy both application and system assets (`app-flash storage-flash`); the
page-only update cannot provide the new firmware endpoint.

Host checks pass: `node tools/test_control_panel.mjs` verifies top placement,
session/data clearing, confirmation with Cancel focused, cancellation without
a request, one authenticated POST on confirmation, error recovery, and no
refresh during reboot. `python3 tools/test_control_reboot.py` exercises the
production token guard and reboot functions with HTTP/restart stubs under
ASan/UBSan, including invalid/expired tokens, install exclusion, duplicate
rejection, scheduling failure recovery, and response-before-restart ordering.
These host results do not establish a successful physical reset.

- [ ] On desktop and mobile, confirm Sign out is visible above the sections
  or tabs and returns to pairing with the app list and package selection cleared.
- [ ] Pair again, open Device on mobile, and select Reboot. Confirm the dialog
  explains the restart and re-pairing requirement; Cancel must leave the device running.
- [ ] Select Reboot now and confirm exactly one reset in the serial monitor,
  preceded by `Rebooting at the paired control panel's request`. The browser
  should request re-pairing rather than reporting a failed refresh.
- [ ] After boot, enable Local Control Panel again and pair with the new code.
- [ ] Without a token or with an invalid token, POST `/api/v1/system/reboot`
  must return one HTTP 401 response and must not reset the device. Recheck
  protected app mutations after the shared token guard correction; a 401
  must stop execution even when sending the error response succeeds.

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

- [x] From the external panel launch an installed app.
  On 2026-11-12, with Settings open, the user reported that Launch neither
  dismissed Settings nor left Status Dashboard selected behind it. The user
  clarified the requirement: launching the requested app behind Settings is
  sufficient; automatically closing Settings is optional.
  The shell correction routes remote Launch through the normal card-start
  path, verifies the active app, and updates the Settings return selection
  only after a successful start. Settings remains visible.
  Host regression and platform integration checks pass, and firmware 0.15.0
  builds. After receiving the flash/retest instructions, the user confirmed
  “Yes, the change is on.” Record the baseline remote-launch correction as
  user-confirmed on the device. Repeated-launch and Settings-return serial
  evidence is now recorded below; requests issued while Settings is open
  remain a separate check.
- [ ] After flashing the correction, launch Status Dashboard from the panel
  while root or nested Settings is open. Confirm serial output includes
  `Control-panel launched org.crystal.declarative-dashboard behind Settings`.
  Confirm the selected app survives closing Settings. The later serial
  excerpt begins with Settings closing, so it does not itself capture a
  Launch request issued while Settings is open.
- [x] Close nested Settings and confirm it returns to Status Dashboard.
  The [device serial evidence](test-evidence/phase15-16/2026-11-12-repeated-panel-launch.txt)
  shows two Settings pages closing at 26280 ms, Dashboard onResume, and
  Dashboard selected as active card 1/5 at 26295 ms.
- [x] Repeat Launch while Dashboard is already active; confirm no crash,
  reboot, or duplicate lifecycle creation.
  The user pressed Launch four times and reported normal operation without
  crashing or restarting. The first request switches from Dev Tester and
  creates Dashboard; the next three select the same app without additional
  onCreate/onResume/onDestroy calls in the supplied excerpt.
- [ ] Follow up Dashboard startup timing: onCreate took 110 ms against an
  80 ms budget on the first remote launch. Track this performance warning
  separately from the successful repeated-launch functional check.
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

**Device report:** USB installation returned `installed: true`. The initial
Status Dashboard launch crashed; after the memory correction below, the user
confirmed it loaded and supplied a screenshot. That screenshot exposed a
separate layout failure: the app occupied a small upper-left rectangle, tabs
overlapped, and the rest of the app area stayed white. Keep the complete device
runtime checks open until the corrected layout and lifecycle pass on the board.

The supplied backtrace reaches picolibc's file-lock allocation from
`crystal_package_read_file` while loading the dashboard image. The same boot
connected to Wi-Fi but HTTPS failed with `ESP_ERR_NO_MEM`. The correction routes
LVGL widget/style allocations and cJSON nodes to PSRAM and uses VFS descriptor
reads for package assets, avoiding a new stdio lock per asset. Task stacks stay
internal: the USB installer and UI write flash, and IDF v6.1 can still disable
the cache during writes with flash mappings present, even with instruction and
rodata relocation enabled. Moving these stacks requires a separate flash-worker
design or proving all relevant cache-disabled paths safe.

The host renderer reproduced the screenshot: clearing the root's styles after
setting its size reset it to LVGL's default 130×130. The layout correction clears
styles first, preserves the full app area, applies layout options to tab pages,
and gives Timer buttons two rows and Controls a bounded vertical scroll view.
Host integration checks now verify root size, tab and button bounds, lack of
button overlap, and access to the lower controls. Both the firmware and the
repacked dashboard must be installed to validate this correction on the board.

- [x] After app-only flashing, confirm the boot message
  `UI and JSON allocators use PSRAM; task stacks remain internal`.
  Observed at 1326 ms in the saved application-flash/boot transcript.
- [ ] Save internal heap measurements before/after Wi-Fi and during HTTPS.
  Verify Weather and the HTTPS smoke request succeed, then open the dashboard
  with Wi-Fi active and repeat while the Local Control Panel is enabled.
  Baseline Weather/HTTPS success and heap samples are now recorded in Section 3
  and the saved Weather/HTTPS continuation. Dashboard launch with Wi-Fi and
  repetition with the panel enabled are still pending, so this combined check
  remains open.

- [ ] With the declarative package installed over USB or signed Wi-Fi, reboot
  the board and launch it from the on-device launcher.
- [ ] Confirm the app creates its tabs and widgets without a watchdog reset,
  allocation failure, or LVGL assertion.
- [ ] Confirm the dark dashboard fills the app area below the system status
  bar, all three tab titles fit, Timer buttons are separate and readable, and
  Controls scrolls to the chart and text input without a white background.
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
