# Handoff — Phase 9.5 Weather, 2026-09-06

Crystal OS (ESP32-S3, esp-brookesia-based) at `/Users/szemy/Workspace/ESP32 Crystal OS`,
branch `main`. Phase 9.5 Weather is implemented and closed in the plan; this
document remains as the implementation record and hardware validation checklist.

## Read first, in this order

1. `docs/HANDOFF.md` — carries standing statements from the owner that no session
   may re-decide: the 50% finger-tracked crossover is **required**, not cancelled,
   and only four Android lifecycle states are dispatched. **Ignore its "does not
   compile" section** — that was 2026-09-04 and is resolved.
2. `docs/DESIGN.md` §Weather — the design authority.
3. `docs/CODE_GUIDE.md` §"Phase 9.5 — Weather" (line ~1153) — written in the
   previous session. It is the spec for the work that remains.

## What already works

The network path is done and validated.

- `crystal_core.cpp:71` `weather_fetch()` performs a keyless Open-Meteo call on
  the `crystal_service` task, parses four fields with `strstr`/`sscanf` into a
  1 KiB static buffer, and posts `UI_EVT_WEATHER` through the UI queue.
- `crystal_shell.cpp:1552` `crystal_shell_weather_event()` looks the app up in
  the registry and calls `WeatherApp::update()`.
- Registered in `main/main.cpp` as `{"weather", make_weather_app, true, 3}`.
- `components/weather_app/` is tracked in the Phase 9.5 implementation commits.

## Implementation status

The former placeholder work is complete: the app has a procedural condition
glyph, resolved location display, typed cache fields, age and staleness status,
dynamic app-area geometry, lifecycle-safe asynchronous updates, and an explicit
warning while the system clock is not synchronized. WiFi enable state is also
persisted across reboot.

## Decisions already made — do not reopen

- **Open-Meteo, never weather.com or any keyed provider.** A key compiled into
  firmware is extractable with `esptool read_flash`, so a keyed API means either
  treating the key as public or running a proxy this project does not have.
- **IP geolocation is the v1 default for location**, with a three-tier resolver:
  manual lat/lon (Phase 11) → cached IP result in NVS → compiled HK default.
  Resolve once on the first `CRYSTAL_NETWORK_CONNECTED` after boot, not per
  fetch. `DESIGN.md` still calls IP geolocation a "later convenience" — that line
  predates Settings slipping behind Weather and is superseded by `CODE_GUIDE.md`.
- **The resolved location is displayed** in the layout (bottom-right, per the
  design mock). With auto-detection it is the only way a user can tell the guess
  was wrong.
- **Icons are procedural**, following `components/clock_app/src/clock_icon.c`.
  `assets/` is empty and there is no SPIFFS image pipeline yet; compiling
  PNG-converted C arrays costs ~906KB each and is forbidden by `CODE_GUIDE.md`.

## Four defects in the placeholder, all found by reading

In `components/weather_app/src/weather_app.cpp`:

- **Width mismatch.** `refresh()` reads `temp_c10` into an `int32_t` but
  `update()` wrote an `int16_t`. `nvs_get_blob` accepts the wider buffer and
  reports 2 bytes, so positive temperatures survive on little-endian by luck and
  negatives read as large positives. Same mismatch on `humid`, `wmo`, `wind`.
- **Dangling label on the failure path.** `update()`'s failure branch calls
  `lv_label_set_text(updated_, ...)` before checking lifecycle state.
  Destroy-on-switch means the 30-minute service refresh routinely lands while the
  app is `Destroyed` and that pointer dangles. `onDestroy()` also never nulls the
  label members.
- **Hardcoded geometry.** `onCreate()` sets `lv_obj_set_size(root_, 480, 440)`.
  Use `getVisualArea()`, as `clock_app.cpp:55` does.
- **Wrong condition ranges.** `condition()` treats code 3 as partly cloudy when
  it is overcast, and `code <= 48` swallows 4-44 into "Fog".

## Closed open question

`DESIGN.md` asks whether the board has an I2C temp/humidity part for indoor
readings. **It does not.** The BSP header declares only QMA7981 (IMU) and OV2640
(camera) on I2C, plus the RTC and IO expander. Indoor-plus-outdoor is dead for v1.

`DESIGN.md` still carries this as open, and still carries the superseded location
line above. Both need updating; they were left untouched because the owner asked
only for the code guide.

## Hardware validation remaining

The code has not been built or flashed in this workspace. Run the exit criteria
in `docs/PHASE_9_5_WEATHER_APP.md` on hardware, including WiFi-off timeout,
negative temperatures, WMO code 80, app switching, and the unsynchronized-clock
warning. `idf.py` is expected with
`IDF_PATH=/Users/szemy/.espressif/v6.1/esp-idf`.

## Conventions that bite here

- No `lv_*` call outside the LVGL task — post to the UI queue.
- Apps never read absolute screen coordinates.
- Apps never touch NVS directly; use `state()`.
- Nothing large goes in an `esp_event` handler frame — that task has a 2304-byte
  stack.
