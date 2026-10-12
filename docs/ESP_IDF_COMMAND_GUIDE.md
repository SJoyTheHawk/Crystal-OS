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

## 4. Signed development builds

The ESP-IDF export command is needed once in every new terminal session. It is
not necessary to regenerate the signing key for every build.

Generate the development RSA-3072 key once, outside the repository:

```bash
mkdir -p ~/.crystal_keys
espsecure generate-signing-key \
  --version 2 \
  --scheme rsa3072 \
  ~/.crystal_keys/dev_signing_key.pem
```

Enable RSA signing once per checkout (or whenever `sdkconfig` is recreated):

```bash
idf.py menuconfig
```

Choose **Security features → Require signed app images → App Signing Scheme →
RSA**, save, and exit. The setting should be present in `sdkconfig` as:

```text
CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y
```

For each signed development build, source ESP-IDF if the terminal is new and
run:

```bash
./scripts/build_dev.sh
espsecure signature-info-v2 build/crystal_os.bin
```

The verification output should say `Signature block 0 is valid (RSA)`. Keep
the private `.pem` files outside Git. The public key digest printed by
`signature-info-v2` can be recorded with a release manifest, but does not need
to be memorized.

## 5. Find the USB serial port

On macOS, list likely USB serial devices before connecting and after connecting
the board:

```bash
ls /dev/cu.*
```

The port commonly looks like `/dev/cu.usbmodem101` or `/dev/cu.usbmodem1101`.
Use the `/dev/cu.*` device for this project rather than a Bluetooth or tty
device. Close other serial tools before flashing or monitoring.

## 6. Flash the board

Flash the application, bootloader, partition table, and required data images:

```bash
idf.py -p /dev/cu.usbmodem101 flash
```

For the fast development loop, flash only the application image:

```bash
idf.py -p /dev/cu.usbmodem101 app-flash
```

`app-flash` preserves NVS, SPIFFS, and other data partitions because it writes
only the selected OTA application. Use it when testing code changes that do
not change the partition table, bootloader, or filesystem contents. A normal
`flash` is required after changing `partitions.csv`, bootloader settings, or
data/filesystem images.

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

## 7. Monitor and reset without reflashing

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

## 8. Erase and recovery operations

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

## 9. Coredump diagnostics

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

## 10. Host-side smoke check

The standalone HAL mock can be checked without ESP-IDF or hardware:

```bash
clang++ -std=c++17 \
  -Icomponents/crystal_hal/include \
  -fsyntax-only sim/crystal_hal_mock.cpp
```

This verifies the complete host HAL contract, including WiFi, power, and system
information mocks. It does not build the LVGL/SDL simulator; see
[`SIMULATOR.md`](SIMULATOR.md) for its current scope.

## 11. Prepare and host an OTA test release

Use this workflow to test **0.12.8 → OTA update → 0.12.9** with one device.
The version numbers are an example: the hosted version must be newer than the
version running on the device.

### Prepare the device baseline

Set the root `CMakeLists.txt` version to:

```cmake
project(crystal_os VERSION 0.12.8)
```

Build and flash the baseline containing the latest Phase 12.9 fixes:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
./scripts/build_dev.sh
idf.py -p /dev/cu.usbmodem101 flash monitor -b 2000000
```

Confirm Settings → System → Device Status reports `0.12.8`, and Wi-Fi connects.
Exit the monitor with `Ctrl+]`. If this baseline is already on the device,
continue directly to packaging.

### Build and package the newer release

Change the root `CMakeLists.txt` version to:

```cmake
project(crystal_os VERSION 0.12.9)
```

Run from the repository root in an ESP-IDF terminal:

```bash
./scripts/prepare_ota_release.sh --build ~/Desktop/crystal-os-ota
espsecure signature-info-v2 build/crystal_os.bin
```

`--build` runs `scripts/build_dev.sh` before packaging. Use the same signing key
as the baseline. Expect `Signature block 0 is valid (RSA)`.

The packaging script reads the version from `CMakeLists.txt`, copies the signed
binary, and writes `manifest.json` with its version, byte size, SHA-256, target,
channel, download URLs, and notes. It produces:

```text
~/Desktop/crystal-os-ota/
├── rockyroad.studio/apps/crystal-os/
│   ├── stable/manifest.json
│   └── releases/0.12.9/crystal_os.bin
└── updates.bubstal.com/crystal-os/
    ├── stable/manifest.json
    └── releases/0.12.9/crystal_os.bin
```

Each run replaces these generated host trees in the output directory. The
script packages files locally; upload them separately. Keep the matching ELF
in a local archive for decoding logs or crashes; it is not included in the
generated hosting tree.

### Upload to the hosting directory

Using your hosting file manager or SFTP, merge the contents under
`rockyroad.studio/` into that domain's document root. If already inside the
server's `apps/crystal-os/` directory, copy the generated `stable/` and
`releases/` contents there. Keep existing release directories.

Upload the binary first, then replace the stable manifest so it only advertises
an image that is already available. The public URLs for this example are:

```text
https://rockyroad.studio/apps/crystal-os/releases/0.12.9/crystal_os.bin
https://rockyroad.studio/apps/crystal-os/stable/manifest.json
```

For mirror testing, also upload the contents under
`updates.bubstal.com/` to that domain's document root, using the same
binary-first order. The directory is generated now so it can be uploaded when
the Bubstal site becomes available.

Verify the primary manifest and binary are reachable:

```bash
curl --fail --show-error --silent \
  https://rockyroad.studio/apps/crystal-os/stable/manifest.json
curl --fail --head \
  https://rockyroad.studio/apps/crystal-os/releases/0.12.9/crystal_os.bin
```

The manifest should report version `0.12.9` and match the generated local
manifest's size and SHA-256. If hosting uses a cache/CDN, refresh its manifest
cache when publishing.

### Test on the device

Leave the device running `0.12.8`; the new `0.12.9` build is for hosting, so do
not USB-flash it before this test.

1. Open Settings → System → Check for Updates.
2. Confirm the **Update Available** dialog shows `0.12.9`.
3. Tap **Cancel** to verify dismissal, then check again and tap **Download**.
4. Check that download progress appears, installation finishes, and the device
   reboots. Watch for any `task_wdt` warning during checks and download.
5. Confirm Device Status now reports `0.12.9`. Check again: the expected result
   is the **System is up to date** toast.

For OTA watchdog diagnostics, see the
[Phase 12 quick reference](PHASE_12_QUICK_REFERENCE.md#issue-task_wdt-reports-idle0-while-checking-for-updates).

## Quick reference

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
cd "/Users/szemy/Workspace/ESP32 Crystal OS"
idf.py set-target esp32s3       # fresh checkout only
idf.py build                    # compile
idf.py -p /dev/cu.usbmodem101 flash
idf.py -p /dev/cu.usbmodem101 app-flash  # preserve NVS/SPIFFS
idf.py -p /dev/cu.usbmodem101 monitor -b 2000000
idf.py -p /dev/cu.usbmodem101 flash monitor -b 2000000
idf.py fullclean && idf.py build
```
