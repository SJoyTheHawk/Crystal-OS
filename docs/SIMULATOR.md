# Phase 2 Desktop Simulation

## What exists today

Phase 2 currently provides a standalone mock implementation of the Crystal HAL
in [`sim/crystal_hal_mock.cpp`](../sim/crystal_hal_mock.cpp). It lets host-side
tests exercise brightness, RTC, Wi-Fi, storage, and touch interfaces without
ESP32 hardware or ESP-IDF.

This is not yet a full desktop Crystal OS application. The Brookesia launcher,
LVGL display, and SDL input path still run on the board. The mock backend is the
foundation for that future simulator and for fast unit tests of Crystal-owned
logic.

## Requirements

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
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
```

The firmware backend uses the real display brightness adapter, NVS storage,
Wi-Fi driver, PCF85063 I2C RTC, and GT911-backed input device. The two backends
share only the interfaces in `components/crystal_hal/include`.

## Current limitations

- No SDL window or desktop rendering target is wired yet.
- The mock does not reproduce PSRAM limits, panel bandwidth, tearing, or touch
  timing.
- The PCF85063 and GT911 hardware paths can only be validated on the board.

These limitations are expected until the full Phase 2 simulator exit criterion
is implemented.
