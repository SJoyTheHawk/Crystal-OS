# Host simulator and HAL backend

`screenshots/` renders the production P4 UI with headless LVGL at 720 × 720.
From the repository root, run `python3 tools/capture_readme.py` to regenerate
the README's launcher, Quick Settings, Clock, Weather, Calculator, and Ethernet
images. It uses the current P4 stylesheet and deterministic service fixtures;
it does not run a device, establish real network connections, or provide an
interactive SDL window. See [capture instructions](../tools/README.md#readme-screenshots)
and [simulator scope](../docs/SIMULATOR.md).

`crystal_hal_mock.cpp` provides deterministic host implementations for the
Phase 2 interfaces. Compile it with the project's `components/crystal_hal/include`
directory in an SDL or unit-test target; it is intentionally not included
in the ESP-IDF firmware component.
