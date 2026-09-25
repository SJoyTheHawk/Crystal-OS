# Phase 9.7 — Transit (HK Bus): design

User-facing behaviour for the bundled transit app. Written to fold into
`DESIGN.md` §9.5 beside Clock, Weather, and Calculator. Scope and rationale are in
[`PHASE_9_7_TRANSIT_PROPOSAL.md`](PHASE_9_7_TRANSIT_PROPOSAL.md); shapes and call
sites are in
[`PHASE_9_7_TRANSIT_CODE_GUIDE.md`](PHASE_9_7_TRANSIT_CODE_GUIDE.md).

## 1. What this app is

A departure board for one stop at a time. The user names a route, picks a
direction, picks a stop, and reads how many minutes until the next buses. Stops
worth watching get saved so the next visit is a single glance with no typing.

It is not a journey planner and not a map. Those need a screen you can pan and a
keyboard you can type sentences on, and this is a 480x480 panel on a shelf.

What it is *for*: the two or three stops near where the device lives. The saved
stops list is the primary screen after first use, and everything else is the path
to filling it.

## 2. Screens

Three tabs, following Clock's precedent. Tabs are the only navigation; there are no
nested pages, so `onBack()` has one job (§6).

```
┌──────────────────────────────────────────────┐
│  [Saved]   Route    Nearby                   │
├──────────────────────────────────────────────┤
```

### 2.1 Saved

The landing tab whenever at least one stop is saved.

```
┌──────────────────────────────────────────────┐
│  [Saved]   Route    Nearby            ⟳      │
├──────────────────────────────────────────────┤
│ ┌──────────────────────────────────────────┐ │
│ │ 68X  to Yuen Long                        │ │
│ │ Tsuen Wan Station                        │ │
│ │ 3 min · 11 min · 24 min                  │ │
│ └──────────────────────────────────────────┘ │
│ ┌──────────────────────────────────────────┐ │
│ │ 960  to Wan Chai                         │ │
│ │ Tuen Mun Road Interchange                │ │
│ │ Due · 8 min · 19 min                     │ │
│ └──────────────────────────────────────────┘ │
│                                              │
│ Updated 40 s ago                             │
└──────────────────────────────────────────────┘
```

Each card is route, destination, stop name, and up to three ETAs. Tapping a card
opens the board (§2.4) for that stop. Long-press offers Remove.

Cap at eight saved stops. Eight cards is already more than one screen, and every
saved stop is one ETA request per refresh — the cap is a network budget as much as
a layout one. At eight, Save is disabled with "Saved stops full — remove one
first".

Empty state, first run: "No saved stops yet. Find a route to get started," with
the Route tab as the affordance. Not a spinner, and not a blank list.

### 2.2 Route

```
┌──────────────────────────────────────────────┐
│  Saved   [Route]   Nearby                    │
├──────────────────────────────────────────────┤
│              ┌──────────────┐                │
│              │      68      │  ⌫             │
│              └──────────────┘                │
│        1    2    3     A   B   C   E         │
│        4    5    6     K   M   P   R         │
│        7    8    9     S   X   ⏎             │
│             0                                │
│                                              │
│  Recent:  68X   960   264M   B3X             │
└──────────────────────────────────────────────┘
```

An on-app keypad, not the shell keyboard overlay. Route names are 1-4 characters
over a 28-character alphabet, and the shell keyboard is a full QWERTY that would
cover half the screen for four keystrokes.

**Keys filter as you type.** The device holds a packed index of every KMB and CTB
route name — 1,048 of them, ~6 KB — so it knows what can follow the prefix on
screen. After `68` the only live keys are the digits and letters that begin a real
route (`68A`, `68X`, …) plus ⌫ and ⏎; everything else greys out. This is the
official KMB app's behaviour and it is what makes an exact-match search usable: the
user cannot type a route that does not exist, so "not found" mostly stops
happening.

Three rules make the greying trustworthy rather than annoying:

- **Keys grey, they never move or disappear.** The grid is fixed. A reflowing
  keypad moves the next target under a finger already travelling toward it, and
  the mistap lands on whatever slid into place. Compare the reference web app's
  scrolling letter strip: same information, but the character you were reaching for
  is somewhere else by the time you arrive.
- **Greyed is dimmed, not gone.** A greyed key stays visible at reduced opacity so
  the layout stays learnable — the `X` is always in the same place whether or not
  this prefix can reach it.
- **Greyed keys are still tappable, and taking one is not an error.** The index can
  be behind the live feed. Tapping a greyed key enters the character and submits
  against the real API, which is the authority. A brand-new route is reachable on a
  device whose index has never refreshed; it is simply not advertised. Grey means
  "we don't think so", never "you can't".

⏎ lights only when the typed string is a complete route name. Letters can stay live
at the same time — `2` is a real route and so is `2A`, so a complete name is not
necessarily a finished one.

Recent routes: last six, most recent first, persisted. After the first visit the
stops a user cares about are reached by tap, not by typing at all.

Submitting queries KMB first, then Citybus. Three outcomes:

- One operator, one direction → straight to the stop list.
- Multiple directions or both operators → a chooser: `68X · KMB · to Yuen Long`,
  one row per variant, origin and destination on each.
- Nothing → "Route 68Y not found. Check the number and try again." The query stays
  in the display so it can be edited rather than retyped.

The index refreshes itself quietly, at most daily, and never while this tab is
open. There is no version number on screen, no "checking for updates", and no
progress. A route list that changes a few times a year does not deserve a UI. The
one visible consequence is in Device Status, which gains a `Route list` row with
the date the index was last confirmed — the place to look when a key greys out that
shouldn't.

### 2.3 Stop list

Reached from Route. Header carries the route, operator, and destination, with a
back affordance to the chooser.

```
┌──────────────────────────────────────────────┐
│ ‹  68X · KMB · to Yuen Long                  │
├──────────────────────────────────────────────┤
│  1   Tsim Sha Tsui East                      │
│  2   Kowloon Park Drive                      │
│  3   Stop 3                            ···   │
│  4   Stop 4                            ···   │
│ ...                                          │
├──────────────────────────────────────────────┤
│           [ Nearest stop to me ]             │
└──────────────────────────────────────────────┘
```

Rows are numbered by sequence, which is information the device has instantly.
Names arrive per §3.2 of the proposal: a row shows `Stop 7` with a quiet `···`
until its name resolves, then the name replaces it in place. No row is ever blank
and the list never reflows — the sequence number holds the row's identity, so a
name landing does not move anything under the user's finger.

"Nearest stop to me" resolves every stop on the route and jumps to the closest. It
is a button, not automatic, because it costs 60-120 requests. While running:
`Checking stops… 34 of 118` with Cancel. Cancel is honoured within one in-flight
request. Without coordinates in Settings the button is disabled with "Set your
location in Settings › Region & Time".

### 2.4 Board

The one stop the user chose.

```
┌──────────────────────────────────────────────┐
│ ‹  68X to Yuen Long                    ♥  ⟳  │
├──────────────────────────────────────────────┤
│   Tsuen Wan Station                          │
│   Stop 14 of 118                             │
│                                              │
│   ┌────────────────────────────────────────┐ │
│   │  3 min          14:32                  │ │
│   │  11 min         14:40    Scheduled     │ │
│   │  24 min         14:53                  │ │
│   └────────────────────────────────────────┘ │
│                                              │
│   Updated just now                           │
└──────────────────────────────────────────────┘
```

Three ETAs, each as minutes-until plus the absolute time. Both, deliberately:
minutes are what you act on, the clock time is what survives a stale reading — if
the refresh failed, `14:32` is still checkable against the indicator bar while
"3 min" has quietly become a lie.

- Under 60 s → `Due`.
- Beyond 60 min → the clock time only.
- Operator remarks (`rmk_en`) show under the row when present and short:
  `Scheduled`, `Last bus`.
- Fewer than three ETAs → show what exists. Empty cells stay empty
  (`MEMORY.md`: reserve space, never fill it).
- None at all → "No departures scheduled" with the age line beneath.

♥ saves or unsaves the stop. ⟳ refreshes now, ignoring the throttle.

Auto-refresh every 30 s while the board is the visible tab, and once on open.
Never on a tighter timer, and never while the app is paused — a destroyed app
issues no requests at all.

### 2.5 Nearby

Saved stops ranked by distance from the Settings coordinates, nearest first, with
the distance on each row.

This tab is deliberately *not* "all stops near me". That needs a searchable index
of every stop in the territory, which is a ~1.5 MB download this device has
nowhere to put. Presenting saved stops by distance is the honest version of the
feature: useful the moment a second stop is saved, and it never lies about
coverage.

Header states it plainly: "Your saved stops, nearest first." With no coordinates
set: a prompt pointing at Settings › Region & Time. With nothing saved: the same
empty state as §2.1.

## 3. Freshness

Every ETA on screen carries its age, in one line at the bottom of the tab. Age is
the whole trust model of this app — a departure board that cannot be dated is
worse than no board, because the user acts on it.

- `Updated just now` — under 30 s.
- `Updated N min ago` — up to an hour.
- Past 5 minutes the ETA numbers dim, values and captions together, and the age
  line reads `Updated 7 min ago · may be out of date`.
- Past 30 minutes the minute counts are withdrawn entirely and only the clock
  times remain, dimmed. A 30-minute-old "3 min" is not stale, it is wrong.
- Age unknown, because the clock was never set → `Updated at an unknown time`.
  Treated as due for refresh, exactly as Weather treats it.

## 4. States to design for

Extends `DESIGN.md` §11.

- **No WiFi.** Saved stops render from cache with their age. No spinner, no
  retry storm. Reconnecting refreshes on the next tick.
- **Request timed out.** Keep the previous reading, keep its age, say
  `Couldn't refresh · showing 4 min old`. Never blank a good reading to report a
  bad request.
- **Route found but no stops.** Real, for a suspended route: "No stops listed for
  this route."
- **API returns an error or nonsense.** One quiet toast, cache untouched.
- **Clock not set.** Absolute times cannot be shown; minutes-until still can, from
  the ETA timestamps' own arithmetic. Show minutes, hide the clock column, and say
  the age is unknown.
- **Swipe away mid-lookup.** Route, direction, and stop persist. Coming back shows
  the same place, repaints from cache, and re-requests once.
- **Nearest-stop resolve interrupted by a swipe away.** The job is cancelled at
  teardown, not left running for an app that no longer exists.
- **Saved stop's route changes upstream.** A saved stop whose ETA request 404s
  three times in a row shows `Route may have changed` and offers Remove. It is not
  silently deleted — the user saved it.
- **Index older than the feed.** A real route greys out on the keypad. It is still
  tappable and still resolves, so the user is inconvenienced by a dim key and
  nothing else. Device Status shows when the index was last confirmed.
- **Index refresh never succeeds** (device is offline for months, or SPIFFS is
  full). The compiled baseline keeps working indefinitely. There is no degraded
  mode and no nag — the app has never depended on the refresh, only benefited from
  it.

## 5. Motion and type

Nothing new. `DESIGN.md` §10 sizes only: 16 small, 20 medium, 28 large. Route
numbers use large, stop names medium, age and captions small. The 48 px size
Weather took for its hero is not used here.

Tab switches and sheet appearances use the existing 180-250 ms ease-out values. ETA
numbers changing on refresh do not animate — a number that slides is a number you
misread.

## 6. Gestures and navigation

Owned by the shell, unchanged. The app claims nothing.

- Bottom edge → home pill behaviour, per `MEMORY.md`: pill is Home, Back is Back.
- Horizontal edge drags → app switch. The stop list scrolls vertically only, so
  there is no conflict to arbitrate. Nothing interactive sits inside the 24 px
  edge band; the ♥ and ⟳ buttons take the same inset treatment Weather's refresh
  button uses.
- `onBack()` → Board back to stop list, stop list back to chooser, chooser back to
  keypad, then default. One layer per press.

## 7. Attribution

A line at the foot of the Route tab: `Data: KMB/LWB and Citybus open data`. Public
feeds, credited where the user can see it, not only in a doc.

The app is modelled on a web app shared by a friend
(`reference/gorhkbus/index.html`). No code from it is reused and it carries no
license — see §8 of the proposal.
