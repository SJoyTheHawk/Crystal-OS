# Crystal OS — System Design (v1)

What the system looks like and how it behaves. `IMPLEMENTATION_PLAN.md` covers
sequencing; `CODE_GUIDE.md` covers the skeletons. This document is the reference
those two defer to.

## 0. What this device is

Crystal OS is a general app platform. Its **primary target** is an IoT data
display with editable settings: a deployment typically carries 3-5 apps, each a
**card** showing information the user came to read, and switching cards is the
main interaction. That is what v1 optimises for and what the defaults assume.

It is not a ceiling. The app framework, lifecycle, and ABI are deliberately
general — `APP_PLATFORM.md` extends them to third-party apps with their own
runtimes, and nothing in v1 forecloses a deployment with more apps, richer
interaction, or purposes not anticipated here. "Primary target" means the design
resolves ties in favour of the data-display case, not that other cases are
rejected.

Where a phone pattern and an appliance pattern conflict, the appliance wins: the
goal is a user learning the whole interaction model in seconds and then never
thinking about it. Concretely, that is why horizontal is reserved for the OS,
card order is fixed rather than recency-based, and there is no recents chooser.

The one place this matters permanently is the **ABI**. Reserving the horizontal
axis in ABI v1 is forward-compatible — a later version can grant apps access to
it. Granting it now and revoking later breaks every app that used it. So the
restriction goes in while it is still free, and generality is preserved by
loosening later rather than tightening.

Confirmed hardware (board README + `01_AXP2101` example):

| | |
| --- | --- |
| Display | 4-inch IPS, 480×480, 65K colours, ST7701, RGB interface |
| Touch | GT911, I2C, 5-point |
| Power | AXP2101 PMIC, USB-C, 3.7V Li-ion header with charging |
| Timekeeping | PCF85063 RTC |
| Motion | QMI8658 6-axis IMU (unused in v1) |
| Memory | 16MB flash, 8MB PSRAM |

**One I2C bus is shared** by GT911, PCF85063, AXP2101, QMI8658, ES8311 and
ES7210. Touch latency rides on that bus, so background polling must stay sparse:
battery no more than every 30s, RTC only at boot and after SNTP. A chatty poll
loop shows up as drag jitter, which is the one thing this UI cannot afford.

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

- The indicator bar stays visible during app switching (cards slide beneath it).
  Quick settings is ordered above it and slides out from behind it, which is what
  makes the pull-down read as coming from above — but the corner panel only ever
  overlaps the bar's right end, not the whole bar.
- Keyboard sits below quick settings: pulling down over an open keyboard leaves it
  in place rather than dismissing it, and the corner panel covers only the part of
  it that falls inside the panel rect. On close, the keyboard is still there. Card
  switching is suppressed while the keyboard is open.
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
- **Battery:** percentage comes from the AXP2101 fuel gauge (gate G4 is closed).
  Show a distinct charging state when `isCharging()` is true. Poll no more than
  once every 30 seconds because the PMIC shares the touch controller's I2C bus.

## 3. Page inventory

Every screen in the system.

| Page | Reached from | Type |
| --- | --- | --- |
| Launcher | Settings / automatic overflow | Brookesia |
| App content | launcher, app switch | per-app |
| App switcher cards | edge drag | transient overlay |
| Quick settings | top-right corner pull | corner overlay |
| WiFi page | long-press WiFi in quick settings; Settings › Network | full-screen page |
| WiFi credential dialog | pick an SSID on the WiFi page | modal |
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
| Drag down | top band ≤20px, **right 120px only** | `QUICK_SETTINGS` | pull panel |
| Drag up | anywhere, panel open | `QUICK_SETTINGS` | dismiss panel |
| Tap | outside panel, panel open | `QUICK_SETTINGS` | dismiss panel |
| Long press | quick settings WiFi button | — | close panel, open WiFi page |
| Drag vertical | WiFi page SSID list | `APP` (list scrolls) | scroll SSIDs |
| Any other | anywhere | `APP` | passed through |

Constants: `kLockThreshold = 12`, `kEdgeBand = 24`, `kTopBand = 20`,
`kQuickCornerWidth = 120`, long press = 500ms.

`kTopBand` gates the pull-down's vertical component; `kQuickCornerWidth` gates its
horizontal one, measured from the right edge. Dismissal is unrestricted — only the
*open* gesture is confined to the corner.

Precedence, in order:

1. **A modal dialog blocks everything.** No switching, no pull-down.
2. **Quick settings open** suppresses app switching. Only dismiss-up and
   tap-outside are available; the app beneath is visible but not interactive.
3. **Keyboard open** suppresses app switching. Text entry remains committed
   until the keyboard closes.
4. **Mid-app-switch** suppresses the pull-down.
5. **OS beats app.** Once the OS owns a gesture, `lv_indev` events do not reach
   the app at all.
6. Anything unclaimed goes to the app.

Horizontal edge gestures are unconditionally reserved for the OS in ABI v1.
There is no per-app opt-out such as `claimsEdgeGestures()`.

The `kTopBand` restriction is the deliberate exception to rule 4. Without it, a
swipe-down inside a scrolled app view opens quick settings when the user meant to
scroll back up — the only place where OS priority reads as a bug rather than a
feature. `kQuickCornerWidth` narrows the same exception further: the top band's
left portion now belongs to the app outright, so only a corner drag can be
mistaken for a scroll.

The cost is discoverability. Nothing marks the corner, so the pull-down is
learned rather than seen. Accepted for v1 on the grounds that it matches an
established desktop convention; a status-bar affordance is the fallback if
testing says otherwise.

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

**This interaction is required, not aspirational.** The 50% finger-tracked
crossover is the specified switching behaviour for Crystal OS. Performance
measurement may delay *when* it ships — it does not authorise replacing it with a
different interaction. Any shipped substitute (see the snap/fade baseline in
`IMPLEMENTATION_PLAN.md` §Phase 6) is an interim fallback that carries an open
requirement to reach the behaviour described above. Documents describing the
fallback must say so; none of them supersede this section.

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

## 5.5 Navigation model — cards are home

**There is no home screen.** The card ring is home. The device boots into a card
and the user is always already somewhere useful.

This follows from §0: on a data display, a grid of icons is the one screen showing
no data. Removing it deletes a concept rather than just a page — there is no
"return to home", because you never left.

- **Boot destination:** the last-viewed card. The ring index persists in NVS.
  Fallback if that card was uninstalled or fails to build: slot 0. Never an empty
  screen.
- **Order:** launcher slot, fixed. Card 2 is always card 2. Recency ordering
  would put the same swipe in a different place each time, which destroys the one
  property this UI is for — knowing where your data lives.
- **No wrap.** First and last are not adjacent.
- **No recents preview.** Fixed position and page dots are the navigation model;
  a second chooser for the same 3-5 cards adds no useful information.
- **Page dots** in the indicator bar show position and total count. With 3-5 cards
  this is sufficient discoverability; no overview screen is needed.
- **Card order is now a primary feature**, not a convenience. Settings › Manage
  Apps reordering is how a user tunes their instrument.

### Launcher as overflow

Brookesia's launcher still exists and is not removed — Crystal simply does not
route through it. It is reachable from Settings, and **surfaces automatically past
~6 installed apps**, where a ring stops being navigable.

That threshold is what keeps §0's generality honest: appliance by default,
scaling when a deployment outgrows the ring. `APP_PLATFORM.md` makes app count
unbounded, so the ring cannot be the only answer forever.

Implementation note: how load-bearing Brookesia's launcher is has not been
verified — `installApp()` presumably registers an icon and `notifyCoreClosed()`
presumably returns there. If it is wired into the lifecycle, do **not** patch
upstream. Leave it intact and never navigate to it. Same result, no fight.

### Settings is an override, not a card

- Not in the ring: no page dot, unreachable by swiping
- Opens full-screen above the current card, from the quick-settings gear
- Dismisses back to **the card you were on**; the ring does not move underneath
- Card switching is suppressed while it is open, same precedence as a dialog
- Lives at layer 5 (§1), with dialogs — not at layer 0 with card content

**Back is two-level.** Pop the sub-page if inside one (Network, Power, General,
System, Manage Apps); dismiss the override only from the Settings root. A single
level would drop the user to a card from inside Network and lose their place.

### Lifecycle and memory

Stated explicitly because the card model invites the assumption that swiping
leaves cards parked in memory. It does not.

```
swipe A → B:
   A: onPause()     save state to CrystalState; stop timers
   A: onDestroy()   release resources; Brookesia tears down the LVGL tree
   B: onCreate()    rebuild UI from state
```

**Crystal OS borrows four Android lifecycle states: `onCreate`, `onResume`,
`onPause`, `onDestroy`.** Those four are the contract an app author must
understand, and they are the four v1 dispatches. `onBack` is also dispatched but is
a gesture callback, not a lifecycle state.

Two further Android hooks — `onStart`/`onStop` — are **declared but never called in
v1**. They are the reserved pair: cards are opened and closed by swipes with
nothing in between, so there is no event to map them to yet.

They stay wired as base-class no-ops rather than being absent, so adding one
later is a framework change plus an optional override — no ABI break.

| Hook | When | Instance | v1 |
| --- | --- | --- | --- |
| `onCreate()` | **every launch** — build the UI | alive | override |
| `onStart()` | — | alive | reserved |
| `onResume()` | re-select of a still-live paused app | alive | override |
| `onPause()` | leaving — **save state** | alive | override |
| `onStop()` | — | alive | reserved |
| `onDestroy()` | switch away — release resources | alive | override |
| `onBack()` | back gesture | alive | override |

**`onCreate()` fires on every launch, not once at install.** Brookesia recreates
the screen tree each time `run()` is called, so `onCreate()` is Android's
`onCreate` + `onStart` + `onResume` collapsed into one event. An app needing
once-per-boot setup guards it itself.

**`onDestroy()` does not free the instance.** The LVGL tree is torn down but the
C++ object stays alive in Brookesia's app list, which is why the next launch is
`onCreate()` and not construction. There is no `onDestroy()` → `onResume()`
transition: return from a torn-down card is always `onCreate()`.

`onInstall()`/`onUninstall()` were removed — Android has no such hook, and the
name misdescribed what the one use of it actually did (a service reconciling its
own state at boot).

Clock performs that reconciliation once from its first `onCreate()` in each
boot. Countdown timers are cleared after a reboot; a running stopwatch survives
using its persisted absolute start instant and updates the system indicator when
Clock first opens.

The reserved pair is for **screen-off**, the case the current set cannot express.
Backlight off means the card is not visible, but tearing down its UI would make
wake slow, so screen-off wants a pause without a teardown. Declared now, unused
in v1.

Steady-state cost is **one live card's LVGL tree plus two ~7KB snapshots**, and
that figure does not change whether the device carries 3 cards or 30. Peak is
briefly two trees, because the incoming card is built before the outgoing one is
destroyed at the 50% crossover — bounded and momentary, not accumulating.

This works because **data lives in services, not in card instances** (the timer
and Weather patterns). A card is a view over service-owned state, so rebuilding it
is cheap. If cards owned their data, destroy-on-switch would be painful.

The real cost is not memory but **rebuild latency**, and the card model raises the
stakes: swiping is constant here, where phone app-switching is occasional. The
80ms `onResume()` budget stops being advisory — it is the number that decides
whether swiping feels instant or sticky. The rebuild is hidden inside the 250ms
drag animation only as long as it holds.

Fallback if measurement says otherwise: keep the **two adjacent neighbours**
resident, bounded at three trees regardless of card count, since those are the
only ones a single swipe can reach. Held in reserve — build destroy-on-switch
first, measure, add the cache only if the feel demands it.

## 6. Quick settings

Pulled from the top 20px band **within the right 120px**. The panel wraps its own
content and hangs from the top-right corner; it does not span the display. Size is
derived from a 4x4 grid of 62px cells — 306x306 on this panel — and never
hardcoded. Full geometry and rationale in `PHASE_8_5_QUICK_PANEL.md`.

```
+-----------------------------------------------+
| status bar                                    |
|                    +----------------------+   |
|                    | +--------++--------+ |   |
|                    | | ((*))  ||   BT   | |   |
|                    | | WiFi   ||Bluetoo.| |   |
|                    | |MyNetwo.||Unavail.| |   |
|     app, fully     | +--------++--------+ |   |
|     visible and    | +--++--++----++----+ |   |
|     untouched      | |##||##||Ener||Gear| |   |
|                    | |##||##|| Off||    | |   |
|                    | |##||##|+----++----+ |   |
|                    | |##||##|             |   |
|                    | |Br||Vo| (reserved)  |   |
|                    | +--++--+             |   |
|                    +----------------------+   |
+-----------------------------------------------+
```

- **Background:** none. The panel body is opaque — a vertical gradient with a 1px
  top highlight — and the app stays fully visible and undimmed everywhere outside
  the panel rect. No snapshot is taken. Frosted translucency would cost a
  full-screen capture plus a downscale on every open, and would blend a zoomed
  image on every drag frame; opaque draws through LVGL's solid-fill path.
- **Grab handle:** none. It belonged to a full-width sheet; a corner card that
  animates from its own corner does not need one.
- **Half-open threshold:** release past 50% of panel travel completes the open
  (200ms ease-out); release below snaps shut. Motion is translate-only — opacity
  is never animated, since a non-opaque parent composites through an intermediate
  buffer.
- **Dismiss:** drag up, tap the gear, or **tap anywhere outside the panel**. The
  outside-tap target is a transparent full-screen catcher, attached only after the
  open animation finishes so the pull gesture's own release cannot dismiss it.
- **Brightness / volume:** vertical fill bars, iPhone-style, dragged directly, one
  grid column wide and two rows tall. Range **0..95**, not 0..100, matching
  `BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX`. Volume goes to
  `bsp_extra_codec_volume_set`. No text label — a fill level is self-evident.
- **Power saving:** a single 1x1 tile, not a switch. Tile colour is the state
  (translucent white off, accent fill on) plus a one-word state line, so "off"
  never reads as "broken". Several effects (§8).
- **Bluetooth:** present, visibly unavailable, non-interactive. Reserved. Its
  status text is required: off and unavailable both render as a dim tile, so
  colour alone cannot distinguish them.
- **Gear:** opens Settings and dismisses the panel.
- **Reserved cells:** the two grid cells beside the gear stay empty until a real
  control needs them. Phase 9 no longer claims them — WiFi expands into its own
  page, not into the panel. No filler tile may be added to square off the panel.

State presentation rule for the whole panel: colour carries binary state; any
control with more than two states also carries text. WiFi has at least four
(below), which no single colour can express.

### WiFi button states

| State | Icon | Label |
| --- | --- | --- |
| Connected | white/black | SSID |
| Disconnected | **grey** | "Not Connected" |
| Connecting | white, pulsing | "Connecting…" |
| Hardware off | grey | "Off" |

### WiFi flow

Long-press (500ms) the WiFi tile → quick settings closes completely → a
full-screen **WiFi page** opens. The tile does not expand in place.

The page is a normal full-screen page over the app area, below the indicator bar.
It holds a title, a back button, and a full-width scrollable SSID list sorted by
RSSI descending with the connected network pinned first. When nothing is
connected, pure RSSI order. Rows show SSID, a signal glyph, and a lock glyph when
secured.

```
long-press WiFi tile
   |
   +-> quick settings animates closed, gesture ownership released
   |
   +-> WiFi page opens, scan starts, list shows "Scanning..." until results
         |
         +-> tap a row -> credential dialog (keyboard opens)
         |     |
         |     +- success -> close dialog, close page, return to the app that
         |     |             was running, toast "Connected to <SSID>",
         |     |             bar icon turns active
         |     +- failure -> close dialog, stay on the page,
         |                   toast "Failed to connect to WiFi network"
         |
         +-> back -> close page, return to the app that was running
```

**Why a page and not an inline list.** The inline version stacked a scrollable
list, a modal, and asynchronously arriving scan results inside a panel that is
simultaneously animating and holding `QUICK_SETTINGS` gesture ownership. Nested
scroll inside a translating parent, and a modal whose parent can be destroyed by
a dismiss gesture, are both avoidable. The page owns its own area, scrolls
normally, and has one clear exit.

Ownership: while the WiFi page is open the quick-settings pull-down is
unavailable and app switching is suppressed, same as `s_settings_open`. Back is
the only exit besides a successful connect.

Outcomes are toasts; input is dialogs. That split is why the toast layer is built
before WiFi. Failure is a toast on the page rather than a stacked dialog, so
there is never a dialog over a dialog.

Settings › Network opens the same page. There is one SSID list in the system.

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

1. ~~**Launcher gesture.**~~ **Closed** — cards are home (§5.5). There is no
   launcher to reach in the common case, so no gesture is needed and no arbiter
   entry is added.
2. ~~**App switch ring wrap.**~~ **Closed: no wrap.** Page dots already show
   position, and hitting the end is useful feedback. Wrapping would make the
   first and last cards adjacent, which breaks the spatial model that the card
   design exists to provide.
3. **Indicator bar interactivity.** Non-interactive in v1 apart from the page
   dots and the counting-timer glyph. Tapping the clock or WiFi icon as a
   settings shortcut is an obvious later addition.
5. **Dashboard card (`summary()` ABI hook).** A glance card showing the key
   reading from each app, tapping a tile to jump to that card — strictly more
   useful than an icon grid on a data display. Not built in v1. The open part is
   whether to *reserve* the optional hook in ABI v1, since adding it to a shipped
   ABI is the expensive direction (see §0).
4. **Per-app bar style declaration.** `barStyle()` is proposed as a static
   per-app value. An app with a light header and dark body may want it dynamic.
