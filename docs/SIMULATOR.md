# Host simulation and P4 screenshots

## What exists today

The repository provides a standalone mock implementation of the Crystal HAL
in [`sim/crystal_hal_mock.cpp`](../sim/crystal_hal_mock.cpp). It lets host-side
tests exercise brightness, RTC, Wi-Fi, storage, and touch interfaces without
ESP32 hardware or ESP-IDF.

The headless screenshot harness also renders the production Brookesia launcher,
Crystal Quick Settings, Clock, Weather, Calculator, and Ethernet diagnostics
using LVGL and the current **720 × 720 P4 stylesheet**. It uses deterministic
service fixtures and captures display flush output, including system layers.
This is not yet a full interactive desktop Crystal OS application: there is no
SDL window or interactive input loop.

## Generate the README gallery

On macOS, with CMake, a C++17 compiler, and the checked-out managed LVGL
dependency, run from the repository root:

```bash
python3 tools/capture_readme.py
# Or keep a preview separate from the documentation images:
python3 tools/capture_readme.py --output-dir /tmp/crystal-p4-preview
```

The default output is `docs/images/`. The harness selects
`CONFIG_IDF_TARGET_ESP32P4=1`, calls the production stylesheet factory, and
uses selected bundled apps with fixed time, weather, battery, and network data.
Ethernet uses example addresses; neither Ethernet nor Wi-Fi accesses a real
network. These images document UI appearance, not hardware performance.
See [capture details](../tools/README.md#readme-screenshots) and
[image provenance](images/README.md).

## Mock HAL requirements

- macOS or another Unix-like host
- A C++17 compiler (`clang++` or `g++`)
- The repository checkout

No ESP-IDF environment is required for the mock HAL itself.

## Compile-check the mock backend

From the repository root:

```bash
clang++ -std=c++17 \
  -Icomponents/crystal_hal/include \
  -fsyntax-only sim/crystal_hal_mock.cpp
```

A successful command produces no output. This checks that the host backend and
the shared HAL contracts compile independently of ESP-IDF.

## Use it from a host test

Compile the mock source together with a test or application that includes
`crystal_hal.hpp`:

```bash
clang++ -std=c++17 \
  -Icomponents/crystal_hal/include \
  your_test.cpp sim/crystal_hal_mock.cpp \
  -o /tmp/crystal_hal_test
/tmp/crystal_hal_test
```

The mock exposes the same global entry points as firmware:

```cpp
#include "crystal_hal.hpp"

int main() {
    crystal_hal_init();
    hal().brightness->set(60);
    hal().wifi->start();
    hal().wifi->connect("test-network", "test-password");
    return hal().wifi->connected() ? 0 : 1;
}
```

The mock Wi-Fi reports connected after `start()` and `connect()`. Mock storage
keeps values in memory, the RTC reads the host clock, and touch returns an
empty point until a test-specific mock extension changes it.

## Firmware comparison

The device backend is selected automatically by the ESP-IDF component build:

```bash
source /path/to/esp-idf/export.sh
idf.py build
```

This assumes the P4 target and signing key are already configured; use the
[root README](../README.md#build-and-flash) for fresh-checkout setup.
The firmware backend uses the P4 display/brightness adapter, NVS storage,
ESP32-C6 hosted Wi-Fi, IP101 Ethernet, RTC, and GT911-backed input device.
The mock implements shared HAL contracts, while capture-specific service
fixtures supply deterministic readings without linking hardware drivers.

## Current limitations

- No interactive SDL window or complete desktop OS startup is wired yet.
- The mock does not reproduce PSRAM limits, panel bandwidth, tearing, or touch
  timing.
- The PCF85063 and GT911 hardware paths can only be validated on the board.

Physical display, networking, and performance acceptance are tracked separately
in the [P4 validation records](README.md#current-work).
