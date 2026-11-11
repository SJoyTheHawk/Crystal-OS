# Declarative dashboard

This package demonstrates the Phase 16 JSON runtime. It builds a dark Crystal
surface, a three-tab view, live time/network/timer text, conditional offline
visibility, and timer actions. The app uses only bundled fonts and LVGL
primitives; its source JSON is ordinary package content.

Package and inspect it with:

```sh
python3 tools/crystal pack examples/declarative_dashboard --unsigned -o /tmp/declarative-dashboard.capp
python3 tools/crystal inspect /tmp/declarative-dashboard.capp
```

Installing an unsigned package requires Developer Mode. The timer buttons
require `system_timer: true`; the runtime rejects timer actions when that
permission is absent. This example is not included in the factory app image.

The declarative runtime currently supports containers, rows/columns, scroll
views, labels, buttons, button matrices, switches, sliders, arcs, tab views,
package-bundled `CR16` RGB images, static lists and chart series, text areas,
spinners, and dialogs. `CR16` images are UTF-8 text containing `CR16 width
height` followed by exactly `width * height` hexadecimal `RRGGBB` pixels. Each
image source is limited to 256 KiB, each dimension to 480 pixels, and decoded
images to 128 KiB per app. Bindings support text and visibility for time,
network connection, and timer state. Actions are allowlisted timer operations;
arbitrary scripts are not evaluated.

The `visual/` directory contains declarative screen documents matching the
Clock, Weather, and Calculator layouts. These are visual references; the
existing bundled apps retain stopwatch, calculation, and weather-fetch behavior.
