# HK Bus app integration evaluation

Date: 2026-09-11

> **Superseded in part by [`PHASE_9_7_TRANSIT_PROPOSAL.md`](PHASE_9_7_TRANSIT_PROPOSAL.md).**
> That document is the buildable scope; where the two disagree, it wins. Three
> things changed after measuring the live endpoints:
> the app is filed as Phase 9.7, not Phase 13; the device holds a ~6 KB index of
> distinct route *names* (1,048 of them) rather than no index at all, which buys
> live keypad filtering; and the route table is refreshed by `ETag`/`If-None-Match`
> — verified `304`, 0 bytes — so the 349 KB figure below is a worst case that a
> current device never pays. The size analysis and language/font work here stand.

## Decision summary

Port the app as a built-in C++/LVGL component for the Phase 13 build. Do not
attempt to embed the web page: Crystal OS has no browser, DOM, CSS, or JavaScript
runtime. A Lua package is a viable later distribution format, but only after the
Phase 16 host ABI and Phase 17 Lua runtime exist. The present Phase 13 catalog
can only enable, disable, reorder, and clear data for code already compiled into
the firmware.

Skipping Phase 12 does not technically block a built-in bus app. It does remove
signed OTA, rollback, and crash recovery, so every update remains a USB firmware
flash and a bad networking build has no automatic rollback.

## Retrieved source

The deployed application is one readable, unminified HTML document containing
all markup, CSS, translations, and JavaScript:

| Item | Measured size |
| --- | ---: |
| Complete deployed `index.html` | 523,197 B |
| Two embedded WebP theme banners | 329,708 B decoded |
| HTML/CSS/JavaScript excluding base64 image text | 83,581 B |
| Gzip-compressed document | 355,736 B |
| Five-language `I18N` object | 9,587 B |
| English-only translation block | 1,758 B |

Selecting one language saves about 7.8 KB in the original web source, or only a
few KB once ported and compiled. Font coverage, not translated strings, is the
important language-dependent cost.

The two banner images account for most of the source-size increase. They are
decorative dark/light theme variants, not required for bus lookup. Keeping them
would consume at least their 322 KB compressed payload in SPIFFS plus decoder or
conversion costs. The recommended Crystal port follows the existing quiet app
style and omits them; a pixel-art or procedural header would be effectively
free. Add 0.3-0.8 MB of asset storage if equivalent raster themes are required.

No public repository, source map, build metadata, or license was discoverable.
The exact served file is preserved at `reference/gorhkbus/index.html` with
SHA-256 `0397e88704d8cf31affa0584b8d5430be04db27d063668474b3afe26627cafed`.

## Functional port scope

The current app provides route keypad search, KMB/LWB route indexing, live
Citybus lookup, direction and stop selection, nearest-stop calculation, ETA
countdowns, saved stops, nearby stops, manual refresh, and map launch. The web
page uses browser geolocation and `localStorage`; the Crystal port must replace
those with a location service and `CrystalState`. External map launch has no
current Crystal OS equivalent and should either be omitted or represented as
coordinates/QR content.

The existing weather stack already links TLS, `esp_http_client`, lwIP, and the
certificate bundle. A bus app reuses those facilities, so their roughly 100+ KB
of linked code is not an incremental app cost. The bus service still needs a
bounded streaming JSON parser and a Core 0 request worker; the current weather
helper has a small fixed response buffer and cannot consume the route list.

The production KMB route response measured 348,938 bytes uncompressed and about
38 KB over gzip. The web app parses and retains the whole response. An ESP32 port
must stream it into compact route records or cache a compact generated index;
building an equivalent Lua table would consume several hundred KB to over 1 MB.

## Size estimates

Current baseline: `crystal_os.bin` is 2,806,304 B. Each OTA slot is 5 MiB, so
2,436,576 B (46%) remains. The board has 8 MB PSRAM; display initialization has
already been measured at about 922 KB PSRAM.

All figures below are incremental to the current firmware and exclude font
packages unless stated. They are engineering ranges until a real port is linked
and measured with `idf.py size-components`.

| Option | Incremental flash/storage | Live RAM/PSRAM | Availability | Assessment |
| --- | ---: | ---: | --- | --- |
| Built-in C++/LVGL component | 30-70 KB | 100-350 KB | Phase 13 | Recommended now |
| Built-in C++ with full raw route DOM | 30-70 KB | 600 KB-1.5 MB | Phase 13 | Avoid; wasteful and fragmentation-prone |
| Lua app with shared native bus service | 170-260 KB first app; 20-45 KB later package | 160-400 KB/app | Phase 17 | Best later installable form |
| Lua app parsing raw route JSON itself | 170-260 KB first app; 20-45 KB package | 700 KB-1.8 MB/app | Phase 17 | Technically possible, poor design |
| Declarative package | 20-40 KB runtime/package overhead | 80-250 KB | Phase 16 | Insufficient for search, geo ranking, caching, and ETA orchestration |
| Curated PIC native package | 45-100 KB including first loader | 100-350 KB | Unscheduled spike | Little isolation; no benefit for Phase 13 |
| Browser/web wrapper | At least 1-3+ MB code, likely more | Multiple MB | Not supported | Reject on ESP32-S3 |

The native estimate is intentionally above the current linked Weather app:
Weather contributes 6,575 B of flash plus an 8 KB procedural icon buffer, while
HK Bus has substantially more UI states, record handling, caching, and JSON
logic. Current Calculator and Clock app logic each contribute about 4 KB flash
plus an 8 KB icon buffer. The recommended bus icon should also be procedural;
its 64x64 RGB565 buffer costs 8 KB BSS and little flash.

For Lua, 150-200 KB flash is the project's existing unmeasured Lua 5.4 runtime
estimate. The upper range includes ABI bindings and a bus-specific streaming
service. The Lua file itself is small; interpreter and object memory dominate.

## One-language variants

| Selected language | Additional font storage | Notes |
| --- | ---: | --- |
| English | 0 | Reuses the five compiled Montserrat sizes |
| French | 0-10 KB | Latin coverage is already present; verify accents actually linked |
| Japanese | 40-180 KB | Fixed Japanese UI subset only; route/stop names remain English as in the web app |
| Korean | 40-180 KB | Fixed Korean UI subset only; route/stop names remain English as in the web app |
| Traditional Chinese | 0.6-2.5 MB | Must cover dynamic HK route/stop names at multiple sizes; measure a generated corpus subset |

The bundled LVGL SimSun CJK font illustrates the scale: its binary `.fnt` is
165,340 B at one 16 px size, while its source array is 1,085,203 B. It is not
enough evidence that every Hong Kong stop character is covered. Traditional
Chinese should therefore be a separately generated asset package based on the
actual API corpus, not a compiled C array and not part of every firmware image.

The practical product split is one firmware/package variant per chosen language:

- English and French use existing fonts and differ mainly in small string tables.
- Japanese and Korean ship only a fixed UI glyph subset while API place names
  stay English, matching the source app.
- Traditional Chinese ships a measured HK transport glyph corpus in SPIFFS (or
  the future app partition). It is the only variant with a material storage cost.

## Recommended Phase 13 boundary

1. Add `bus_app` as a built-in `CrystalApp`, registered beside Weather and
   Calculator, with English as the first measured build.
2. Add an OS-owned asynchronous bus data service on Core 0. Stream JSON into
   compact records, cap concurrent stop requests, and post bounded UI events.
3. Store favorites and the last selection through `CrystalState`; add the Phase
   13 `clear()` support before exposing Clear Data.
4. Use the Settings location coordinates. Do not add a second IP-geolocation
   path or expose raw Wi-Fi/location internals to app code.
5. Keep language as a build/package choice, not an in-app selector. Record the
   selected language in app metadata so the catalog can show which variant is
   installed.
6. Measure the linked component, peak PSRAM during route loading, TLS request
   peak, first-frame time, and teardown recovery before setting a final budget.

Suggested hard gates for the native English port are no more than 100 KB added
to `crystal_os.bin`, no more than 400 KB peak app-owned PSRAM, no UI-thread
network calls, and full app-owned heap recovery after `onDestroy()`.
