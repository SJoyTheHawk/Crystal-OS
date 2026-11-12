# App Platform v2 Phase 15 and 16 Test Guide

Use this guide to record the remaining Phase 15 and Phase 16 exit evidence on a
real ESP32-S3 board. Check a box only after the step has been observed and save
the command output, serial log, screenshot, or HTTP response as evidence.

The host tests prove implementation behavior, but they do not close the
hardware-only items. USB developer install accepts an unsigned package. Wi-Fi
install is authenticated and accepts signed packages only after the matching
trusted public key has been provisioned on the device.

For the reconciled outstanding work, use the [Phase 15/16 closure checklist](APP_PLATFORM_V2_PHASE_15_16_CLOSURE_CHECKLIST.md).
It distinguishes missing implementations, remaining device observations and
documentation conflicts, and avoids rerunning already-passed checks below.

## Test record

- [ ] Tester: ______________________________
- [ ] Date/time: ___________________________
- [x] Latest observed firmware: `crystal_os` version `0.15.0`, compiled
  `Nov 13 2026 03:04:55`, ESP-IDF `v6.1`, ELF SHA256 prefix `07e71ed2e...`,
  reported in [batch N–R](test-evidence/phase15-16/2026-11-13-batch-nr-results.txt).
  G–M used ELF prefix `30c606fd4...` with the same compile-time string.
  The D–F test used ELF prefix `7afe2888f...`, compiled `Nov 12 2026 18:38:15`,
  after the built-in ID correction.
  The earlier same-version recovery capture used ELF prefix `eb414a728...`.
  Earlier evidence also identifies
  version `0.12.14`, compiled `Nov 12 2026 04:53:57`, ELF prefix
  `27bdd384f...`. Git commit is not identified by either supplied log.
- [x] Board serial port: `/dev/cu.usbmodem1101`
- [x] Latest observed board Wi-Fi address: `http://192.168.1.193/` in the
  2026-11-13 recovery log; earlier captures used `http://192.168.1.158/`.
- [x] Trusted test package key ID: `240`; public key hex
  `1a20458475af8971b821a08ac8c6f2a42ee0718917bc0ebee8848d0f48c2dd9b`.
  Provisioning is confirmed by the boot log and successful signed Wi-Fi
  installs in G/I/J/M. This is the local test identity, not a release key.
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
- [x] Provision the release/test Ed25519 public key in the device trust store
  before attempting a Wi-Fi package install. Record the key ID above.
  Batch G supplies `Loaded configured package signing key ID 240` at 3142 ms,
  and the final M boot repeats it at 3145 ms. I/J/M accept packages signed
  by the corresponding test key. Earlier builds selected ID 1 with an empty
  initial-public-key configuration and warned of no configured key; the new
  evidence supersedes that pending test-provisioning result without asserting
  that all other NVS key slots are empty.
- [x] Capture serial logs for boot, Wi-Fi install, and launch tests. Close any
  monitor using the transfer port before flashing or USB installation. The USB
  installer must be the only application reading `/dev/cu.usbmodem1101`; a monitor
  on that port can consume protocol replies. A monitor on a separate UART port
  may remain open. Reopen the USB monitor after the installer exits.
  Batch G–M adds app-flash/boot, successful signed install, live registry
  application, launch/destroy and cleanup evidence. USB transfer exclusivity
  remains a transport-specific requirement covered separately by Section 4.

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

- [x] Install the package over USB:

  Exit IDF Monitor with `Ctrl+]` and close other serial terminals using the
  transfer port before running this command. Keep them closed until it returns.

  ```sh
  ./tools/crystal install --usb --port /dev/cu.usbmodem1101 \
    /tmp/crystal-phase15-usb.capp
  ```

- [x] The CLI returns JSON with `"installed": true` and
  `"restart_required": true`.
  On 2026-11-12 the [reinstall transcript](test-evidence/phase15-16/2026-11-12-panel-reinstall.txt)
  records a fresh successful USB transfer of `/tmp/crystal-uninstall-retest.capp`
  on `/dev/cu.usbmodem1101` with both fields true.
- User-reported evidence: pack/inspect succeeded and the second USB install
  returned `{"installed": true, "restart_required": true}`. The first attempt
  was interrupted while waiting for `READY`; the monitor reported a possible
  multiple-access error. Repeat transfers with exclusive access to the port.
- [x] After the installer exits, reconnect the monitor with
  `idf.py -p /dev/cu.usbmodem1101 monitor` and verify there is no panic or crash
  recovery indication. Save the successful CLI response as transfer evidence.
- [x] Reboot the board and confirm the package count increases and the
  `org.crystal.declarative-dashboard` package is discovered at boot.
  The reinstall transcript records USB_UART_CHIP_RESET, firmware 0.15.0,
  no coredump, LittleFS ready with one package, all four built-ins and
  Dashboard installed, then successful Dashboard creation and HTTPS smoke.
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
creation use fixtures. The user accepted the toggle change on 2026-11-12
("OK now"). The 2026-11-13 D–F device results now validate successful
enable, page-reopen state, disable, and missing-Wi-Fi recovery below.

- [x] Turn the switch on while connected to Wi-Fi. Successful startup leaves
  it on and displays the pairing code (user-confirmed in E, 2026-11-13).
- [x] Verify the Starting state and duplicate-start guard with the
  deterministic real-LVGL host test. On 2026-11-13 the user explains the
  pairing dialog covers the toggle and startup is complete before it can
  be dismissed. Production code opens that dialog after completion.
  `python3 tools/test_developer_tools_page.py` is rerun successfully: it
  holds startup pending, checks Starting/disabled state, injects a repeated
  event and requires one startup request. The transient state is not
  claimed as physically observed; E closes with this host coverage plus
  the successful device checks below.
  [A–F closure and evidence qualification](test-evidence/phase15-16/2026-11-13-batch-af-closure.txt)
- [x] Dismiss the pairing dialog, leave and reopen Developer Tools, and confirm
  the switch remains on for the enabled service. Turn it off and confirm the
  browser panel can no longer reach the API.
  User confirms retained switch state; curl after disabling fails to connect.
- [x] With Wi-Fi disconnected, try enabling the switch. Confirm it returns off
  and requests a Wi-Fi connection. Reconnect and confirm startup can be retried.
  The user reports the Wi-Fi-required warning, reconnect at 523208 ms and
  panel enable at 526087 ms. A subsequent unauthenticated system query
  returns expected HTTP 401; F then confirms pairing/Refresh recovery.
  [D–F evidence](test-evidence/phase15-16/2026-11-13-batch-def-persistence-recovery.txt)

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
  exchange, reuse rejection, and expiry are separately validated below.
- [x] Open `http://<device-ip>/` in a browser and confirm the static control
  panel loads.
  The user screenshot from 2026-11-12 at 5:40 PM shows the original panel
  loaded in Safari at 192.168.1.158. Its pairing-error message does not establish
  a successful token exchange. The redesigned page requires a fresh browser
  check after updating the storage asset partition.
- [x] After deploying the 2026-11-13 icon update, confirm the supplied Crystal
  artwork appears as the browser icon on laptop and mobile. Verify requests
  for `/favicon.png`, `/favicon.ico`, `/apple-icon.png`,
  `/apple-touch-icon.png`, and `/apple-touch-icon-precomposed.png` return
  HTTP 200 with image content while the panel is enabled.
  The PNG favicon is 48x48; the mobile touch icon is 180x180. Icon files use
  4895 bytes for the PNG favicon and 15453 bytes for
  the indexed mobile icon. Images remain in SPIFFS flash and are streamed
  through a 1024-byte buffer; they are not decoded into ESP32 RAM. Legacy
  icon URLs are served explicitly instead of returning the previously
  observed 404s. Deploy both `app-flash` and `storage-flash`, then reload
  the panel; browser icon caches may require reopening the tab.
  [Implementation and host validation](test-evidence/phase15-16/2026-11-13-panel-icons.txt)
  pass, including the SPIFFS image build. The user's subsequent device
  results on 2026-11-13 show HTTP 200, matching MIME types and byte counts
  for all five URLs at 192.168.1.193. Browser icon display is confirmed on
  laptop and mobile. The evidence file retains these device results.
- [x] Exchange the PIN in the panel and confirm the authenticated panel opens.
  On 2026-11-12 the user explicitly confirmed that the first pairing in the
  normal browser succeeds; the private-window retry using the same code is
  rejected. The bearer token value is not included in this evidence.
- [x] Retain the returned bearer token locally for the authenticated API checks;
  do not commit or paste the token into the test record.
  I stores session.json locally, asserts the returned token, and uses it for
  successful authenticated launch/logs without including its value in evidence.
- [x] Confirm a second exchange with the same PIN is rejected.
  On 2026-11-12, following the same-code retry instructions, the user reported:
  “That code could not be accepted. Turn Local Control Panel off and on in
  Developer Tools to get a new code.” This confirms rejection of the second
  pairing attempt. Initial normal-browser pairing is confirmed above.
- [x] Confirm the rejected retry leaves the original browser session usable.
  On 2026-11-12, the user clicked the panel's Refresh button in the original
  paired browser and received “Device information updated.” This confirms a
  successful authenticated refresh after the private-window rejection.
- [x] Confirm an unused PIN expires after the documented enable window. In
  F on 2026-11-13 the user waits 11 minutes, reports rejection of the unused
  code, then successfully pairs/Refreshes with a fresh code after toggling
  the panel. This does not establish the separate bearer-token lifetime.
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
- [x] Confirm missing and made-up tokens return HTTP `401` across the
  protected system/apps/logs queries, package upload, order update, per-app
  launch/enable/clear/uninstall actions, and reboot. On 2026-11-13 all
  20 requests return unauthorized JSON/401, no reboot-request log appears,
  and the paired browser still Refreshes to four apps. See the
  [batch A–D evidence](test-evidence/phase15-16/2026-11-13-batch-abc-clock-disable.txt).
- [ ] Confirm expired or altered previously valid tokens return HTTP `401`
  for every protected endpoint. R establishes alteration and invalidation for
  the system query below; all-route lifecycle/expiry coverage remains separate.
- [x] With a real valid token, GET system returns 200; change one character
  and repeat for 401; after panel stop/restart the old token returns 401.
  R supplies all three responses without printing the token. This does not
  establish 24-hour expiry or invalidation of an already-open log stream.
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
- [x] Confirm restart-required feedback after installing a package against
  the installation response. The apps query does not include this field.
  I supplies installed/restart_required true in the CLI response; J supplies
  the corresponding browser install/restart message; M repeats both fields.

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

- [x] On laptop and mobile browsers, Sign out returns to pairing with the
  app list hidden; reloading remains unpaired. Toggle Local Control Panel
  off/on, pair with the new code, and Refresh restores the four-app list.
  On 2026-11-13 the user confirmed these instructed behaviors on both
  browsers. The [retained evidence](test-evidence/phase15-16/2026-11-13-panel-sign-out.txt)
  includes panel-start logs; the individual browser outcomes are user-reported.
- [x] Confirm Sign out appears at the top, just below the header. The user
  explicitly confirms this placement on 2026-11-13; see the
  [grouped test results](test-evidence/phase15-16/2026-11-13-panel-session-checks.txt).
- [x] With an upload file selected, confirm Sign out clears the package
  selection. On 2026-11-13 the user confirms Step 4 works as instructed,
  including signing out, re-pairing, an empty upload selection, disabled
  Install, and four apps after Refresh.
- [x] Cancel the reboot dialog and Refresh. On 2026-11-13 the user confirms
  Cancel is on the left, Reboot on the right, and the session still works
  after cancellation, without a reset or reboot-request log. See the
  [reboot/recovery evidence](test-evidence/phase15-16/2026-11-13-panel-reboot-recovery.txt).
- [x] Select Reboot now and capture one requested restart per deliberate
  click with the existing monitor running. The 2026-11-13 D result captures
  the request at 131074 ms followed by `RTC_SW_CPU_RST`, and the second
  deliberate request at 194553 ms followed by USB reconnect and a successful
  restoration boot. Neither sequence shows an abort or watchdog.
  The continuous D capture resolves the earlier monitor-start reset
  ambiguity; see the D–F evidence.
- [x] Confirm the browser returns to pairing with reboot/re-pairing guidance.
  The user supplies the expected text on 2026-11-13: `Reboot requested.
  After your Crystal restarts, enable Local Control Panel and pair with a
  new code.` The pairing screen includes the Settings path and code
  expiry/single-use explanation. This closes D's final browser observation;
  button placement/cancellation and post-boot recovery already pass.
  See the A–F closure evidence.
- [x] After the captured boot, firmware 0.15.0 starts, LittleFS mounts with
  zero packages, four built-ins register, Wi-Fi reconnects, weather and
  HTTPS smoke requests succeed. The user confirms four working launcher
  apps without gaps and Local Control Panel initially off. The recovery
  log then shows the panel enabled at 10488 ms.
- [x] After boot, pair again with the newly enabled panel, Refresh, and
  confirm the authenticated system/apps responses and device/web order.
  D supplies both HTTP 200 browser Network responses after the disabled-Clock
  reboot; the restoration boot and user report confirm four enabled apps
  in original order across launcher, Manage Apps and web panel.
  Weather initially times out in this D boot, then succeeds on its retry at
  74621 ms; HTTPS smoke passes at 20525 ms. Retain that transient failure
  and recovery, not an all-requests-succeeded claim.
- [x] Without a token and with `Bearer invalid-test-token`, POST
  `/api/v1/system/reboot` returns HTTP 401. The user supplied two separate
  unauthorized responses on 2026-11-13, each with the expected JSON body,
  Content-Length 24, and Cache-Control no-store; see the grouped test results.
- [x] The grouped missing/made-up-token matrix produces no reboot-request
  message and leaves the paired browser usable with four apps after Refresh
  (user-confirmed 2026-11-13). Both unauthorized reboot POSTs return 401.
  The per-app mutation probes target an absent app and prove rejection at
  the HTTP boundary, not each possible mutation/body combination.

### Grouped device test session (2026-11-13)

The following is a planned sequence, not a record of passed device checks.
It groups the remaining icon, sign-out and reboot observations into one
session; signed Wi-Fi installation remains a later test.

1. If the latest icon changes are not deployed, close the USB monitor and run
   `idf.py -p /dev/cu.usbmodem1101 app-flash storage-flash monitor`. Use the
   current port if it has changed. This updates firmware and system assets;
   do not use full `flash` for this session, because it includes the factory
   apps image. Keep the monitor open for these HTTP/browser tests.
2. Enable Local Control Panel. Use its current IP to check all five icon
   URLs above: expect HTTP 200, correct image MIME types, 4895-byte PNG
   favicon, 4917-byte ICO, and 15453-byte mobile image (also for its aliases).
   Check actual browser icon display on laptop/mobile separately from the
   HTTP responses; browser caching can retain an old icon.
3. With the panel enabled, POST `/api/v1/system/reboot` once without an
   Authorization header and once with `Bearer invalid-test-token`. Each
   must return one HTTP 401 and must not reset the device. This covers this
   endpoint and these token cases only, not every protected mutation.
4. Pair the laptop browser. Select a `.capp` file without pressing Install,
   then Sign out. Confirm the list disappears and reload stays unpaired.
   Toggle the panel off/on, pair again, and confirm the upload selection is
   empty, Install disabled, and Refresh returns the four built-ins. Check
   Sign out stays above the sections/tabs on laptop and mobile. Pair one
   browser at a time with a fresh device code.
5. In the paired mobile browser, choose Device -> Reboot. Check the restart
   and re-pairing explanation and Cancel/Reboot now arrangement. Cancel,
   wait several seconds, and Refresh. Expect no reset, no reboot-request
   serial message, and a usable paired session.
6. Open the dialog again and confirm Reboot now once. Expect one
   `Rebooting at the paired control panel's request` message and one device
   restart. The browser returns to pairing with reboot guidance, rather
   than a failed-refresh error. Keep the existing monitor open and capture
   the request, boot, and recovery logs.
7. After boot, confirm firmware 0.15.0, Wi-Fi reconnect, four built-in apps
   without launcher gaps, and Local Control Panel initially off. Enable it,
   pair with a fresh code, and Refresh: expect firmware 0.15.0, four apps,
   and matching device/web order. Record failures and console logs; do not
   include pairing codes or bearer tokens in evidence.

For the next larger session, use the
[continuation batch for 2026-11-13](APP_PLATFORM_V2_DEVICE_TEST_BATCH_2026-11-13.md):
protected endpoint rejection, unsigned Wi-Fi rejection, persisted app
order/enable state with controlled reboots and restoration, Developer Tools
switch recovery, and unused-PIN expiry. These instructions are not pass
evidence; record observed results against the corresponding checks.
The A–F continuation batch is now closed as of 2026-11-13, with E's brief
startup state covered by the deterministic host test rather than a claimed
device observation. See the closure evidence above. Signed install and
other phase-exit gates below remain separate.

## 6. Phase 15 Wi-Fi installation, progress, run, and logs

[Device test batch G–M](APP_PLATFORM_V2_DEVICE_TEST_BATCH_G_M_2026-11-13.md) was
reported on 2026-11-13. [Recorded results and qualifications](test-evidence/phase15-16/2026-11-13-batch-gm-results.txt)
establish test-key provisioning, signed CLI/browser installs, negative
signature rejection, upload progress, live lifecycle logs, full run and
cleanup. Concurrent API service and active-stream shutdown are not established.
P in [N–R](test-evidence/phase15-16/2026-11-13-batch-nr-results.txt) additionally
confirms the existing browser Live logs controls on desktop/mobile; it does
not close the separate concurrent-service/shutdown check.

Use a signed package made with the private key whose public key is provisioned
on the device. Do not treat an unsigned package refusal as a failure; it is the
required production policy for Wi-Fi.

- [x] Create a signed package. The G fixture helper reports host verification
  with test key ID 240; subsequent device acceptance is established by I/J/M:

  ```sh
  ./tools/crystal pack examples/declarative_dashboard \
    --key /secure/package-private.pem --key-id <KEY_ID> \
    -o /tmp/crystal-phase15-wifi.capp
  ./tools/crystal verify /tmp/crystal-phase15-wifi.capp \
    --trusted-key <KEY_ID>=/secure/package-public.pem
  ```

- [x] Install the signed package from the CLI using the PIN. I supplies exit
  code 0, `installed: true` and `restart_required: true` for the signed Dashboard:

  ```sh
  ./tools/crystal install /tmp/crystal-phase15-wifi.capp \
    --wifi http://<device-ip>/ --pin <PIN>
  ```

- [x] CLI output reports a successful install and returns a bearer token.
  The user's command asserts the nonempty token and successfully uses it for
  authenticated launch (HTTP 202) and live logs. The token is not recorded.
- [x] Attempt Wi-Fi installation with the unsigned test package and confirm
  rejection without registry/launcher changes. On 2026-11-13 the user
  supplies the message `Unsigned package requires explicit Developer Mode`,
  a screenshot of HTTP 400 for POST /api/v1/apps (17275-byte upload), and
  confirms four apps in both management lists and no launcher gaps.
  See the batch A–D evidence; signed negative cases are separately confirmed below.
- [x] Reject untrusted-key and tampered signed packages without registry or
  launcher changes. H reports `Package signature not trusted` for each and
  confirms four apps, no Dashboard/gaps, and continuous operation. The HTTP
  status for these two requests was not supplied.
- [x] In the browser panel upload a package larger than 1 MB and confirm a
  visible progress indicator advances before the final success response.
  J reports the 1328026-byte fixture advancing roughly 10% per second from
  0% to 100%, then checking/installing and successful-install guidance.
- [x] Confirm the browser reports the same installed package and restart
  requirement as the CLI.
  J supplies `App installed. Restart your Crystal to load it.` and confirms
  post-reboot Dashboard launch plus web/Manage Apps discovery.
- [x] Start a live log stream in a second terminal:

  ```sh
  ./tools/crystal logs -f --wifi http://<device-ip>/ --token <TOKEN>
  ```

- [x] While the stream is open, launch or enable/disable an app and observe
  bounded lifecycle/debug events in the stream.
  I supplies repeated Dashboard/built-in onCreate/onPause/onDestroy lines
  across device navigation. This does not prove concurrent HTTP actions
  while a synchronous SSE handler is running.
- [x] Run the complete Wi-Fi command, using the signed key:

  ```sh
  ./tools/crystal run examples/declarative_dashboard \
    --wifi http://<device-ip>/ --pin <PIN> \
    --key /secure/package-private.pem --key-id <KEY_ID>
  ```

- [x] Confirm `crystal run` packs, installs, launches, and produces lifecycle
  output without a reboot or crash.
  M supplies install success, a queued launch, live lifecycle output, and
  actual device launch. Serial confirms live registry application at
  110854–110872 ms and Dashboard start behind Settings at 111018 ms.
- [x] In browser Live logs, observe new lifecycle rows, Pause/Start behavior,
  and sign-out clearing/hiding log output and device controls; confirm mobile
  behavior as well. P reports these outcomes. Source bounds the displayed
  output to 32768 characters; no new device maximum-output stress test is
  claimed. Retained device history is available to newly paired clients:
  the volatile queue keeps at most 64 lines of 240 bytes and resets on reboot,
  not on panel/Wi-Fi toggles. Browser sign-out clears the local view.
- [ ] Confirm `crystal logs -f` stops cleanly when the device disables the
  panel or the network connection is closed.
  Not tested by this batch. The current synchronous stream lasts around
  60 seconds and the CLI does not automatically reconnect; normal return
  alone does not prove cancellation, concurrency or uninterrupted logging.

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
- [x] After flashing the correction, launch Status Dashboard from the panel
  while root or nested Settings is open. Confirm serial output includes
  `Control-panel launched org.crystal.declarative-dashboard behind Settings`.
  Confirm the selected app survives closing Settings. The 2026-11-12
  [clear-data test excerpt](test-evidence/phase15-16/2026-11-12-panel-clear-data.txt)
  captures Launch behind Settings at 297786 ms, both pages closing at
  300824 ms, and Dashboard retained as the active card at 300831 ms.
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
  The later clear-data recovery test records 115 ms on both Dashboard
  creations at 373722 and 396349 ms. This performance item remains open.
  The reinstall/boot test records 100 ms at 16018 ms, also above budget.
- [x] Disable it, refresh the app list, then enable it again.
  On 2026-11-12 the user confirmed all four instructed steps: Disable, Refresh,
  verify Disabled with Launch unavailable; then Enable, Refresh, verify Enabled
  with Launch available again. No visible new serial output was reported.
  [User-reported evidence](test-evidence/phase15-16/2026-11-12-panel-enable-disable.txt)
  records the successful roundtrip. This confirms refreshed panel state and
  Launch availability for Dashboard; package-specific persistence is separate
  from the later built-in Clock persistence result.
  On 2026-11-13 the grouped persistence test exposes `invalid app id` when
  disabling built-in Clock. Package-only ID validation wrongly rejected
  built-in catalog IDs. The HTTP parser now also accepts exact registered
  built-in IDs while leaving package grammar strict. The production-handler
  ASan/UBSan regression, reboot/icon regressions and `idf.py build` pass.
  The subsequent D retest on firmware ELF `7afe2888f...` confirms the fix
  on the device; see the D–F evidence.
- [x] Built-in enabled state and ordering persist through reboot and restore
  correctly. D records Clock disabled in both UIs, then a three-app launcher
  ordered Calculator, Dev Tester, Weather after reboot. Re-enabling Clock
  and restoring slots 0–3 survives the second reboot: Dev Tester, Clock,
  Weather, Calculator, all enabled and gap-free across device/web views.
- [x] Clear its data and verify the action reports success.
  Device retest on 2026-11-12: Cancel does nothing and produces no completion
  log (user-confirmed). Both confirmed clear attempts returned `: 0`, so
  clearing failed on the earlier firmware. The supplied
  [serial evidence](test-evidence/phase15-16/2026-11-12-panel-clear-data.txt)
  also confirms Launch behind nested Settings and retention after closing it.
  A later clear returned `: 1` at 410825 ms (one successful attempt); these
  numbers are boolean results, not slots. The subsequent device retest
  confirms the correction: explicit succeeded logs at 37080, 45547, and
  59732 ms, plus another at 36081 ms after a user-reported reboot.
  The excerpt does not identify whether each request targeted a foreground,
  paused, or already stopped app, so those individual paths are not assumed.
  The preceding `getRunningAppById` errors are expected-absence lookup noise,
  not failed clearing. The helper now checks the running list quietly while
  retaining STOP verification. Its ASan/UBSan regression and firmware build
  pass; this log cleanup has not yet been flashed or device-tested.
  The production shell lifecycle regression and full platform ASan/UBSan
  integration pass, and `idf.py build` succeeds. Device retest: launch
  Dashboard, open Developer Tools to pause it, confirm Clear data twice via
  the web panel, and require the explicit succeeded log for both requests.
  Then Launch, close Settings, and confirm normal operation. Repeat once
  with Dashboard foregrounded and confirm it can launch again.
  Use Status Dashboard only. First cancel the confirmation and confirm no
  clear-data operation runs. Then repeat and confirm Clear data. The panel's
  queued response only acknowledges scheduling; require serial completion
  `Control-panel clear data succeeded for org.crystal.declarative-dashboard`.
  Older firmware reports the same success as `cleared data for <id>: 1`.
  Refresh to confirm the app remains installed and enabled, then Launch and
  confirm normal operation without a crash or reboot. Dashboard may have no
  visible saved-state change; timer/network service state is not app data.
- [x] After the successful clear-data retest, Refresh to confirm Dashboard is
  still installed and enabled, Launch with Settings open, then close Settings
  and confirm its UI works normally. Repeat clear/relaunch once with
  Dashboard foregrounded. On 2026-11-12 the user confirmed working normally
  and supplied the [recovery serial sequence](test-evidence/phase15-16/2026-11-12-panel-clear-data.txt):
  Dashboard launches behind Settings at 373729 ms, remains selected after
  both pages close at 380411 ms, then pauses/destroys, clears successfully
  at 393137 ms, and is created and selected again at 396353 ms.
- [x] Move the app up and down in the external ordering controls.
  On 2026-11-12 the user confirmed moving Status Dashboard down and back up,
  with Refresh retaining each saved order. The supplied
  [serial evidence](test-evidence/phase15-16/2026-11-12-panel-ordering.txt)
  shows registry writes for both operations. The user also requested fixing
  sparse slot values; validation of that follow-up is separate below.
- [x] After the consecutive-slot correction, move an app and Refresh. Confirm
  `/api/v1/apps` contains unique slots 0 through app-count minus one; move it
  back and Refresh again. Reboot and on-device ordering checks remain separate.
  On 2026-11-12 the user confirmed "Correct now" after the correction and
  retest instructions. Record this as user-confirmed slot normalization; no
  new API response or serial excerpt was supplied. Reboot persistence and
  on-device Manage Apps agreement are not established by that confirmation.
  Host validation on 2026-11-12 passes browser reorder/Refresh in both
  directions, real-LVGL drag ordering with sparse defaults, persistence,
  no-op/cancel and rollback checks, the registry regression, the production
  platform ASan/UBSan integration suite, and `idf.py build`.
  Both reorder interfaces now save consecutive ranks. Package slot writes also
  use the registry success/failure log, so installed packages appear in serial
  ordering diagnostics. Flash both the application and storage image, reload
  the panel, and make an actual move to normalize existing sparse slots;
  merely flashing or refreshing does not rewrite saved order.
- [x] Uninstall the test app and confirm it disappears from both the panel and
  `/api/v1/apps`.
  On 2026-11-12 the user supplied successful lifecycle teardown/uninstall
  logs and an apps API response containing only the four enabled built-ins;
  built-in apps remain usable. The initial test found a launcher hole and
  incorrect count until reboot. After the correction and retest instructions,
  the user confirmed "Yes, working after uninstallation." The follow-up
  log shows successful removal at 42512 ms, return from nested Settings to
  Dev Tester at 54406 ms, and the corrected `active card 1/4` at 54412 ms.
  Record web removal and live launcher recovery as passed.
  [Uninstall evidence](test-evidence/phase15-16/2026-11-12-panel-uninstall.txt)
  records the discrepancy. The correction compacts existing launcher icons
  across pages and notifies the shell after an installed row is removed,
  updating card count, selection and Settings return indices for both web
  and on-device uninstall. Both removal interfaces are now device-confirmed;
  the later on-device result is recorded separately below.
  Host validation passes the production platform ASan/UBSan multipage
  launcher/replacement/icon-input tests, removal notification cycles, shell
  index/count/Settings return regressions, registry tests and firmware build.
  First switch the device to Dev Tester. In the web panel, cancel the
  Dashboard Uninstall dialog once and Refresh to confirm it remains. Then
  confirm Uninstall, require `Control-panel uninstalled org.crystal.declarative-dashboard`
  in serial, Refresh, and verify the package is absent from both the list and
  the authenticated apps API response. Confirm built-in apps remain usable.
  A queued response alone is not removal confirmation.
- [x] After flashing the live-uninstall correction, reinstall Dashboard and
  remove it without rebooting afterward. Confirm no empty icon position,
  no redundant launcher page, four shell card indicators, working built-in
  icons/card swipes, and valid Settings return behavior. User confirmation
  and the follow-up serial excerpt establish the web-panel removal result.
  Persisted slot ranks need not match grid cell numbers;
  grid compaction must not depend on rebooting or reordering another app.
- [x] After reinstalling Dashboard, cancel the on-device Manage Apps Uninstall
  confirmation, then confirm removal. Verify Manage Apps and the web panel
  both show four apps and the live launcher has four apps without gaps.
  On 2026-11-12 the [device evidence](test-evidence/phase15-16/2026-11-12-manage-apps-uninstall.txt)
  records five catalog apps, cancellation at 374002 ms, a later confirmed
  operation, then four catalog apps at 388147 ms. The user confirms both
  Manage Apps and the web panel show four apps, with no launcher holes.
  The old handler misleadingly labels Uninstall as clearing data; action
  dispatch is correct. The logging-only correction now names Uninstall
  separately and has not been flashed. This excerpt does not separately
  show Settings return or card-indicator counts.
  Host validation passes the real-LVGL Manage Apps regression (including
  cancellation without removal, failed removal/retry, and catalog refresh),
  `idf.py build`, and `git diff --check`.
- [x] After on-device Uninstall, return from nested Settings to the surviving
  app and confirm the shell count is four. The 2026-11-13
  [follow-up serial excerpt](test-evidence/phase15-16/2026-11-13-panel-sign-out.txt)
  shows both Settings pages closing, Dev Tester resuming at index 0, and
  `active card 1/4`, followed by a Home gesture returning to the launcher.
- [x] After on-device Uninstall, confirm four launcher/card positions and an
  authenticated apps API response without Dashboard. Q (the user's second P)
  reports correct launcher/card positions and HTTP 200 with four enabled
  built-ins in slots 0–3 after both packages are removed. Manage Apps logs
  show Action Guard removal (6 to 5), then Dashboard uninstall (5 to 4).
- [ ] Record the exact on-device `App uninstalled.` success-notice wording.
  Q proves successful uninstall and correct resulting state, but does not
  quote the notice; do not repeat completed cleanup just for that observation.
- [x] In the web panel, cancel the Dashboard Uninstall dialog, Refresh, and
  confirm Dashboard remains installed. Q confirms the instructed cancellation.
  A confirmed Action Guard uninstall requires manual Refresh before its row
  disappears. Current queued-action UI explicitly advises Refresh and performs
  only one immediate list read; automatic completion refresh is not established.
- [x] Reinstall it, reboot the board, and confirm boot reconciliation restores
  the same package and order.
  Use the USB package `/tmp/crystal-uninstall-retest.capp` with Developer Mode
  enabled and exclusive access to `/dev/cu.usbmodem1101`. Retain the CLI
  installed/restart-required response, then reopen the monitor and restart
  once. Require Dashboard's boot discovery, five working apps, successful
  Dashboard launch, and agreement between Manage Apps and the web panel.
  Compare order to the current saved ranks; reinstall may assign a new rank
  and does not promise to restore the position from before uninstalling.
  On 2026-11-12 the user confirmed all instructed checks work. The
  [retained transcript](test-evidence/phase15-16/2026-11-12-panel-reinstall.txt)
  shows installed/restart-required true, LittleFS with one package, four
  built-ins plus Dashboard at its newly assigned rank 16, and Dashboard
  onCreate. The user confirms five apps without gaps and matching list/order
  in Manage Apps and the web panel. The subsequent on-device drag saves
  Dashboard=0 and Dev Tester=1; another reboot of that new order is not shown.
- [ ] Open the on-device `Manage Apps` page and compare the package ID,
  version, enabled state, ordering, launch, clear-data, and uninstall results
  with the external panel after each edit.
  Basic installed-list and order agreement is user-confirmed in the reinstall
  test. On-device Uninstall and both lists showing four apps also pass in
  the subsequent device test. Keep the remaining cross-interface actions open.
- [ ] Record any mismatch as a Phase 15 exit-gate failure; do not close the
  phase based only on a host or API response.
- [ ] **Blocked/open:** Browser Web Serial transport is not implemented. Leave
  this box unchecked until a Web Serial implementation and board validation
  exist.
- [ ] **Scope reconciliation:** On-device catalog browsing is not implemented,
  but the resolved plan section 15.2 specifies computer-side catalog fetch.
  Align this older checklist with that decision and complete the hosted
  end-to-end catalog workflow; do not silently add a device-store requirement
  or call the entire catalog delivered solely because host fixtures pass.

## 8. Phase 16 declarative runtime host checks

- [x] Run `python3 tools/test_crystal_declarative.py` and retain its output.
  Already recorded in Section 2 and the retained declarative/registry transcript.
- [x] Inspect the fixture package and confirm it contains bounded `ui.json` and
  optional `bindings.json` data:

  ```sh
  ./tools/crystal pack examples/declarative_dashboard --unsigned \
    -o /tmp/declarative-dashboard.capp
  ./tools/crystal inspect /tmp/declarative-dashboard.capp
  ```

- [x] Confirm host validation covers containers, labels, buttons, basic
  controls, tab views, button matrices, image, list, chart, textarea,
  spinner, and dialog widgets.
- [x] Confirm host validation covers time, network, and timer bindings.
- [x] Confirm allowlisted timer actions are accepted and unknown actions are
  rejected.
- [x] Confirm the visual references for Clock, Weather, and Calculator remain
  expressible without adding ABI calls.
  These host checks are already established by Section 2's production
  integration evidence and the later successful real-LVGL regression.
  They do not implement the remaining callable facade or replace board tests.
- [ ] **Open:** dynamic list/chart operations and input focus are not complete;
  leave the corresponding Phase 16 exit checkbox unchecked.

## 9. Phase 16 device lifecycle and binding checks

**Device reports (2026-11-13):** batch G–M confirms signed Dashboard
installation, post-reboot launch, full-size dark layout, readable tabs and
two-row timer controls, live bindings, repeated lifecycle transitions and
cleanup. Photos establish the corrected Status/Timer/Controls layout.
[Evidence and remaining qualifications](test-evidence/phase15-16/2026-11-13-batch-gm-results.txt)
retain combined Dashboard/panel/TLS checks, input focus, dynamic APIs and
onCreate timing classification as open work. The [N–R follow-up](test-evidence/phase15-16/2026-11-13-batch-nr-results.txt)
closes reconnect UI (O) and launcher captions (N). Historical failures
and their corrections below explain the earlier test history.

The initial USB installation returned `installed: true` but Dashboard launch
crashed. After the memory correction it loaded with a separate layout failure:
the app occupied a small upper-left rectangle, tabs overlapped, and the rest
of the app area stayed white. Those launch/layout failures are superseded by
the successful G–M captures; the remaining APIs are not closed by that result.

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
Host integration checks verify root size, tab and button bounds, lack of
button overlap, and access to the lower controls. K's supplied photos and
control observations now confirm the installed layout correction on the board.

- [x] After app-only flashing, confirm the boot message
  `UI and JSON allocators use PSRAM; task stacks remain internal`.
  Observed at 1326 ms in the saved application-flash/boot transcript.
- [ ] Save internal heap measurements before/after Wi-Fi and during HTTPS.
  Verify Weather and the HTTPS smoke request succeed, then open the dashboard
  with Wi-Fi active and repeat while the Local Control Panel is enabled.
  J proves Weather HTTP 200 and HTTPS smoke HTTP 200/577 bytes with Dashboard
  running after reboot (TLS minima: 26711 and 25807 internal bytes). M proves
  Weather/TLS success while the panel is enabled, but before Dashboard starts.
  No supplied excerpt establishes all three at once, so this combined check
  remains open; do not combine separate runs into simultaneous-load evidence.
  O adds successful panel+Weather/HTTPS requests but Dashboard starts only
  at 158666 ms, after both requests finish at 36422 ms. Free internal memory
  reaches 5807 bytes and the largest internal block reaches 3328 bytes at
  different samples; investigate headroom before controlled combined-load
  validation. This capture contains successful requests, not an OOM.

- [x] With the declarative package installed over USB or signed Wi-Fi, reboot
  the board and launch it from the on-device launcher.
  J confirms the large signed Dashboard launches after reboot and appears
  in both management lists; its onCreate is captured at 10191 ms.
- [x] Confirm the app creates its tabs and widgets without a watchdog reset,
  allocation failure, or LVGL assertion.
  K photos and repeated launch/teardown observations support this functional
  result. The reported 95–120 ms onCreate times exceed the 80 ms budget and
  remain an open performance finding.
- [x] Confirm the dark dashboard fills the app area below the system status
  bar, all three tab titles fit, Timer buttons are separate and readable, and
  Controls scrolls to the chart and text input without a white background.
  K photos show the full dark Status/Timer/Controls area and two separate
  rows of timer buttons; user reports the control checks passed. The lower
  Controls widgets are not all visible in the supplied photo.
- [x] Confirm the visible time/network values update while the app remains open.
  K reports advancing time and connected/disconnected status with the offline
  hint. O confirms all instructed reconnect UI checks, with connected online
  and disconnected offline plus two serial disconnect/reconnect cycles.
- [x] Confirm timer actions run only when the package has the required timer
  permission and are routed only to allowlisted actions.
  K reports 300-second countdown, Pause at 294, matching Running/Paused
  indicators, Resume and Reset to zero. L captures permission rejection for
  timer.arm and unsupported-action rejection for system.reboot.
- [x] Switch away from the app and back several times; confirm widgets and
  bindings are recreated without duplicated timers or stale values.
  K reports three Settings returns and five Dashboard/Dev Tester cycles,
  smooth transitions and matching serial lifecycle events. Same-Dashboard
  PSRAM samples 4471092/4471100/4471108 do not show a large monotonic leak;
  this does not assert a complete memory or long-duration soak pass.
- [x] Close, disable, uninstall, and reinstall the app; confirm cleanup and
  fresh state after reinstall.
  I/J/L/M cover repeated install/launch, persisted disable/enable, clear-data
  onDestroy followed by onCreate, and final boot with zero packages and
  original built-in slots 0–3. No app-owned persistent-state erasure assertion
  is made for a Dashboard with no exposed saved-note behavior.
- [ ] Capture serial and `crystal logs -f` output for launch, destroy, and
  binding events.
  Lifecycle output is supplied in I/M and timer/network serial events in K/M.
  The SSE hook streams app lifecycle/debug lines rather than every binding
  update; do not claim a binding-event stream that was not captured.
- [x] Repeat the external-panel and on-device Manage Apps comparison after
  this lifecycle test.
  J confirms Dashboard in both lists; L confirms correct counts after
  uninstall/disable and re-enable/reboot. M's final boot restores four built-ins.
- [ ] **Open:** complete documented property/event APIs, dynamic list/chart
  behavior, and input focus still require implementation and tests.
- [x] Confirm the updated launcher truncates long captions within its cells
  while management lists retain full names. N reports Action Guard... in the
  launcher and Action Guard Check in management after the app-only update.
- [ ] **Open:** full device memory validation still needs simultaneous
  Dashboard/panel/TLS observations with adequate headroom; implemented lifecycle
  and cleanup already have device evidence.
- [ ] **Open:** ABI v1 is not frozen until the remaining facade surface is
  implemented and reviewed.

## 10. Exit record

### Phase 15

- [x] Local HTTP API functional hardware checks passed across A–F/G–M/N–R
  (auth, management, install, reboot and bounded logs). SSE concurrency and
  active-stream shutdown remain separate open checks in Section 6.
- [x] Wi-Fi control panel served and loaded.
- [x] Browser Live logs functional controls and sign-out passed in P, including
  mobile; stream concurrency/shutdown remain open separately.
- [ ] USB browser/Web Serial transport passed. **Leave unchecked: not implemented.**
- [x] Pairing PIN was shown on-device and accepted once.
- [x] Bearer token was issued once and enforced on all protected requests.
- [x] Ed25519 signed Wi-Fi package was accepted with configured test key ID 240.
- [x] Unsigned/untrusted-key Wi-Fi packages were rejected; signature tampering
  was also rejected without catalog/launcher changes.
- [x] USB developer package installed and reboot-reconciled with no partial
  data reported in the successful transfer (Section 4); interrupted/malformed
  transfer resilience remains a separate unchecked test.
- [x] Browser upload progress was visible for the 1328026-byte package.
- [x] `crystal logs -f` streamed authenticated lifecycle/debug events.
- [x] `crystal run` completed pack, install, launch, and log streaming.
- [x] External panel and on-device Manage Apps stayed consistent after edits.
- [ ] Catalog scope reconciled and hosted catalog/download/install workflow
  validated. Device-side browsing is unimplemented; whether it is required
  must be aligned with the resolved computer-side workflow in plan section 15.2.
- [x] Functional gate: web UI and CLI installs work, and unauthenticated
  requests are rejected (A–F/G–M). This functional gate does not override
  the missing required transports/catalog or the phase closure decision.

### Phase 16

- [x] Implemented DeclarativeApp lifecycle passed on the board (K/L/M).
- [ ] Bounded UI and binding parsing passed on host and device.
- [ ] All documented visual widgets rendered on the board.
- [x] Time, network, and timer bindings updated correctly on the board (K/O),
  including the final reconnect UI observation.
- [x] Allowlisted timer actions routed correctly; missing permission and
  unsupported-action requests were rejected (K/L).
- [ ] Dynamic list/chart APIs and input focus passed. **Leave unchecked while open.**
- [x] Device lifecycle and cleanup passed across launch, hide, destroy, reinstall,
  and reboot.
- [x] Clock, Weather, and Calculator visual references remain expressible
  in the previously recorded real-LVGL host checks; this is visual parity,
  not newly implemented native behavior or device screenshots of all three.
- [ ] ABI v1 was reviewed and frozen. **Leave unchecked while the facade is incomplete.**

## Closure decision

- [ ] Phase 15 is closed: every required Phase 15 box above is checked, and all
  blocked/open items have been implemented and hardware-validated.
- [ ] Phase 16 is closed: every required Phase 16 box above is checked, and the
  remaining facade APIs, device lifecycle checks, and ABI review are complete.
- [ ] Any unchecked box has an owner, issue, and next test date recorded in the
  evidence location.
