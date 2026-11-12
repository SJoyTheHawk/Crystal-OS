# Crystal OS recovery tools

## README screenshots

Run `python3 tools/capture_readme.py` to render the production Brookesia
launcher, Crystal shell Quick Settings panel, Clock, Weather, and Calculator
interfaces into `docs/images/`. The headless LVGL capture uses fixed sample
time, weather, battery, brightness, volume, and Wi-Fi status; it does not need a
device or network connection. It requires CMake, a C++17 compiler, and the
checked-out managed LVGL dependency.

## Launcher swipe regression check

Run `python3 tools/test_launcher_swipe.py` to build a headless test with the
production Brookesia launcher/manager and the checked-out LVGL dependency.
It requires CMake and a C++ compiler with AddressSanitizer support.

The test sends pointer input through LVGL and checks live page positions,
center/edge/icon/indicator-band swipes, flicks, snap-back, reversal, interruption,
page limits, vertical drags, page dots, app taps after settling, returning from
an app, and teardown during animation. It does not exercise Crystal's shell
overlays or measure frame pacing on the physical panel.

## Wi-Fi scan/reconnect regression check

Run `python3 tools/test_crystal_memory.py` to check the production LVGL and
cJSON allocation paths with capability-checked host heaps under ASan/UBSan.
It verifies PSRAM allocation, reallocation, failure without internal-RAM
fallback, and cleanup. It does not measure the board's available memory.

Run `python3 tools/test_crystal_wifi.py` to exercise the production Wi-Fi
adapter with a scripted host driver under AddressSanitizer and UBSan. It checks
scanning during saved-network retries and backoff, queued retry callbacks,
connected scans, scan failures, radio-off/forget cancellation, and late scan
events. It requires a C++17 compiler and does not simulate ESP32 radio timing.

For device verification, boot with a saved SSID out of range and open WiFi
Networks while it is retrying. Nearby networks should appear; leaving the
saved credentials in place should still allow reconnection when that SSID
returns. Also check scanning while connected and toggling Wi-Fi off mid-scan.

## USB recovery

The recovery tool uses the ESP-IDF build metadata in `build/flasher_args.json`,
so it follows the offsets generated for the current partition table. Build the
firmware first, then connect the board over USB and identify its serial port.

Install the only external dependency with:

```bash
python3 -m pip install esptool
```

Preserve mode restores the bootloader, partition table, OTA metadata, and
application image. It leaves NVS settings, the SPIFFS storage partition, and
the coredump partition untouched:

```bash
./tools/crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware build --preserve
```

Factory reset asks for two confirmations, erases the complete flash, and
flashes every image listed by ESP-IDF:

```bash
./tools/crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware build --factory-reset
```

Extract the coredump partition without changing the device. The partition
offset and size are read from `partition-table.bin`:

```bash
./tools/crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware build \
  --extract-coredump /tmp/crystal-coredump.bin
esp-coredump.py info_corefile -t raw build/crystal_os.elf /tmp/crystal-coredump.bin
```

Use `CRYSTAL_ESPTOOL` to select a particular executable or wrapper, for
example `CRYSTAL_ESPTOOL="python3 -m esptool"`. The tool streams esptool's
progress and returns a non-zero status when a confirmation, validation, or
flash operation fails.

## App packages (Phase 14)

`./tools/crystal pack examples/calculator_package --unsigned -o /tmp/calculator.capp`
creates the development proof package. `./tools/crystal inspect /tmp/calculator.capp`
checks the stream layout and CRCs. General YAML and Ed25519 signing use the
optional dependencies in `requirements-pack.txt`; this JSON-syntax example
needs only Python's standard library.

For the Phase 16 declarative runtime example, run
`python3 tools/test_crystal_declarative.py`. It validates the dashboard's
widget/binding/action references and packs and inspects its `.capp` stream.

The authenticated Wi-Fi developer commands are:

```sh
./tools/crystal install ./signed.capp --wifi http://DEVICE/ --pin 123456
./tools/crystal logs -f --wifi http://DEVICE/ --token TOKEN
./tools/crystal run ./myapp --wifi http://DEVICE/ --pin 123456 --key /secure/package.pem
./tools/crystal pack ./myapp --unsigned -o /tmp/myapp.capp
./tools/crystal install --usb --port /dev/cu.usbmodem101 /tmp/myapp.capp
```

The device must have a matching trusted public key configured for signed
package installation over Wi-Fi. USB is a physical developer-mode transport;
it accepts unsigned packages and uses the same atomic installer. Browser Web
Serial support and hardware transport validation remain open.

Before `install --usb`, exit IDF Monitor with `Ctrl+]` and close other serial
terminals using the selected port. The installer must have exclusive access to
the protocol replies. Reopen the monitor after the command returns. A successful
response with `restart_required: true` means the package is stored; reboot the
board to load it into the running app registry.

## Curated online catalog

The catalog client runs on the user's computer and fetches a manually maintained
signed JSON index plus prebuilt, signed `.capp` files from HTTPS hosting. The
host serves files; it does not build apps. The downloaded package is then
transferred to the device by the developer transport.
See [`docs/APP_CATALOG_FORMAT.md`](../docs/APP_CATALOG_FORMAT.md) for the schema,
canonical signature bytes, and publish workflow. The production hosting URL and
release public keys are deployment configuration and are not checked in.

```sh
./tools/crystal catalog-sign catalog-source.json \
  --key /secure/catalog-private.pem --key-id 7 -o public/catalog.json
./tools/crystal catalog https://apps.example.org/catalog.json \
  --trusted-key 7=/secure/catalog-public.pem
./tools/crystal catalog-download https://apps.example.org/catalog.json \
  org.crystal.example -o ./org.crystal.example.capp \
  --trusted-catalog-key 7=/secure/catalog-public.pem \
  --trusted-package-key 12=/secure/package-public.pem
```

`python3 tools/test_crystal_signatures.py` exercises package and catalog
signing, key separation, unknown-key rejection, tampering, and HTTPS URL policy.
`python3 tools/test_crystal_crypto.py` compiles the production ESP32 entropy
adapter against the bundled libsodium with host RNG doubles. It checks provider
selection, boot entropy cleanup, repeated initialization, token/PIN generation,
and an offline RFC 8032 Ed25519 verification/tamper vector. Hardware randomness
and browser pairing still require a board check.

Run `python3 tools/test_app_platform_v2.py` after ESP-IDF has resolved the
managed dependencies. It builds the production package store, registry,
Calculator, Brookesia, and LVGL under ASan/UBSan, then exercises repeated
installation and lifecycle teardown, malformed packages, resource admission,
state isolation, and leak refusal. Phase 14 is closed; board evidence and open
follow-ups are recorded in [Phase 14 closure notes](../docs/PHASE_14_IMPLEMENTATION.md).

The same integration test checks the declarative dashboard's app bounds, tabs,
Timer button spacing, and scrolling. To save rendered PPM previews of Status,
Timer, and Controls, run:

```sh
CRYSTAL_LAYOUT_CAPTURE_DIR="$PWD/.cache/declarative-layout" python3 tools/test_app_platform_v2.py
```

For the Calculator test on a board already using the Phase 14 partition layout,
run `idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=ON build app-flash monitor`. It embeds
a dedicated development fixture, runs 10 install/launch/save/uninstall cycles,
and reboots once to verify persisted state. Leave the panel untouched and wait
for `PASS: Phase 14 device test complete`. Turn the cached option off afterward
with `idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=OFF build app-flash monitor`.
See the implementation notes for expected logs, reruns, and failure handling.

## Browser control panel

In device Settings → Developer Tools, Local Control Panel uses the same
right-aligned switch as the other Settings toggles. It is disabled while
startup is pending, returns off if Wi-Fi or startup fails, and opens the
pairing dialog when startup succeeds. Turning it off ends the local panel
service. Reopening Developer Tools reflects the current service state.
Run `python3 tools/test_developer_tools_page.py` for real-LVGL checks of the
production row, switch, startup callback, failure recovery, and page exit.
Add `--output-dir /tmp/crystal-developer-tools-preview` to save renders.
Networking, worker creation, and the page header use fixtures in this test.

The device serves the self-contained page in assets/control-panel/index.html.
Its fonts, icons, styles, and scripts require no external downloads. The pairing
screen and app dashboard adapt to desktop and mobile browsers.
Above 800 px, apps, installation, and device information are visible together
in a two-column workspace. At 800 px and below, Apps, Install, and Device tabs
show one section at a time. Resizing preserves pairing, the selected package,
and the active mobile tab; keyboard focus stays in a visible section.
App rows keep ↑ and ↓ ordering buttons on the right. Launch, enable/disable,
clear data, and uninstall are visible directly without an expandable options
section. Clear data and uninstall still require confirmation.
Sign out sits beside Refresh above the tabs and clears this browser's session.
Reboot appears below Live logs in a matching Device card with the same padding,
heading, description, and secondary action button.
Its confirmation defaults to Cancel; Reboot now sends an authenticated
`POST /api/v1/system/reboot`, then returns the browser to pairing. The device
acknowledges before restarting and rejects reboot while an install is active.
After restart, enable Local Control Panel again and pair with a new code.
Confirmations match the panel's bordered cards, with a gray Cancel button on
the left and a red confirmation button on the right.

The header logo is an embedded JPEG in the HTML, so it requires no extra asset
route. Its optimized 144×121 source is assets/control-panel/logo.jpg; the
original is retained in docs/images/control-panel-logo-source.jpg. It displays
at 44 px wide on desktop and 32–36 px on narrow screens, with its proportions
preserved. Update the embedded image when replacing the source logo.

Run the browser regression suite with Node 22+ and Chrome installed:

    node tools/test_control_panel.mjs

Optional desktop/mobile screenshots outside the repository:

    CRYSTAL_PANEL_PREVIEW_DIR=/tmp/crystal-control-panel-preview node tools/test_control_panel.mjs

On macOS the suite uses Google Chrome's standard application path; elsewhere it
uses google-chrome. Set CHROME_BIN to override it. Tests launch an isolated
headless browser with simulated API responses; they do not connect to a board.
Coverage includes pairing, invalid codes, expiry, app actions and confirmation,
ordering, uploads, errors, log framing, and responsive layouts. Actual device
pairing, signed-package installation, Safari, and hardware logs remain board
checks in the Phase 15/16 test guide.
Run `python3 tools/test_control_reboot.py` for the production reboot and token
guard regression under ASan/UBSan. It checks unauthorized and expired tokens,
install exclusion, queue failures, duplicate requests, and response ordering
with HTTP/restart stubs; it does not reset a physical device.

Run `python3 tools/test_shell_control_launch.py` for the shell remote-launch
regression. It exercises the production launch, card-start, and Settings-close
functions with a deterministic UI fixture under ASan/UBSan. It checks root and
nested Settings, the return selection, repeated launches, newly installed apps,
and failure preservation. Real LVGL rendering and device event delivery still
require the board checks; `python3 tools/test_app_platform_v2.py` separately
exercises the production app lifecycle through LVGL/Brookesia.

To deploy a page change, close the serial monitor, then run:

    idf.py build
    idf.py -p /dev/cu.usbmodem1101 storage-flash

The storage-flash target rewrites the system asset partition from assets/. It
leaves the separate LittleFS app partition and NVS intact. The app-flash target
alone does not update the page. After flashing, enable Local Control Panel again
on the device, reload the browser page, and pair with the new code. Use the
current port if it changes.
The new Reboot endpoint also requires the application update. To deploy both
the page and this firmware change, use `idf.py -p /dev/cu.usbmodem1101 app-flash storage-flash`
after building, with the serial monitor closed.
