# Crystal OS

[![License: PolyForm Noncommercial 1.0.0](https://img.shields.io/badge/license-PolyForm%20Noncommercial%201.0.0-blue.svg)](LICENSE.md)

Crystal OS is an ESP32-S3 touch-device shell and app framework built on
LVGL and Espressif's `esp-brookesia` phone UI.

## Project goal

Crystal OS standardizes touch-screen apps on ESP32 with a shared shell,
hardware interfaces, lifecycle, persistence, and system services. Early phases
verify that the MCU can run a modern, animation-rich interface. Apps currently
ship in firmware; later phases will add packages and a PC loader.

## Why ESP32?

ESP32 offers fast boot, low power, low cost, direct hardware access, and
built-in wireless connectivity in a small, predictable system.

Compared with Android, Crystal OS is smaller and more deterministic, but has
far fewer apps and drivers. Compared with Raspberry Pi, it uses less power and
has fewer moving parts, but cannot match Linux's CPU, memory, storage, or
software ecosystem. Android or Raspberry Pi is preferable for browsers,
heavy multimedia, Linux tools, or general-purpose apps.

## Design principles

- **Cards are home:** the device opens a useful data card, not an empty icon grid.
- **Appliance first:** fixed card order and simple gestures are preferred over
  phone-style navigation.
- **One live app:** the active app owns the screen; neighbouring cards use small
  snapshots to keep memory bounded.
- **Services own state:** timers, time, network, and storage survive app
  recreation, while apps remain lightweight views.

## Target hardware

- Waveshare ESP32-S3-Touch-LCD-4B
- ESP32-S3 with 16 MB flash and octal PSRAM
- 480x480 RGB LCD
- GT911 touch controller over I2C
- PCF85063 RTC over I2C
- AXP2101 power management over I2C
- ESP-IDF 6.1
- LVGL 8.4.0
- `esp-brookesia` 0.4.2

Board wiring and pin assignments are maintained in the
[Waveshare ESP32-S3-Touch-LCD-4B schematic diagram](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B#Schematic_Diagram).

## Current features

- **Card shell:** horizontal swipe navigation with 50% visual crossover and early icon cards
- **Gesture arbiter:** unified edge-gesture ownership for app switching and system overlays
- **Status bar:** time, Wi-Fi, battery, and page indicators
- **Quick Settings:** brightness, volume, Wi-Fi, and energy-saving toggle
- **Settings overlay:** Wi-Fi networks, static IP, timezone, manual time/location, timer alerts, device info
- **Keyboard overlay:** iOS-inspired shell keyboard with field lifting and multi-layer lifecycle
- **Apps:** Clock (with timer/stopwatch), Weather (Open-Meteo), Calculator, Hello World, State Test
- **Core services:** RTC persistence, Wi-Fi/SNTP sync, power management with dim/off timeouts
- **Hardware abstraction:** unified interface for brightness, storage, Wi-Fi, RTC, power, and touch

## Phase status

`[x]` means the phase exit criteria are complete; `[ ]` means work or
validation remains. Details and evidence live in
[`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) and
[`docs/VALIDATION_CHECKLIST.md`](docs/VALIDATION_CHECKLIST.md).

- [x] Phase 0 — skeleton and boot baseline
- [x] Phase 1 — animation performance gate
- [x] Phase 2 — hardware abstraction layer and simulator
- [x] Phase 3 — core services (RTC, Wi-Fi/SNTP, power states)
- [x] Phase 4 — app framework checkpoint
- [x] Phase 4.5 — lifecycle correctness validation
- [x] Phase 5 — persistent app registry and launcher order
- [x] Phase 5.5 — Clock, Timer, and Stopwatch hardware validation
- [x] Phase 6 — card shell and app switcher baseline
- [x] Phase 6.5 — measured crossover re-gate
- [x] Phase 7 — gesture arbiter and indicator bar
- [x] Phase 7.5 — 50% visual crossover and icon-only transition cards
- [x] Phase 8 — quick settings
- [x] Phase 8.5 — corner-anchored quick panel
- [x] Phase 9 — Wi-Fi product integration
- [x] Phase 9.5 — Weather app
- [x] Phase 9.6 — Calculator app port
- [x] Phase 10 — keyboard overlay
- [x] Phase 11 — Settings and power management
- [ ] Phase 12 — reliability and recovery
- [ ] Phase 13 — PC app catalog and package loader

Phase 11 closed on 2026-09-11 after all Settings and power validation rows passed
on the physical panel. This includes the five V3 defect regressions, the durable
project-local Brookesia override, and independent enabled/disabled timer-alert
policy checks.

## V2 track: installable apps

The V2 app platform is a design track, not yet built. It replaces the V1 rule
that apps must ship in firmware with a versioned host ABI and PC-assisted
installation.

- Apps live in `/apps/<reverse-dns-id>/` with a manifest, assets, and private data.
- A signed `.capp` package is verified and installed transactionally.
- Declarative, Lua, and curated native runtime tiers share one handle-based ABI.
- Permissions, memory limits, and per-app storage keep third-party code bounded.
- The launcher merges built-in and packaged apps; built-ins can be disabled and
  packaged apps can be uninstalled.

See [`docs/APP_PLATFORM.md`](docs/APP_PLATFORM.md) for the ABI, package format,
security boundaries, and the `examples/clock3p/` worked example.

### V2 phase status

V2 continues the main roadmap as Phases 14–17. The design is documented, but
implementation has not started.

- [ ] Phase 14 — package format and registry generalisation
- [ ] Phase 15 — PC/on-device control panel and transport
- [ ] Phase 16 — declarative runtime and frozen ABI v1
- [ ] Phase 17 — Lua runtime, sandbox, quotas, and watchdog

See [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) for phase exit criteria and decisions.
The current desktop mock workflow is documented in [`docs/SIMULATOR.md`](docs/SIMULATOR.md).
Phase 3 hardware checks are documented in [`docs/PHASE_3_SERVICES.md`](docs/PHASE_3_SERVICES.md).
The consolidated regression checklist is in [`docs/VALIDATION_CHECKLIST.md`](docs/VALIDATION_CHECKLIST.md).
The complete project-specific ESP-IDF command reference is in
[`docs/ESP_IDF_COMMAND_GUIDE.md`](docs/ESP_IDF_COMMAND_GUIDE.md).

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
main/                       Application entry point and shell initialization
components/crystal_hal/     Hardware abstraction interfaces and device adapters
components/crystal_app/     CrystalApp lifecycle and bounded per-app state
components/crystal_registry/ Persistent app enable and launcher slot registry
components/crystal_shell/   Card shell, gesture arbiter, and system overlays
components/crystal_core/    Core services (RTC, Wi-Fi/SNTP, power states, timers)
components/hello_app/       Hello World launcher application and icon
components/state_test_app/  Phase 4 state persistence validation app
components/clock_app/       Clock, timer, and stopwatch app (Phase 5.5)
components/weather_app/     Open-Meteo weather app (Phase 9.5)
components/calculator_app/  Calculator app port (Phase 9.6)
components/perf_spike/      Phase 1 temporary performance benchmark
docs/                       Design, implementation, and bring-up notes
partitions.csv              Dual-OTA partition table with storage partition
sdkconfig.defaults          Project configuration defaults
```

The performance spike is retained as diagnostic code, but it is not installed
by the production startup path. The display currently uses two RGB buffers:
`CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2` in avoid-tear direct mode.

## License and attribution

Crystal OS is licensed under the
[PolyForm Noncommercial License 1.0.0](LICENSE.md)
(`PolyForm-Noncommercial-1.0.0`). Copyright (c) 2026 is jointly held by
[Rocky Road Studio](https://rockyroad.studio) and
[Bubstal Limited](https://bubstal.com). The project is authored by
Johnny "SJoyTheHawk" Sze. Commercial licensing enquiries may be sent to
[rockyroadstudio@outlook.com](mailto:rockyroadstudio@outlook.com).

This license applies only to original Crystal OS software, documentation, and
artwork. Third-party dependencies and reference materials retain their own
licenses. See [`NOTICE`](NOTICE) for third-party credits; the exact dependency
license files under `managed_components/` remain authoritative for
redistributed components.
