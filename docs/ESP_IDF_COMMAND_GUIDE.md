# ESP-IDF command guide

Commands in this guide assume the shell is in the Crystal OS repository root:

```bash
cd "/Users/szemy/Workspace/ESP32 Crystal OS"
```

The project targets the Waveshare ESP32-S3-Touch-LCD-4B and is configured for
ESP-IDF 6.1, 16 MB flash, octal PSRAM, a custom `partitions.csv`, and a 2 Mbps
console. Replace `/dev/cu.usbmodem101` with the port assigned to your board.

## 1. Activate ESP-IDF

Run this once in every new terminal session:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py --version
```

The export script supplies `idf.py`, the ESP32-S3 toolchain, and `IDF_PATH`.
If the project uses another local ESP-IDF installation, source that matching
installation instead of mixing toolchains.

## 2. Select the target and resolve components

The target is normally saved in `sdkconfig`, but this is the explicit setup
command for a fresh checkout:

```bash
idf.py set-target esp32s3
idf.py reconfigure
```

The first configure/build downloads the pinned managed components declared by
`main/idf_component.yml`. Do not commit generated `managed_components/`,
`sdkconfig`, or `dependencies.lock`; they are intentionally ignored here.

Review or change Kconfig options interactively with:

```bash
idf.py menuconfig
idf.py reconfigure
```

After changing configuration, rebuild before flashing.

## 3. Build and inspect the image

Normal incremental build:

```bash
idf.py build
```

Useful size and artifact checks:

```bash
idf.py size
idf.py size-components
ls -lh build/crystal_os.bin build/crystal_os.elf build/bootloader/bootloader.bin
```

The ELF is required to decode a coredump. Archive the exact
`build/crystal_os.elf` alongside any firmware image distributed for testing.

Use a clean rebuild only when configuration/dependencies changed or stale
generated output is suspected:

```bash
idf.py fullclean
idf.py build
```

`fullclean` removes the generated `build/` directory; it does not erase the
board.

## 4. Find the USB serial port

On macOS, list likely USB serial devices before connecting and after connecting
the board:

```bash
ls /dev/cu.*
```

The port commonly looks like `/dev/cu.usbmodem101` or `/dev/cu.usbmodem1101`.
Use the `/dev/cu.*` device for this project rather than a Bluetooth or tty
device. Close other serial tools before flashing or monitoring.

## 5. Flash the board

Flash the application, bootloader, partition table, and required data images:

```bash
idf.py -p /dev/cu.usbmodem101 flash
```

Build and flash in one command:

```bash
idf.py -p /dev/cu.usbmodem101 build flash
```

Flash and immediately open the monitor:

```bash
idf.py -p /dev/cu.usbmodem101 flash monitor -b 2000000
```

The configured console rate is 2,000,000 baud. Press `Ctrl+]` to exit the
monitor. `Ctrl+T` then `Ctrl+R` resets the board while the monitor is open.

## 6. Monitor and reset without reflashing

Open a monitor for an already-flashed image:

```bash
idf.py -p /dev/cu.usbmodem101 monitor -b 2000000
```

Reset the running board from another terminal (with the ESP-IDF environment
active):

```bash
idf.py -p /dev/cu.usbmodem101 reset
```

To capture a repeatable boot log, start the monitor, reset the board, exercise
the launcher/apps, then exit with `Ctrl+]`.

## 7. Erase and recovery operations

Erase all application flash when a stale partition/NVS state is preventing a
valid test, then rebuild and flash:

```bash
idf.py -p /dev/cu.usbmodem101 erase-flash
idf.py -p /dev/cu.usbmodem101 build flash monitor -b 2000000
```

This removes NVS data, app state, Wi-Fi credentials, registry order, timer and
stopwatch state, and coredump contents. It does **not** erase eFuses.

The first boot with NVS encryption may provision the configured HMAC eFuse key
slot (`CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=0`). This is a one-time irreversible
hardware operation. Never use an eFuse erase command for routine recovery.

## 8. Coredump diagnostics

When the monitor reports a panic and a coredump is present, keep the matching
ELF and run:

```bash
idf.py -p /dev/cu.usbmodem101 coredump-info
idf.py -p /dev/cu.usbmodem101 coredump-debug
```

If the coredump was saved to a file, pass it using the options shown by:

```bash
idf.py coredump-info --help
```

An `Incorrect size of core dump image` message on a first boot is commonly
stale/uninitialized coredump-partition contents; treat it as a warning unless
it is followed by a panic or reset loop.

## 9. Host-side smoke check

The standalone HAL mock can be checked without ESP-IDF or hardware:

```bash
clang++ -std=c++17 \
  -Icomponents/crystal_hal/include \
  -fsyntax-only sim/crystal_hal_mock.cpp
```

This currently reports an abstract `MockWifi` error until the mock implements
the `IWifi::has_ip()` interface method. It does not build the LVGL/SDL
simulator; see [`SIMULATOR.md`](SIMULATOR.md) for its current scope.

## Quick reference

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
cd "/Users/szemy/Workspace/ESP32 Crystal OS"
idf.py set-target esp32s3       # fresh checkout only
idf.py build                    # compile
idf.py -p /dev/cu.usbmodem101 flash
idf.py -p /dev/cu.usbmodem101 monitor -b 2000000
idf.py -p /dev/cu.usbmodem101 flash monitor -b 2000000
idf.py fullclean && idf.py build
```
