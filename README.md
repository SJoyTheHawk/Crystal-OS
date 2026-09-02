# Crystal OS

Crystal OS is an ESP32-S3 touch-device shell and app framework built on
LVGL and Espressif's `esp-brookesia` phone UI.

## Target hardware

- Waveshare ESP32-S3-Touch-LCD-4B
- ESP32-S3 with 16 MB flash and octal PSRAM
- 480x480 RGB LCD
- GT911 touch controller over I2C
- ESP-IDF 6.1
- LVGL 8.4.0
- `esp-brookesia` 0.4.2

## Current status

- Phase 0: complete. The launcher, Hello app, touch input, and return-to-launcher flow were verified on hardware.
- Phase 1: complete. The performance gate measured approximately 7-8 visible FPS while dragging. Double buffering did not improve the result, so the future switcher will use a simplified animation.
- Phase 2: in progress. The hardware abstraction layer includes device brightness, NVS storage, a lazy Wi-Fi station adapter, a PCF85063 RTC adapter, and a raw-touch adapter. A standalone host mock is available under `sim/`; the full SDL UI target remains.
- Phase 3: implemented; final physical-device validation is pending. `crystal_core` provides the UI event queue, toast overlay, RTC-first clock, SNTP correction, and ramped dim/off inactivity states.

See [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) for phase exit criteria and decisions.
The current desktop mock workflow is documented in [`docs/SIMULATOR.md`](docs/SIMULATOR.md).
Phase 3 hardware checks are documented in [`docs/PHASE_3_SERVICES.md`](docs/PHASE_3_SERVICES.md).
The consolidated regression checklist is in [`docs/VALIDATION_CHECKLIST.md`](docs/VALIDATION_CHECKLIST.md).

## Build and flash

From the project directory, activate the matching ESP-IDF environment:

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
```

Build the firmware:

```bash
idf.py build
```

Flash a connected board. Replace the port if macOS assigns a different device:

```bash
idf.py -p /dev/cu.usbmodem101 flash
```

Open the serial monitor at the configured 2 Mbps console rate:

```bash
idf.py -p /dev/cu.usbmodem101 monitor -b 2000000
```

Press `Ctrl+]` to quit the monitor. A clean build is only needed after changing
configuration, dependencies, generated files, or when troubleshooting stale
build results:

```bash
idf.py fullclean
idf.py build
```

## Project layout

```text
main/                  Application entry point
components/crystal_hal/ Hardware abstraction interfaces and device adapters
components/hello_app/   Hello World launcher application and icon
components/perf_spike/  Phase 1 temporary performance benchmark
docs/                   Design, implementation, and bring-up notes
partitions.csv          Dual-OTA partition table with storage partition
sdkconfig.defaults     Project configuration defaults
```

The performance spike is retained as diagnostic code, but it is not installed
by the production startup path. The display currently uses one RGB buffer:
`CONFIG_BSP_LCD_RGB_BUFFER_NUMS=1`.

## License and attribution

See [`NOTICE`](NOTICE) for project attribution requirements and third-party
credits.
