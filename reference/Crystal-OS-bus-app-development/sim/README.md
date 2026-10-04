# Desktop HAL backend

`crystal_hal_mock.cpp` provides deterministic host implementations for the
Phase 2 interfaces. Compile it with the project's `components/crystal_hal/include`
directory in a future SDL or unit-test target; it is intentionally not included
in the ESP-IDF firmware component.
