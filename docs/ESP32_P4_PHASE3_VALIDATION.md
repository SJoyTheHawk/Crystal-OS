# ESP32-P4 Phase P4.3: Network expansion

Started: 2026-11-15 (Asia/Hong_Kong). Base: 6cd375c.
Status: **closed for the accepted P4.3 scope — 2026-11-15 (Asia/Hong_Kong).**
Ethernet link, DHCP, preferred route, Internet requests and user-confirmed cable-removal/reconnection pass;
Ethernet-only Weather, SNTP, HTTPS, browser pairing/authenticated app-list access
and device pairing dialog readability pass. Ethernet Settings/app details,
scrolling and return navigation also have user acceptance. A fresh Weather
refresh after switching to Wi-Fi alone passes. The user confirms the corrected
Ethernet/Wi-Fi status indicator, 2px status-bar offset, rounded launcher icons
while pressed and complete rounded switcher artwork all pass on device.
Sustained network/performance/soak qualification continues in P4.4.
The earlier Weather timeout remains recorded below; repeated success does not establish its precise cause.

## Scope and design

- Follow the v2 plan's Network Expansion scope. The older Phase 3 Wi-Fi port is
  already operational; retain its C6 firmware, credentials and Crystal HAL.
- Implement the board's IP101/address-1 RMII wiring from the vendor factory BSP:
  reset 51, MDC 31, MDIO 52, external 50MHz clock 50, TX_EN 49, TXD0/1 34/35,
  CRS_DV 28, RXD0/1 29/30. The generic LAN87xx initialization in the original
  migration plan is illustrative and is not correct configuration for this board.
- Pin espressif/ip101 1.1.0 for IDF v6.1, matching the factory dependency. Its
  dedicated header replaces the old built-in PHY declaration.
- Enable per-interface DNS so switching the default route also selects the
  DHCP DNS servers belonging to that interface.
- Prefer Ethernet only after IPv4 acquisition. Keep Wi-Fi enabled and associated
  according to its existing user setting for fallback; never erase credentials or
  silently turn the radio off. Link loss must invalidate stale Ethernet addresses.
- Share network readiness with Weather, SNTP, HTTPS and the opt-in control panel.
  The P4 reserves ADC1 for the SDK local entropy source after crypto initialization:
  external C6 Wi-Fi cannot provide entropy to P4, including Ethernet-only sessions.
  The S3 boot-entropy lifetime remains unchanged.
  Distinguish local IPv4 availability from verified Internet reachability.
- Keep immediate Settings navigation, accepted 720px typography, Crystal dark
  surfaces and standard information rows. Ethernet details sit under Network;
  no second navigation system or heavyweight transition cache is introduced.
- Add a P4-only Ethernet diagnostics card using CrystalApp lifecycle and
  snapshot-only one-second refresh, plus a small RJ45 status icon in the existing
  Brookesia status-bar area. Show real status/IP/MAC/speed; do not fabricate packet
  statistics or unsupported 1Gbps/IPv6 values.
- Driver ownership stays in crystal_hal alongside Wi-Fi and board access; core
  orchestrates background startup and publishes aggregate network events.
- Existing unrelated brightness/build changes are preserved separately. The
  separate Downloads factory workspace and installed C6 firmware remain untouched.

## Completed P4.3 acceptance

- [x] IDF v6.1 build and focused host policy/crypto/UI regressions
- [x] App-only flash with written-data hash verification
- [x] Final Wi-Fi-only Weather refresh without an Ethernet cable (user-confirmed)
- [x] Ethernet PHY initialization, 100 Mbps link and DHCP
- [x] User accepts device Ethernet details, including assigned IP and 100 Mbps link speed
- [x] Ethernet preferred when both interfaces have IPv4
- [x] Cable removal falls back to Wi-Fi (user-confirmed Settings state)
- [x] Reconnect restores Ethernet preference (user-confirmed Settings state)
- [x] Ethernet-only Weather/SNTP/HTTPS, including three user-confirmed refreshes
- [x] Ethernet-only opt-in panel starts and serves the browser pairing page (user-confirmed)
- [x] Device pairing dialog readability, with no clipping or overlap (user-confirmed)
- [x] Complete browser pairing and load the authenticated installed-app list over Ethernet (user-confirmed)
- [x] Device UI: Ethernet Settings/app readability, scrolling and return navigation
- [x] Device UI: Ethernet/Wi-Fi status-bar indicator (user-confirmed)
- [x] Device UI: accepted 2px status-bar offset (user-confirmed)
- [x] Device UI: launcher icons stay rounded while pressed; switcher artwork is complete with rounded corners (user-confirmed)
- [x] Disclose unmeasured radio stability, sustained traffic and soak limits

## Device evidence — 2026-11-15

- IDF v6.1 signed application built successfully (0x491000 bytes; 43% free in
  the 8 MiB application slot). App-only flash at 0x30000 passed hash verification.
  SHA-256: 630f713e01399a41531c3b25b2c06d68ccdfb93f02f3c8c7872d5521080829b4.
- Initial successful capture: /private/tmp/crystal-phase3-ethernet-live.txt. IP101
  initialized at 4669 ms; link up at 7169 ms (100 Mbps); DHCP at 8181 ms:
  192.168.1.233 / 255.255.255.0, gateway 192.168.1.1. Policy selected
  Offline -> Ethernet. The user independently reported Connected and this IP.
- SNTP synchronized at 8674 ms, live Weather fetched at 12496 ms, and HTTPS
  smoke returned HTTP 200 / 577 bytes at 14722 ms. These occur after the Ethernet
  route selection. This proves routed traffic, not a controlled Wi-Fi-off test.
- The laptop received three ping replies from 192.168.1.233 with no loss.
- Earlier captures showed no cable link during their limited capture window;
  that absence is superseded by the positive link and DHCP evidence above.
- Final cleanup build and app-only flash also passed. Its SHA-256 is
  69b9dbb87c0c6f4008c0da399636ec60cf8ff2e79007d58f8600ae67f53e8fb1
  (same 0x491000-byte signed image size). In
  /private/tmp/crystal-phase3-final-boot.txt, IP101 initialized at 4702 ms,
  100 Mbps link came up at 7202 ms and DHCP/default Ethernet route at 8205 ms
  using the same address/mask/gateway. Wi-Fi got 192.168.1.52 at 8813 ms and
  did not displace Ethernet. SNTP synchronized at 11215 ms, Weather fetched at
  12495 ms and HTTPS smoke returned 200 / 577 bytes at 14725 ms. No panic,
  assertion or brownout appeared in this capture.
- User confirms that unplugging Ethernet with Wi-Fi left enabled changes
  Active Connection to WiFi and Ethernet to disconnected. This verifies the
  physical removal/UI fallback check; no new serial capture or post-fallback
  application request was collected for this step.
- User confirms reconnecting the cable with Wi-Fi enabled restores Ethernet as
  Active Connection and shows Connected with an IP address. This completes the
  requested physical unplug/replug Settings checks. A fresh application request
  after switching and controlled Ethernet-only operation are separate checks.
- User confirms turning Wi-Fi back on, waiting for association, unplugging
  Ethernet and tapping Weather Refresh completes successfully with Updated just
  now. This verifies a fresh application request on Wi-Fi after Ethernet loss;
  it does not establish uninterrupted continuity of existing TCP connections.
- C6 firmware was not flashed. Status-bar acceptance was open at this capture
  and is now closed by the user confirmation below. Sustained traffic and soak
  remain P4.4 qualification tasks.
- User confirms both Settings → Network → Ethernet and the Ethernet app look
  correct and work: details including the assigned IP and 100 Mbps link speed
  are readable, scrolling works, and return navigation has no clipping or stuck
  controls. This accepts these two views; the status-bar indicator was not part
  of that question. No independent router comparison of MAC/DNS was collected.
- Ethernet-only check: user reports Weather Refresh fails or stays busy after
  turning Wi-Fi off. Capture /private/tmp/crystal-phase3-ethernet-only-failure.txt
  shows Ethernet DHCP/default route and SNTP success, Weather connection timeouts
  (ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT, about 8.2 seconds), and an intervening
  HTTPS smoke request succeeding (HTTP 200, 577 bytes). This does not qualify
  Weather or prove a general Ethernet outage. Recovery evidence follows.
- Updated P4 Weather transport budget from 8 to 15 seconds (matching the existing
  framework HTTPS probe); the 30-second completion/cancellation bound and the
  S3 8-second budget remain unchanged. The empty-state prompt now says network
  instead of WiFi. Build and app-only flash passed hash verification; SHA-256:
  a1b1854f19e28ded4756e55ef796db9e643971b875b3ee267338a98bbcdbef1f.
- With Wi-Fi still off, /private/tmp/crystal-phase3-weather-budget-boot.txt
  records Ethernet as the only active interface, two Weather HTTP 200 requests
  in 4029/4021 ms, SNTP synchronization, and HTTPS smoke success. The user then
  confirms three additional manual Weather refreshes all pass. The successful
  requests are shorter than the old timeout, so the reboot/timeout trial does
  not prove that the extra budget resolves the original intermittent failure.
  Keep this limitation visible for P4.4 repeated network/endpoint testing.
- User confirms that the laptop loads the pairing page with Wi-Fi off and
  Ethernet connected. This verifies the opt-in HTTP server is reachable over
  Ethernet. The subsequent authenticated pairing result is recorded below.
  The user reports that the device pairing dialog still has small text. Its
  original fixed 400x330 card and 20/28/16/14px fonts match the S3 layout;
  this is corrected by the P4 sizing update below.
- With Wi-Fi still off and Ethernet connected, the user confirms entering the
  device code pairs the browser successfully and loads the installed-app list.
  This verifies authenticated read access through Ethernet. App installation,
  package mutations and other control-panel operations are separate P4.4 checks.

## Closure checkpoint — 2026-11-15

The user confirms “All checked ok” after the launcher press-rounding, switcher
full-artwork and Ethernet status-icon corrections. This accepts those device UI
fixes, including the retained 2px status-bar offset, and closes the last P4.3
acceptance item. Phase P4.3 is complete for its documented scope.

This follow-up is user-reported device acceptance; no new flash/hash-verification
log or serial capture was collected with it. Earlier flash evidence remains
attached to its recorded binaries. Controlled radio reconnect/timing, sustained
traffic, DNS failover across different servers, active-socket continuity, the
earlier intermittent Weather timeout, TLS stress, full S3 regression, packages,
OTA, performance and soak remain later qualification work.

## Pairing dialog readability correction — 2026-11-15

- P4 card is 600x495, with proportional padding, spacing and corner radius;
  the Done button is 210x63. Colors and modal behavior follow the existing dialog.
- P4 fonts: title 28px, code 48px, instructions 24px, notice 20px, Done 24px.
  S3 retains its 400x330 card, original fonts and 140x42 Done button.
- Production dialog functions pass real-LVGL tests on both resolutions with
  ASan/UBSan: card bounds, font selection, three URL lengths, wrapped instruction
  height, no overlapping elements, Done dismissal, modal state and rebuild cleanup.
  Both rendered previews were visually reviewed. Test: tools/test_control_pair_dialog.py;
  local log: /private/tmp/crystal-phase3-pairing-test.log.
- P4 IDF v6.1 build and application-only flash pass, including written-data hash
  verification. SHA-256:
  bfa33b1e97de65628b18bd75e0a983c5dc0f24f87c69da83f2ea827f97f41994.
  In /private/tmp/crystal-phase3-pairing-boot.txt, Ethernet is the active route
  with Wi-Fi off, SNTP synchronizes, Weather fetches and HTTPS smoke passes.
  The user confirms the title, pairing code, instructions and Done button are
  readable with no clipping or overlap. Device readability acceptance passes;
  subsequent browser pairing/app-list acceptance is recorded above. C6 firmware
  and data partitions were not flashed.

## Host regression evidence

- UI follow-up, 2026-11-15: launcher rounding now follows the image through
  press/release/press-loss; switcher frames use LVGL REAL size mode to enclose
  the full 1.5x artwork. Ethernet artwork uses true-color plus alpha, supported
  by the LVGL 8 transform renderer, instead of unrenderable scaled alpha-only
  pixels. The existing 2px status-bar offset is retained.
- `python3 tools/test_shell_icon_rendering.py` passes with real LVGL and
  AddressSanitizer: rendered rounded corners in normal/pressed/released/lost-press
  states, stable caption placement, all four source-edge markers after switcher
  zoom, visible Ethernet pixels and network/percentage/battery ordering across
  Ethernet/Wi-Fi/disconnected transitions, including Ethernet on first update.
  The harness uses production widgets and extracts production callback/icon
  construction; route state is a fixture. P4 `idf.py build`, launcher swipe
  regression and whitespace checks pass. At implementation handoff the correction
  was built but not flashed by the agent. Subsequent user device acceptance is
  recorded in the closure checkpoint above.
- Production route-policy test passes with AddressSanitizer/UndefinedBehaviorSanitizer:
  all interface combinations, IPv4 gating, missing-netif/default-route failure
  retry, loss/recovery, repeated switching and notification deduplication.
- Production crypto passes for P4 and S3: initialization, entropy lifetime,
  randomness, offline Ed25519 and rejection of tampered signatures. These are
  provider/lifecycle checks, not a certification of physical entropy quality.
- Real-LVGL tests pass for Ethernet Settings state/address updates, readable row
  geometry, unchanged-label refresh, close/reopen and timer cleanup. A targeted
  regression also verifies that closing Network clears its cached Ethernet and
  active-interface rows before a later connectivity update can access them.
- The production Ethernet app passes state/text geometry, address/MAC/speed,
  idle refresh, pause/resume timer uniqueness and destroy/relaunch cleanup with
  real LVGL and a deterministic HAL fixture. Its rendered dark information rows
  were visually reviewed; user device acceptance is recorded separately above.
- Wi-Fi scan/password/keyboard, S3/P4 keyboard proportions, Developer Tools
  including Ethernet-only readiness, Wi-Fi HAL, core HTTP adapter, control actions,
  shell launch and visual regressions pass. Network/RPC fixtures do not qualify
  physical radios, reconnect timing or a fresh full S3 firmware/device regression.
- Route refresh notifications are serialized across event and service tasks;
  UI notifications remain queued onto LVGL. No PHY/RPC work runs in UI refresh.

Logs are under /private/tmp/crystal-phase3-*-test.log for this session; they are
local evidence, not checked-in generated artifacts. Controlled Wi-Fi reconnect,
sustained wired/wireless traffic, DNS failover between different servers,
active socket continuity, TLS stress, S3 hardware regression and soak remain
unmeasured. Default-route fallback does not migrate an already-open TCP socket;
services must reconnect/retry when their current connection breaks.
