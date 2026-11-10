# Phase 14 Calculator proof package

This package selects the curated Calculator implementation already compiled into
Crystal OS. It uses the system Calculator layout and icon, and a separate
`org.crystal.calculator` state namespace. It is a proof fixture; it does not
replace the bundled Calculator or load a native binary from the package.

From the repository root:

```sh
./tools/crystal pack examples/calculator_package --unsigned -o /tmp/calculator.capp
./tools/crystal inspect /tmp/calculator.capp
python3 tools/test_app_platform_v2.py
```

`app.yaml` is written in JSON syntax (valid YAML) so the example packs without
external Python packages. General YAML needs PyYAML. Unsigned installation
requires explicit Developer Mode. USB/Wi-Fi installation is Phase 15.

The integration suite exercises this package with the real Calculator,
Brookesia, and LVGL. Device validation is tracked in
[the Phase 14 implementation notes](../../docs/PHASE_14_IMPLEMENTATION.md).

To run the automated board check after migrating to the Phase 14 partition
layout, use `idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=ON build app-flash monitor`.
The build embeds a separate **Calculator Test** fixture under
`org.crystal.phase14.calculator`. It performs 10 cycles, automatically reboots
once, verifies restoration, and removes its package. Leave the display untouched
until the final `PASS: Phase 14 device test complete` message. Disable the test
afterward with `idf.py -DCRYSTAL_PHASE14_DEVICE_TEST=OFF build app-flash monitor`.
