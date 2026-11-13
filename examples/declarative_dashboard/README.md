# Declarative dashboard

This package demonstrates the Phase 16 JSON runtime. It builds a dark Crystal
surface, a three-tab view, live time/network/timer text, conditional offline
visibility, and timer actions. The app uses only bundled fonts and LVGL
primitives; its source JSON is ordinary package content.

The dashboard fills the 480×440 app area below the system status bar. The three
tabs stay on one line, Timer has four 44-pixel-tall buttons in two rows, and
Controls scrolls vertically to reveal the remaining widgets. The app background
and tabs use the system's dark surfaces and teal accent.

Package and inspect it with:

```sh
python3 tools/crystal pack examples/declarative_dashboard --unsigned -o /tmp/declarative-dashboard.capp
python3 tools/crystal inspect /tmp/declarative-dashboard.capp
```

Installing an unsigned package requires Developer Mode. The timer buttons
require `system_timer: true`; the runtime rejects timer actions when that
permission is absent. This example is not included in the factory app image.

After changing the runtime, flash the application with `idf.py app-flash`.
After changing this package's JSON, repack it, uninstall the previous Status
Dashboard from Settings → Manage Apps, and install the new package. A firmware
app-only flash preserves the installed `.capp`, so it does not update its JSON.
Close the serial monitor before a USB install and reopen it after the command
finishes.

The declarative runtime currently supports containers, rows/columns, scroll
views, labels, buttons, button matrices, switches, sliders, arcs, tab views,
package-bundled `CR16` RGB images, dynamic rich lists and chart series, text areas,
spinners, and dialogs. `CR16` images are UTF-8 text containing `CR16 width
height` followed by exactly `width * height` hexadecimal `RRGGBB` pixels. Each
image source is limited to 256 KiB, each dimension to 480 pixels, and decoded
images to 128 KiB per app. Bindings support text and visibility for time,
network connection, and timer state. Actions are allowlisted timer operations and structured UI updates;
arbitrary scripts are not evaluated. Controls includes Focus input (the system
keyboard), Add list item, Update chart and Show dialog. The dialog dismisses
after a button selection; reopening the app rebuilds its initial tree.
The shared callable contract is documented in
[the implementation record](../../docs/APP_PLATFORM_V2_PHASE_15_16_IMPLEMENTATION.md).

The `visual/` directory contains declarative screen documents matching the
Clock, Weather, and Calculator layouts. These are visual references; the
existing bundled apps retain stopwatch, calculation, and weather-fetch behavior.
