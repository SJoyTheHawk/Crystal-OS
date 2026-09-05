# Phase 9.5 — Weather app: rendering the reading

Companion to `CODE_GUIDE.md` §"Phase 9.5 — Weather", which covers the provider,
location resolution, and the fetch path. This document is only about the second
half: getting a reading that already arrived onto the screen.

The fetch works. As of 2026-09-06 the service logs, on hardware:

```
I (8881) crystal_core: weather fetched: 23.9 C, humidity 96%, code 80, wind 2.5 km/h
```

And the app still shows "No weather data" / "Connect to WiFi to refresh". Nothing
is wrong with the layout code. The break is in the cache between them.

## The blocker: seven characters, and not one more

**A `CrystalState` key longer than 7 characters is silently discarded.**

```cpp
// WRONG — "temp_c10" is 8 characters. make_key() rejects it, so set() returns
// false and get() returns false. Both results are cast to void, so nothing is
// logged. The temperature is never stored, `have` is false forever, and a
// perfectly good reading is invisible.
(void)state().set("temp_c10", &reading.temperature_c10, sizeof(reading.temperature_c10));
```

The budget is arithmetic, not style. `NVS_KEY_NAME_MAX_SIZE` is 16 *including* the
terminator, so 15 usable. `CrystalState`'s sandbox prefix is `"a" + 6 hex + "."`,
which is 8. That leaves exactly 7 — which is why `crystal_app.cpp` sets
`kMaxKeyBytes = 7`.

```text
a3f9c21.tc10
^^^^^^^^ 8 chars of prefix, not yours to spend
        ^^^^ your budget: 7 characters
```

Audit of the keys in use:

| Key | Length | Verdict |
|---|---|---|
| `temp_c10` | 8 | **rejected — this is the bug** |
| `fetched` | 7 | at the limit, fine |
| `humid` | 5 | fine |
| `wind` | 4 | fine |
| `city` | 4 | fine |
| `wmo` | 3 | fine |

Rename `temp_c10` to `tc10` in both `update()` and `refresh()` and the existing
code starts working with no other change.

## Never `(void)` a state write

This is the reason a two-character overage cost a debugging session. `set()`
reports failure, and that report is the only warning you get — key too long, NVS
full, storage absent:

```cpp
if (!state().set("tc10", &reading.temperature_c10, sizeof(reading.temperature_c10))) {
    ESP_LOGW(TAG, "weather cache write failed");
}
```

The general rule: **when the service says it fetched and the UI says it has
nothing, suspect the cache between them before touching layout code.**

## One formatter, two callers

Per `CODE_GUIDE.md` §9.1, a lazily built widget reads state rather than trusting
that it saw the event. `refresh()` is that reader, and it must be the *only* place
that turns stored values into text. Both `onCreate()` (app opened, data may be
hours old) and `update()` (reading just landed) call it.

Do not let `update()` write labels directly. The moment it does, build-time and
event-time formatting can disagree, and they will:

```cpp
// WRONG — a second formatter. Now there are two places that decide how a
// temperature looks, and only one of them handles the negative case.
void WeatherApp::update(const CrystalWeatherReading &reading)
{
    snprintf(text, sizeof(text), "%d C", reading.temperature_c10 / 10);
    lv_label_set_text(condition_, text);
}
```

`update()` persists, then delegates:

```cpp
void WeatherApp::update(const CrystalWeatherReading &reading)
{
    pending_ = false;
    if (reading.success) write_cache(reading);   // safe in any lifecycle state
    if (!is_live()) return;                     // see "a reading can outlive the tree"
    reading.success ? refresh() : show_stale_failure();
}
```

## Read into the type that was written

`CrystalState` is a blob store over `nvs_get_blob`, which reads a 2-byte blob into
a 4-byte buffer, reports `length == 2`, and returns success. The upper bytes keep
whatever the stack held:

```cpp
// WRONG — update() wrote int16_t, refresh() reads int32_t. Positive temperatures
// survive on little-endian by luck; -5.0 C reads as a large positive number.
int32_t temp = 0;
size_t n = sizeof(temp);
const bool have = state().get("tc10", &temp, &n);
```

Match the width and check the length that comes back:

```cpp
int16_t temp = 0;
size_t n = sizeof(temp);
const bool have = state().get("tc10", &temp, &n) && n == sizeof(temp);
```

`humid` and `wmo` are `uint8_t`, `wind` is `uint16_t`, `fetched` is `int32_t`.
A per-field typed helper is less error-prone than repeating `size_t n = ...`.

## Scaled integers, formatted at the edge

There are no floats in NVS. Temperature is stored as tenths, so formatting has to
split it — and the naive split is wrong for negatives:

```cpp
// WRONG at -0.4 C: temp/10 is 0 and -temp%10 is 4, printing "0.4" — sign lost.
snprintf(text, sizeof(text), "%ld.%ld", temp / 10, temp < 0 ? -temp % 10 : temp % 10);
```

Take the sign out first, then format the magnitude:

```cpp
const char *sign = temp < 0 ? "-" : "";
const int mag = temp < 0 ? -temp : temp;
snprintf(text, sizeof(text), "%s%d.%d°C", sign, mag / 10, mag % 10);
```

`°` **is** available — checked, not assumed. LVGL's stock Montserrat faces carry a
second compressed range starting at U+00B0, so the glyph is present in 14, 16, 20,
28 and 48 (a real 10×10 box in Montserrat 28, not a placeholder). The §9.1 rule
still holds for anything outside that range; it just does not bite here.

The layout puts the degree mark in its own small label rather than in the
temperature string, so it can be aligned to the number's cap height. A 48 px `°`
sitting on the baseline of a 48 px numeral looks misplaced.

## The glyph is shapes, not a bitmap

The first implementation plotted pixels into a static `lv_color_t[64*64]` and
pointed one `lv_img_dsc_t` at it. It worked and it looked hand-sketched, for three
reasons worth recording because each is a general trap:

- **It carried its own opaque background.** A glyph that fills its own rectangle
  reads as a sticker pasted onto the card, not artwork on it.
- **Per-pixel circle tests do not anti-alias.** `(x-cx)² + (y-cy)² < r²` gives a
  hard staircase edge. At 64 px on a 480 px panel that staircase is clearly
  visible, and it is what made the result look like a doodle.
- **One static buffer means one glyph per process.** Two `lv_img` objects sourced
  from the same descriptor cannot show different conditions; the second
  `prepare()` overwrites what the first drew.

`weather_glyph.c` builds the glyph from rounded rectangles and circles instead —
LVGL anti-aliases their edges for free, the background stays transparent, and each
`weather_glyph_create()` returns an independent object. It also costs no static
RAM, where the bitmap cost 8 KiB of internal DRAM.

Shapes are authored against a 96 px box and scaled through one macro, so the
proportions hold at any size without a second set of constants:

```c
enum { AUTHORED = 96 };
#define S(v) ((lv_coord_t)((v) * size / AUTHORED))
```

Two details that are easy to get wrong:

**Store the size; do not read it back.** `lv_obj_get_width()` returns 0 until LVGL
has run a layout pass, and `weather_glyph_set_code()` is called from `onCreate()`
before one has happened. Reading the scale from the object there collapses every
dimension to zero and draws nothing. The size is kept in the parts struct.

**Free the parts table from `LV_EVENT_DELETE`.** The glyph can be destroyed by its
parent being destroyed, which the app never observes. Hanging the free off the
object's own delete event is the only way the allocation cannot outlive it.

## WMO codes: one table, not two ladders

`condition()` returns the label and `weather_icon_prepare()` picks the glyph, and
today each re-derives the grouping from raw code ranges. Two independent ladders
over the same input drift: it is already possible to get the "Overcast" label with
a partly-sunny icon, because one branches on `code == 3` and the other on
`code >= 1 && code <= 48`.

One table, both consumers reading it:

```cpp
struct WeatherGroup { uint8_t lo, hi; const char *label; WeatherGlyph glyph; };

static constexpr WeatherGroup kGroups[] = {
    {  0,  0, "Clear",        Glyph::Sun     },
    {  1,  2, "Partly cloudy",Glyph::SunCloud},
    {  3,  3, "Overcast",     Glyph::Cloud   },
    { 45, 48, "Fog",          Glyph::Fog     },
    { 51, 57, "Drizzle",      Glyph::Drizzle },
    { 61, 67, "Rain",         Glyph::Rain    },
    { 71, 77, "Snow",         Glyph::Snow    },
    { 80, 82, "Rain showers", Glyph::Rain    },
    { 85, 86, "Snow showers", Glyph::Snow    },
    { 95, 99, "Thunderstorm", Glyph::Storm   },
};
```

Codes 80-82 are the ones that catch people. They are *showers*, they are common —
the hardware log above is `code 80` — and a `code <= 77` ladder drops them into
the unknown bucket while a `code >= 80 && code <= 82` clause in the icon path
still draws rain. The label and the picture disagree, on the most frequent
non-clear condition.

Anything unmatched renders as "Unknown" with a neutral glyph. Do not map unknown
onto clear; a wrong confident answer is worse than an honest blank.

## Layout comes from the visual area

```cpp
lv_obj_set_size(root_, 480, 440);   // WRONG — 440 is a guessed status-bar height
```

`clock_app.cpp:55` is the pattern. The bar height is a stylesheet value, and a
hardcoded 440 goes stale silently when the stylesheet changes:

```cpp
const lv_area_t area = getVisualArea();
const lv_coord_t width  = area.x2 - area.x1 + 1;
const lv_coord_t height = area.y2 - area.y1 + 1;
```

There is a second half to this that cost a visible bug. `getVisualArea()` is
expressed in **display** coordinates, below the status bar, but the active app
screen is already app-area sized and its children use **local** coordinates — the
same distinction `crystal_shell.cpp:449` documents for snapshots. So:

```cpp
lv_obj_set_pos(root_, area.x1, area.y1);   // WRONG — offsets by the bar height
lv_obj_set_pos(root_, 0, 0);               // right
```

A full-height root pushed down by the bar height overhangs the bottom edge, the
screen becomes scrollable to reach the overhang, and the page looks like it has
"dropped down" and needs scrolling up. Clearing `LV_OBJ_FLAG_SCROLLABLE` on the
root does not help, because the scroll is on the parent screen.

Layout, as built:

```
┌──────────────────────────────────────────────┐
│ Taipei                                       │  montserrat_20
│ ┌──────────────────────────────────────────┐ │
│ │ 22.3 °C                            ( ☁ ) │ │  48 px number, 96 px glyph
│ │                                          │ │
│ │ Partly cloudy                            │ │  montserrat_28
│ └──────────────────────────────────────────┘ │
│ ┌────────────────────┐┌────────────────────┐ │
│ │ HUMIDITY           ││ WIND               │ │  montserrat_14 caption
│ │ 64%                ││ 12.4 km/h          │ │  montserrat_28 value
│ └────────────────────┘└────────────────────┘ │
│ Updated 4 min ago                            │
│                                              │
│              (reserved: forecast row)        │
└──────────────────────────────────────────────┘
```

Heights sum to 363 px of a 440 px area. Per `CODE_GUIDE.md` §8.5 ("Reserve space,
never fill it"), the ~77 px below the status line stays empty — it is where a
forecast row goes later. Do not put a filler tile in it now, and do not spread the
existing content out to consume it: centring five elements in 440 px is what
produced the "meaninglessly dropped down" spacing in the first attempt.

Two units per row, one fact each. `Humidity 64%   Wind 12.4 km/h` crammed into one
label is a data dump; two captioned tiles are scannable at arm's length.

## A reading can outlive the tree

Destroy-on-switch means the app instance survives but its LVGL objects do not. The
service refreshes every 30 minutes regardless of what is on screen, so `update()`
routinely runs with every label pointer dangling:

```cpp
// WRONG — touches a label before checking whether one exists.
if (!reading.success) { lv_label_set_text(updated_, "Unable to refresh weather"); return; }
```

Gate every draw on liveness. Persisting is always safe; drawing is not:

```cpp
bool WeatherApp::is_live() const
{
    const LifecycleState s = lifecycle_state();
    return s == LifecycleState::Created || s == LifecycleState::Started ||
           s == LifecycleState::Resumed;
}
```

And null the members in `onDestroy()`, or `refresh()`'s null guard is decoration:

```cpp
bool WeatherApp::onDestroy()
{
    root_ = condition_ = details_ = updated_ = location_ = nullptr;
    icon_ = nullptr;
    return true;
}
```

Brookesia frees the tree. Clearing the pointers is the app's job.

## The timer displays, it does not fetch

A 5 s `lv_timer` re-renders the age line and expires the request timeout, nothing
more. It must never trigger a network call — that is the service's throttled job,
and a timer that fetches turns an idle open app into a request loop. 5 s rather
than 60 s only because the timeout needs that resolution to feel prompt; the age
text changes far less often.

```cpp
bool WeatherApp::onPause()  { if (timer_) { lv_timer_del(timer_); timer_ = nullptr; } return true; }
```

`onCreate()` asks for a fetch only when the cache is older than 15 minutes.
`crystal_weather_request()` is a request, not a command — the service still owns
throttling and decides whether to honour it.

## States, and none of them is a spinner

| Condition | Glyph | Primary | Status line |
|---|---|---|---|
| Fresh cache | condition glyph | reading | "Updated just now" / "N min ago" |
| Stale cache, refresh out | condition glyph | **the reading, kept** | "Refreshing - updated N min ago" |
| Fetch failed, cache present | unchanged | unchanged | "Couldn't refresh - updated N min ago" |
| No cache, refresh out | neutral | `--` + "Checking weather" | "Fetching" |
| No cache, offline | neutral | `--` + "Connect to WiFi to refresh" | empty |

Three rules behind the table.

**A failed or in-flight refresh never clears a good reading.** Cache is painted
before any request is made, so a stored reading is on screen in the first frame and
the fetch quietly replaces it. The qualifier goes in the status line, never in
place of the data. This is the difference between "Refreshing - updated 8 min ago"
and a page of placeholder text.

**A pending request must be able to fail.** The service task only posted a result
when WiFi was up, so an offline request produced no event at all and the UI sat on
"Fetching" forever. Two changes: the service now answers offline requests with
`success = false`, and the app times its own request out after 25 s. Either alone
would leave a path where the UI waits on something that is never coming.

**The location label is not decoration.** With IP geolocation it is the only way a
user can tell the guess was wrong, so it renders even in the empty states.

That last point has teeth. A fallback that matches the expected answer hides the
failure it exists to report — the Hong Kong fallback was indistinguishable from a
successful resolve for a device in Hong Kong, which is exactly how the geolocation
failure stayed invisible. Show resolved city, or formatted coordinates, or
"Location not set" — never a default dressed as a result.

## Exit criteria

- A logged `weather fetched:` line is visible on screen within one refresh.
- Opening the app with a cache present shows the reading immediately, never
  placeholder text followed by data.
- Opening it with WiFi off ends at "Connect to WiFi to refresh" within 25 s, not on
  a permanent "Fetching".
- The page does not scroll: no content below the app area's bottom edge.
- Temperature correct at negative values and at `-0.4 C`.
- Label and icon agree for codes 0, 3, 45, 55, 65, **80**, 75, and 96.
- Location shows the resolved city, distinguishable from the compiled fallback.
- Switching away and back preserves the reading and its age.
- A service refresh while the app is closed does not crash and is visible on
  reopen.
- No `lv_*` call anywhere in the fetch path.
