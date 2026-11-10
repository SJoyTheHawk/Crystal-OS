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

Run `python3 tools/test_app_platform_v2.py` after ESP-IDF has resolved the
managed dependencies. It builds the production package store, registry,
Calculator, Brookesia, and LVGL under ASan/UBSan, then exercises repeated
installation and lifecycle teardown, malformed packages, resource admission,
state isolation, and leak refusal. Phase 14 is closed; board evidence and open
follow-ups are recorded in [Phase 14 closure notes](../docs/PHASE_14_IMPLEMENTATION.md).

For the Calculator test on a board already using the Phase 14 partition layout,
run `idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=ON build app-flash monitor`. It embeds
a dedicated development fixture, runs 10 install/launch/save/uninstall cycles,
and reboots once to verify persisted state. Leave the panel untouched and wait
for `PASS: Phase 14 device test complete`. Turn the cached option off afterward
with `idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=OFF build app-flash monitor`.
See the implementation notes for expected logs, reruns, and failure handling.
