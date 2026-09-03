# Crystal OS — System Design (v1)

What the system looks like and how it behaves. `IMPLEMENTATION_PLAN.md` covers
sequencing; `CODE_GUIDE.md` covers the skeletons. This document is the reference
those two defer to.

Screen: 480x480, ~86mm square, ~5.6 px/mm. That density is unusually low for a
touch UI — a 44px control is ~7.9mm, comfortably tappable. It also means text
below ~16px is coarse rather than small. Minimum touch target: **44x44px**.

## 1. Layer model

Z-order, bottom to top. Anything ambiguous about "what covers what" resolves
here.

| # | Layer | LVGL home | Interactive |
| --- | --- | --- | --- |
| 0 | App content | `lv_scr_act()` | yes |
| 1 | Indicator bar | Brookesia status bar | no (v1) |
| 2 | App switch cards | shell container | during drag only |
| 3 | Keyboard overlay | shell container | yes |
| 4 | Quick settings panel | shell container | yes |
| 5 | Dialogs | `lv_layer_top()` | yes, modal |
| 6 | Toasts | `lv_layer_top()` | **never** |

Rules that follow from the ordering:

- The indicator bar stays visible during app switching (cards slide beneath it)
  but is **covered** by quick settings, which is what makes the pull-down read as
  coming from above.
- Keyboard sits below quick settings: pulling down over an open keyboard covers
  it rather than dismissing it. On close, the keyboard is still there.
- Dialogs are modal and block the arbiter entirely — no app switching, no
  pull-down while one is open.
- Toasts are above dialogs so a dialog can produce one, and are always
  non-interactive.

## 2. Indicator bar

Height ~40px (inherit from the Brookesia `480_480` stylesheet — the reference
hardcodes a 40px offset in `Drawpanel::touch_event_cb`, which is the hint, not a
guarantee; confirm against the stylesheet).

```
┌──────────────────────────────────────────────┐
│ [logo]                    14:32  [wifi] [bat]│  ~40px
├──────────────────────────────────────────────┤
│                                              │
```

- **Left:** company logo, monochrome.
- **Right:** time (`HH:MM`, no date), WiFi state, battery.
- **Contrast rule:** icons and text are pure black or pure white, chosen by the
  foreground app's declared background luminance (threshold 0.5). The bar's own
  background inherits the app's background colour, so the bar reads as part of
  the app rather than a separate strip. Apps declare this via
  `CrystalApp::barStyle()`; default is dark.
- **Time before first sync:** RTC provides a valid time before the first frame,
  so `--:--` should be rare. It is still the defined fallback if the PCF85063
  read fails.
- **WiFi icon:** white/black when connected, **grey** when disconnected — grey is
  the one colour exempt from the contrast rule, since it must read as "inactive"
  in both themes.
- **Battery:** hidden entirely in v1 unless a sense pin is confirmed (gate G4).
  Reserve the space so enabling it later does not reflow the bar.

## 3. Page inventory

Every screen in the system.

| Page | Reached from | Type |
| --- | --- | --- |
| Launcher | home gesture / app close | Brookesia |
| App content | launcher, app switch | per-app |
| App switcher cards | edge drag | transient overlay |
| Quick settings | top-edge pull | overlay |
| WiFi SSID list | long-press WiFi in quick settings | expanding inline list |
| WiFi credential dialog | pick an SSID | modal |
| Settings root | gear in quick settings | app |
| Settings › Network | settings root | sub-page |
| Settings › Power | settings root | sub-page |
| Settings › General | settings root | sub-page |
| Settings › System | settings root | sub-page |
| Settings › Manage Apps | settings root | sub-page |

## 4. Gestures

Complete table. The arbiter claims exactly one owner per touch, after 12px of
travel, and holds it until lift.

| Gesture | Zone | Owner claimed | Action |
| --- | --- | --- | --- |
| Drag right | left edge, ≤24px | `APP_SWITCH` | previous app |
| Drag left | right edge, ≤24px from right | `APP_SWITCH` | next app |
| Drag down | top band, ≤20px | `QUICK_SETTINGS` | pull panel |
| Drag up | anywhere, panel open | `QUICK_SETTINGS` | dismiss panel |
| Long press | quick settings WiFi button | — | expand SSID list |
| Drag vertical | SSID list | `APP` (list scrolls) | scroll SSIDs |
| Any other | anywhere | `APP` | passed through |

Constants: `kLockThreshold = 12`, `kEdgeBand = 24`, `kTopBand = 20`, long press
= 500ms.

Precedence, in order:

1. **A modal dialog blocks everything.** No switching, no pull-down.
2. **Quick settings open** suppresses app switching. Only dismiss-up is
   available.
3. **Mid-app-switch** suppresses the pull-down.
4. **OS beats app.** Once the OS owns a gesture, `lv_indev` events do not reach
   the app at all.
5. Anything unclaimed goes to the app.

The `kTopBand` restriction is the deliberate exception to rule 4. Without it, a
swipe-down inside a scrolled app view opens quick settings when the user meant to
scroll back up — the only place where OS priority reads as a bug rather than a
feature.

Reserved, not built in v1: multi-touch. Apps may opt into a raw GT911 point
array via `ITouchRaw`, bypassing `lv_indev`. No OS-level pinch or rotate
recognisers.

## 5. App switching

Apps form an ordered ring by launcher slot. Left is previous, right is next.

Frames during a rightward drag from the left edge (revealing the previous app):

```
    idle              dragging 30%           past 50%            settled
┌───────────┐      ┌──┬─────────┐      ┌──────┬──────┐      ┌───────────┐
│           │      │  │         │      │      │      │      │           │
│  current  │      │p │ current │      │ prev │ curr │      │   prev    │
│           │      │  │         │      │      │      │      │           │
└───────────┘      └──┴─────────┘      └──────┴──────┘      └───────────┘
                    ^ incoming card      ^ crossover:         ^ current
                      = blurred snap       live render,         destroyed
                                           touch transfers
```

Behaviour:

- The **incoming** app is drawn as its 1/8 blurred snapshot on a card that
  follows the finger. Cheap, and it hides the fact that nothing is loaded yet.
- The **outgoing** app stays centred and live underneath. It does not move.
- **Crossover at 50%.** Past that point the incoming app is really instantiated
  (`onCreate()` — nothing is resident to resume, see below), the card is replaced
  by live content, and new touch input goes to the incoming app.
- **Release before 50%** → card animates out, no switch, outgoing app never
  paused.
- **Release after 50%** → card completes, outgoing app gets `onPause()` then
  `onDestroy()`.

The incoming app is built, not resumed, because only one app is ever resident
(`max_running_num = 1`). That makes the switch path the same every time, which is
the point: an app never has to ask whether it is being created or restored. The
hook contract lives in `IMPLEMENTATION_PLAN.md` §Phase 4.5.

Animation: 250ms, ease-out, on release. During drag the card tracks the finger
1:1 with no easing — anything else feels laggy at this size.

Card styling: 12px corner radius, a soft shadow on the leading edge to separate
it from the app beneath, no scrim on the outgoing app in v1.

**Why snapshots and not resident apps.** The snapshot pair costs ~14KB total
regardless of how many apps have ever been opened, where keeping apps resident
costs their whole LVGL object tree each. The user-visible result is the same
because the rebuild happens behind the card. This is the central memory decision
of v1 and the switcher exists in this shape to serve it.

**Contingent on gate G1.** If a full-screen drag with a blurred backdrop
measures under ~20 FPS on one RGB buffer, the fallbacks in order are: enable
double buffering (+460KB PSRAM), drop the leading-edge shadow, then replace the
tracking card with a straight 200ms cross-fade.

## 6. Quick settings

Pulled from the top 20px band. Panel height ~320px of the 480.

```
┌──────────────────────────────────────────────┐
│ ▔▔▔▔▔▔▔▔  (grab handle, highlights on hold)  │
│                                              │
│  ┌────────────┐  ┌────────────┐              │
│  │   WiFi     │  │ Bluetooth  │              │
│  │  MyNetwork │  │  (disabled)│              │
│  └────────────┘  └────────────┘              │
│                                              │
│  ┌────────────┐  ┌───┐ ┌───┐                 │
│  │            │  │ ☼ │ │ ♪ │                 │
│  │ ▓▓▓▓▓░░░░  │  │ ▓ │ │ ▓ │  ← fill bars    │
│  │            │  │ ▓ │ │ ▓ │                 │
│  └────────────┘  └───┘ └───┘                 │
│   power saving   bright  vol                 │
│                                       [gear] │
├──────────────────────────────────────────────┤
│         app beneath, blurred snapshot        │
└──────────────────────────────────────────────┘
```

- **Background:** a 1/8 blurred snapshot of the foreground app taken at pull
  start, not live. Live translucency would mean recompositing the app every frame
  on a single-buffer RGB panel.
- **Grab handle:** hidden until a touch lands in the top band, then highlights —
  the affordance that the panel exists.
- **Half-open threshold:** release past 50% completes the open (200ms ease-out);
  release below snaps shut.
- **Brightness / volume:** vertical fill bars, iPhone-style, dragged directly.
  Range **0..95**, not 0..100, matching `BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX`.
  Volume goes to `bsp_extra_codec_volume_set`.
- **Power saving:** one toggle, several effects (§8).
- **Bluetooth:** present, visibly disabled, non-interactive. Reserved.
- **Gear:** opens Settings and dismisses the panel.

### WiFi button states

| State | Icon | Label |
| --- | --- | --- |
| Connected | white/black | SSID |
| Disconnected | **grey** | "Not Connected" |
| Connecting | white, pulsing | "Connecting…" |
| Hardware off | grey | "Off" |

### WiFi flow

Long-press (500ms) the WiFi button → the tile expands into an inline scrollable
SSID list, sorted by RSSI descending, with the connected network pinned first.
When nothing is connected, pure RSSI order.

```
pick SSID → SSID list collapses → credential dialog (keyboard auto-opens)
   ├─ success → dismiss dialog + panel + list, return to app, toast
   │            "Connected to <SSID>", bar icon turns active
   └─ failure → dialog: "Failed to connect to WiFi network", stay in flow
```

Outcomes are toasts; input is dialogs. That split is why the toast layer is built
before WiFi.

Known networks reconnect without a prompt. Saved credentials live in
NVS-encrypted storage (`nvs_keys` partition) — plaintext otherwise.

## 7. Keyboard overlay

English QWERTY, height ~180px, three planes: letters, numbers/symbols, shift.
Password fields mask with a reveal toggle.

When the keyboard opens, the app viewport bottom binds to the keyboard top, and
the app becomes scrollable within that reduced band.

Field positioning — the whole rule:

- Focused field **already fully visible** → do not move it. Even if the keyboard
  is nowhere near it. No jump.
- Focused field **would be covered** → scroll it to the vertical midpoint between
  the keyboard top and the indicator bar bottom, 250ms ease-out.

Dismiss: return/done key, tapping outside any field, or the OS back gesture. The
pull-down covers the keyboard rather than closing it (§1).

## 8. Settings

| Category | Rows |
| --- | --- |
| Network | WiFi (SSID list, connect, forget); DHCP / static toggle; IP, gateway, netmask, DNS |
| Power | screen dim timeout; screen off timeout; dim level; power saving toggle |
| General | timezone |
| System | hardware version; OS version; company info; current IP; attribution |
| Manage Apps | per-app install/uninstall, reorder, clear data |

Notes:

- **Static IP fields are disabled while DHCP is on**, and validated on commit
  rather than per-keystroke.
- **Timezone is required, not optional.** Stored as a POSIX TZ string. Without
  it, SNTP leaves the clock on UTC and the indicator bar shows the wrong hour.
- **System › attribution** is where `esp-brookesia` and ESP-IDF are credited in
  the UI, alongside the root `NOTICE` file.
- **Power saving** is one NVS flag with several effects: CPU capped at 80MHz,
  `WIFI_PS_MAX_MODEM`, lowered brightness ceiling, shortened timeouts.
- Screen states: full → dim (after dim timeout) → backlight off (after off
  timeout). Any touch restores full brightness and **is swallowed**, not
  delivered to the app. No automatic light sleep in v1 — the RGB panel is a
  continuous DMA scan-out and will blank or tear.

## 9. Manage Apps

The user-facing face of the NVS registry. To the user this is installing and
removing apps; underneath, install is `app.<id>.enabled = 1`, reorder is
`app.<id>.slot`, and clear-data is `CrystalState::clear()`.

```
┌──────────────────────────────────────────────┐
│  Manage Apps                                 │
├──────────────────────────────────────────────┤
│  ≡  Notes            [Installed]  [Clear]    │
│  ≡  Calculator       [Installed]  [Clear]    │
│  ≡  Draw             [Install  ]             │
└──────────────────────────────────────────────┘
```

Drag the `≡` handle to reorder. Changes apply to the launcher immediately;
disabled apps are never constructed, so a hidden app costs flash but no RAM.

The honest limit, worth stating in the UI copy: users can only install what
shipped in the firmware. The catalog grows when the OS updates.

## 9.5 Bundled apps

Three ship in v1: Clock, Weather, Calculator. Clock is first because it is the
hardest test of the lifecycle; Calculator is last because it is borrowed.

### Clock

Three tabs: **Clock**, **Timer**, **Stopwatch**.

Timer and stopwatch are different tools and both are worth having — the brief
said "stopwatch" but described timer presets, so v1 builds both. Stopwatch counts
up from zero with lap capture; timer counts down from a chosen duration.

```
┌──────────────────────────────────────────────┐
│  [Clock]   Timer    Stopwatch                │
├──────────────────────────────────────────────┤
│                                              │
│              14:32:07                        │
│           Tuesday, 2 Sep                     │
│                                              │
│           <timezone name>                    │
└──────────────────────────────────────────────┘
```

Clock tab: `HH:MM:SS` large, date below (the indicator bar deliberately omits
date, so this is where it lives), timezone name from the General setting. Updates
on a 1s `lv_timer`, not a busy loop.

Timer tab: preset chips — **30s, 1m, 3m, 5m, 10m, 20m, 30m** — plus a custom
entry. Controls: start, pause, resume, reset. Progress as a ring, remaining time
as `MM:SS` in the centre.

```
   idle ──select preset──> ready ──start──> running ──┬─pause─> paused ──resume─┐
    ^                        ^                        │                          │
    └────────reset───────────┴────────────────────────┴──────reset───────────────┘
                                     │
                                  expires
                                     v
                                  finished ──dismiss/reset──> idle
```

Stopwatch tab: start, pause, resume, lap, reset. Laps in a scrollable list,
newest first. Cap at 50 laps — this is a 480px screen, not a running watch.

**The timer must keep running when the app is destroyed.** This is the important
part, and it is the reason Clock is the first app: switching away from a running
timer must not cancel it. The app cannot own the countdown, because the app stops
existing.

The design that follows:

- The **absolute end time** (`time_t`, not a remaining-seconds counter) lives in
  `CrystalState`. Remaining time is always derived as `end - now`, so it stays
  correct across destruction, and across an SNTP correction.
- A **service-level timer** in `crystal_service` owns expiry, independent of any
  app. On expiry it posts to the UI queue and raises a toast plus a chime —
  wherever the user happens to be.
- Reopening Clock reads the end time and resumes the ring mid-flight. Nothing was
  paused.
- Stopwatch works the same way: store the start instant, derive elapsed.
- Paused state stores remaining seconds instead of an end time, plus a flag.

A running timer or stopwatch should show a small indicator-bar glyph so the user
knows something is counting while they are elsewhere. That is the one exception to
the "no new bar content in v1" stance, and it is worth it — an invisible running
timer is a bug report.

### Weather

Current conditions: temperature, humidity, and a condition icon (clear, partly
cloudy, cloudy, fog, drizzle, rain, snow, thunderstorm).

```
┌──────────────────────────────────────────────┐
│                    ☁                         │
│                  22°C                        │
│              Partly cloudy                   │
│                                              │
│   Humidity 64%          Wind 12 km/h         │
│                                              │
│   Updated 4 min ago            <location>    │
└──────────────────────────────────────────────┘
```

**Use Open-Meteo, not weather.com.** The deciding reason is not price — it is
that Open-Meteo needs no API key. Any key compiled into firmware is extractable
with `esptool read_flash`, so a keyed API means either shipping a key you must
treat as public, or standing up a proxy server. Open-Meteo avoids the choice
entirely, returns compact JSON, and its WMO weather codes map cleanly to a small
icon set.

```
GET https://api.open-meteo.com/v1/forecast
      ?latitude=<lat>&longitude=<lon>
      &current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m
```

- **Location:** no GPS on this board. Lat/long is a Settings › General field.
  Manual entry is the v1 answer; IP-based geolocation is a later convenience.
- **Refresh:** on app open if the cache is older than 15 minutes, plus a service
  refresh every 30 minutes while WiFi is up. Never per-frame, never on a tight
  timer.
- **Cache:** last reading plus its timestamp in `CrystalState`, so the app shows
  data immediately on open instead of a spinner, with "Updated N min ago" making
  staleness visible rather than hidden.
- **HTTPS cost:** a TLS handshake needs roughly 30-45KB of heap. Fine with PSRAM,
  but the certificate bundle is a flash cost and the fetch must never run on the
  LVGL task — it goes on `crystal_service`, results via the UI queue.
- **Icons:** eight condition glyphs, in SPIFFS, mapped from WMO codes.

States: no WiFi → cached reading plus "Offline"; no location set → prompt
pointing at Settings; fetch failed → keep the cache and show the stale age;
never-fetched and offline → an empty state, not a spinner.

**Open question — onboard sensor.** "Local temperature and humidity" may mean the
room, not the region. If the board carries an I2C temp/humidity part, the better
design is indoor readings from the sensor alongside outdoor from the API, which is
genuinely more useful than either alone. I could not verify what sensors this
board has; worth checking the schematic before building.

### Calculator

Borrowed from the reference (`components/apps/calculator`, 439 lines,
Apache/CC0 per its headers). Ported to `CrystalApp`, with the icon moved from the
906KB C array to a SPIFFS binary. Serves as the third lifecycle conversion test
and needs no network.

## 10. Motion and type

| Motion | Duration | Curve |
| --- | --- | --- |
| App switch settle | 250ms | ease-out |
| Quick settings open/close | 200ms | ease-out |
| Keyboard show/hide | 250ms | ease-out |
| Field scroll-into-view | 250ms | ease-out |
| Toast fade in / out | 150ms / 200ms | linear |
| Dialog appear | 180ms | ease-out |
| Brightness ramp (auto-dim) | 400ms | linear |

Nothing exceeds 250ms except the deliberate auto-dim ramp. Drag-tracking is
always 1:1 with no easing.

Type: three Montserrat sizes — **16 small, 20 medium, 28 large** — plus the
indicator bar size. Trim the reference's ~20 compiled sizes to these; each unused
one is dead flash. English only in v1.

Toast hold: 2500ms.

## 11. States to design for

Easy to forget until they appear on a device:

- No WiFi configured, first boot — launcher and apps must be fully usable.
- WiFi scan returns nothing → "No networks found", not an empty list.
- RTC read fails → `--:--` in the bar.
- Only one app installed → edge drags must no-op cleanly, not wrap to self.
- All apps uninstalled → launcher needs an empty state pointing at Manage Apps.
- Recovered from a crash → quiet notice, once, then the flag clears.
- `onCreate()` exceeds its 80ms budget → warn in debug builds; the 5s task
  watchdog is the real failure mode, since it runs under the LVGL lock. This is
  the launch path, so it is the one that matters — `onResume()` only fires for a
  still-resident app, which one-app residency makes rare.
- Static IP misconfigured → device must stay reachable enough to fix itself.

## 12. Open design questions

Not blockers, but undecided:

1. **Launcher gesture.** Brookesia provides a home affordance; whether Crystal
   adds a bottom-edge swipe is unsettled, and it would need an arbiter entry.
2. **App switch ring wrap.** Does next-from-last wrap to first, or stop? Stopping
   is more predictable; wrapping is faster to cycle. Leaning stop.
3. **Indicator bar interactivity.** Non-interactive in v1. Tapping the clock or
   WiFi icon as a settings shortcut is an obvious later addition.
4. **Per-app bar style declaration.** `barStyle()` is proposed as a static
   per-app value. An app with a light header and dark body may want it dynamic.


