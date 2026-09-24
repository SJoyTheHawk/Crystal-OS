# Hong Kong Bus App — UI/UX Design Document
**Date:** 2026-09-24  
**Status:** Draft  
**Purpose:** Define the complete user experience, page structure, and interaction patterns for Crystal OS Bus App v2

---

## Executive Summary

The first bus app implementation failed due to:
1. **Poor UI design**: Unclear navigation, overwhelming screens, no clear entry point
2. **Wrong program flow**: Downloaded full datasets on every refresh, blocking the UI
3. **No clear use case**: Tried to be everything (journey planner, map, search) on a 480×480 display

**The fix**: Design for the **one actual use case** — checking ETA for the 2-3 stops near where the device lives. Everything else is scaffolding to reach that state.

**Design philosophy** (from [hk-independent-bus-eta](https://github.com/hkbus/hk-independent-bus-eta)):
> "Clutter-free UI that lets you get what you need, fast."

---

## 1. App Structure Overview

### 1.1 Core Pages (4 total)

```
┌─────────────────────────────────────────────┐
│ App Entry Point                             │
└─────────────────────────────────────────────┘
              │
              ↓
┌─────────────────────────────────────────────┐
│ 1. FAVORITES (Landing Page)                 │
│    → Quick glance at saved stops            │
│    → Empty state: guide to Route Search     │
└─────────────────────────────────────────────┘
       │                    ↑
       │ (tab switch)       │
       ↓                    │
┌─────────────────────────────────────────────┐
│ 2. ROUTE SEARCH                             │
│    → Custom keypad (0-9, A-Z letters)       │
│    → Recent routes (tap to reuse)           │
│    → Leads to Stop Picker                   │
└─────────────────────────────────────────────┘
              │
              ↓
┌─────────────────────────────────────────────┐
│ 3. STOP PICKER (Modal/Overlay)              │
│    → List of stops for selected route       │
│    → "Find nearest" button                  │
│    → Leads to ETA Board                     │
└─────────────────────────────────────────────┘
              │
              ↓
┌─────────────────────────────────────────────┐
│ 4. ETA BOARD (Detail Page)                  │
│    → Live countdown for 1 stop              │
│    → Auto-refresh every 30s                 │
│    → Save/unsave button                     │
│    → Manual refresh button                  │
└─────────────────────────────────────────────┘
```

**Navigation model**: Tabs + linear drill-down, not a complex tree.

---

## 2. Page Designs

### 2.1 Page 1: FAVORITES (Landing Page)

**When shown:**
- Always the landing page after first use (when ≥1 stop saved)
- Accessible via tab bar anytime

**Purpose:** Immediate ETA check with zero interaction — the 80% use case.

```
┌──────────────────────────────────────────────┐
│  ★ Favorites   🔍 Search                  ⟳  │  ← Tab bar + refresh
├──────────────────────────────────────────────┤
│                                              │
│ ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓ │
│ ┃ 68X → Yuen Long                         ┃ │  ← Route + destination
│ ┃ Tsuen Wan Station                       ┃ │  ← Stop name
│ ┃ 3 min · 11 min · 24 min                 ┃ │  ← Next 3 buses
│ ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛ │
│                                              │
│ ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓ │
│ ┃ 960 → Wan Chai                          ┃ │
│ ┃ Tuen Mun Road Interchange               ┃ │
│ ┃ Due · 8 min · 19 min                    ┃ │
│ ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛ │
│                                              │
│ ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓ │
│ ┃ 264M → Yuen Long West                   ┃ │
│ ┃ Tsing Yi Railway Station                ┃ │
│ ┃ 15 min · 28 min                         ┃ │
│ ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛ │
│                                              │
│                                              │
│ Updated 40s ago                              │  ← Age indicator
└──────────────────────────────────────────────┘
```

**Interactions:**
- **Tap card** → Open ETA Board for that stop (page 4)
- **Long-press card** → Show "Remove from favorites?" dialog
- **Tap ⟳** → Refresh all favorites now (ignores 30s throttle)
- **Tap 🔍 Search tab** → Switch to Route Search (page 2)

**Card design:**
- Height: 80px (3 lines of content)
- Border: 1px slate hairline (#1E293B)
- Background: Dark navy (#0F172A)
- Radius: 8px
- Spacing: 12px between cards
- Max visible: 5 cards without scrolling (480px height - 64px tab bar - margins)

**Empty state** (first run, no favorites):
```
┌──────────────────────────────────────────────┐
│  ★ Favorites   🔍 Search                     │
├──────────────────────────────────────────────┤
│                                              │
│                                              │
│            🚌                                │
│                                              │
│      No saved stops yet                      │
│                                              │
│   Tap "Search" to find a bus route,         │
│   then save stops you check often.          │
│                                              │
│                                              │
│         ┌────────────────┐                   │
│         │  Find a route  │                   │  ← Button to Search tab
│         └────────────────┘                   │
│                                              │
└──────────────────────────────────────────────┘
```

**Limit:** 8 saved stops maximum (network budget + UX constraint)

**Why this works:**
- Zero-tap access to the most common action
- Scannable in <2 seconds
- Each card is self-contained info (no need to drill down for the 80% case)

---

### 2.2 Page 2: ROUTE SEARCH

**Purpose:** Enter a route number to find stops. Optimized for 1-4 character route names.

```
┌──────────────────────────────────────────────┐
│  ★ Favorites   🔍 Search                     │
├──────────────────────────────────────────────┤
│                                              │
│              ┌────────────────┐              │
│              │      68X       │              │  ← Input display (centered)
│              └────────────────┘              │
│                                              │
│           1    2    3    ⌫    ⏎             │  ← Number row (centered)
│           4    5    6                        │
│           7    8    9                        │
│                0                             │
│                                              │
│    ╔══════════════════════════════════════╗ │
│    ║  A  B  C  E  K  M  P  R  S  X  ...  ║ │  ← Scrollable letter strip
│    ╚══════════════════════════════════════╝ │     (centered container)
│                                              │
│  Recent:                                     │
│    ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐     │
│    │ 68X  │ │ 960  │ │264M  │ │ B3X  │     │  ← Recent routes (centered)
│    └──────┘ └──────┘ └──────┘ └──────┘     │
│                                              │
└──────────────────────────────────────────────┘
```

**Keypad behavior** (critical UX innovation):

1. **Adaptive key greying** (like official KMB app):
   - After typing `6`, only keys that can follow `6` in real routes stay lit
   - After typing `68`, only `A`, `E`, `F`, `M`, `P`, `R`, `X` stay lit (real 68X, 68A, etc.)
   - Uses the pre-loaded 6KB route name index (1,048 routes, ~6KB)
   
2. **Scrollable letter strip** (reduces visual clutter):
   - Numbers 0-9 stay in fixed grid (always visible)
   - Letters A-Z in horizontal scroll panel below
   - Only ~6-7 letters visible at once (reduces overwhelm)
   - Greyed letters stay visible at 30% opacity
   - Greyed letters are still tappable (in case index is stale)
   - Scroll position resets on each keystroke (most likely next chars scroll to view)

3. **⏎ Enter key**:
   - Fixed position in number row (always reachable)
   - Only lights when input is a complete route name
   - Can stay lit alongside other letters (e.g. `2` is complete, but `2A` also exists)

**Recent routes:**
- Last 6 routes, most recent first
- Persisted in NVS
- Tap to fill input and auto-submit

**After submitting:**
- **One company, one direction** → Directly open Stop Picker (page 3)
- **Multiple directions/companies** → Show direction chooser:
  ```
  ┌──────────────────────────────────────────┐
  │ Select Direction                         │
  ├──────────────────────────────────────────┤
  │ ┌──────────────────────────────────────┐ │
  │ │ 68X · KMB                            │ │
  │ │ Tsim Sha Tsui ▸ Yuen Long           │ │
  │ └──────────────────────────────────────┘ │
  │ ┌──────────────────────────────────────┐ │
  │ │ 68X · KMB                            │ │
  │ │ Yuen Long ▸ Tsim Sha Tsui           │ │
  │ └──────────────────────────────────────┘ │
  └──────────────────────────────────────────┘
  ```
- **Not found** → Show error in place:
  ```
  ┌────────────────┐
  │     68Y        │  ← Input stays
  └────────────────┘
  
  Route 68Y not found
  Check the number and try again
  ```

**Why this works:**
- Fixed number grid for muscle memory (most routes start with numbers)
- Scrollable letter strip reduces visual overwhelm (6-7 visible vs 18 letters in grid)
- Adaptive greying still works (scroll to see which letters are available)
- Recent routes reduce repeat typing to zero

---

### 2.3 Page 3: STOP PICKER (Modal Overlay)

**Purpose:** Choose one stop from a route (60-120 stops typical).

```
┌──────────────────────────────────────────────┐
│ ‹ Back   68X · KMB → Yuen Long              │  ← Header w/ back
├──────────────────────────────────────────────┤
│  1   Tsim Sha Tsui East                      │
│  2   Kowloon Park Drive                      │
│  3   Tsuen King Circuit                      │
│  4   Stop 4                            ···   │  ← Name loading
│  5   Castle Peak Road                        │
│  6   Stop 6                            ···   │
│  7   Tai Lam Tunnel Toll Plaza               │
│ ...                                          │
│ 56   Fuk Hi Street                           │
├──────────────────────────────────────────────┤
│         [ 📍 Find nearest stop ]             │  ← Action button
└──────────────────────────────────────────────┘
```

**Stop name loading strategy** (CRITICAL for perf):
- Rows show "Stop N" immediately (from route-stop sequence API, <2KB)
- Names load **lazily** as you scroll (viewport + 10-row lookahead)
- Names fetched over one kept-alive TLS connection
- Names cached in RAM (LRU, max 200 entries)
- `···` spinner shows until name resolves, then updates in place (no reflow)

**"Find nearest stop" button:**
- Requires location set in Settings
- Fetches coordinates for ALL stops on route (60-120 requests)
- Shows progress: `Checking stops… 34 of 118` with Cancel button
- Scrolls to nearest stop when done
- This is **opt-in**, not automatic (too expensive to do by default)

**Without location set:**
```
┌──────────────────────────────────────────────┐
│         [ 📍 Find nearest stop ]             │
│                                              │
│  Set your location in Settings › Region      │
│  to use this feature.                        │
└──────────────────────────────────────────────┘
```

**Why this works:**
- Instant list appearance (no waiting for 120 name fetches)
- Scrolling is never blocked
- Nearest-stop is a power feature, not a blocker

---

### 2.4 Page 4: ETA BOARD (Detail Page)

**Purpose:** Show live ETA for ONE stop, with auto-refresh.

```
┌──────────────────────────────────────────────┐
│ ‹ Back  68X → Yuen Long              ♥   ⟳  │  ← Heart = save/unsave
├──────────────────────────────────────────────┤
│                                              │
│   Tsuen Wan Station                          │  ← Stop name (large)
│   Stop 14 of 118                             │  ← Context
│                                              │
│   ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓   │
│   ┃  3 min          14:32              ┃   │  ← Next bus
│   ┃  11 min         14:40   Scheduled  ┃   │  ← 2nd bus + remark
│   ┃  24 min         14:53              ┃   │  ← 3rd bus
│   ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛   │
│                                              │
│   Updated just now                           │  ← Freshness indicator
│                                              │
└──────────────────────────────────────────────┘
```

**ETA display rules:**
- **<60s** → Show `Due` (not `0 min`)
- **<60min** → Show minutes + absolute time
- **>60min** → Show only absolute time (minutes become meaningless)
- **Operator remarks** → Show below time if present: `Scheduled`, `Last bus`, etc.
- **No ETAs** → Show "No departures scheduled" with age line

**Freshness states** (CRITICAL trust signal):
```
Updated just now              (< 30s, normal color)
Updated 2 min ago             (30s - 5min, normal color)
Updated 7 min ago · may be stale    (5-30min, dimmed numbers)
[only clock times shown]      (> 30min, remove minute counts)
Updated at an unknown time    (clock not set)
```

**Auto-refresh:**
- Every 30 seconds while visible
- Stops when app paused (no background requests)
- Manually triggered via ⟳ button (ignores throttle)

**Save button (♥):**
- Filled heart = already saved
- Empty heart = not saved
- Tap to toggle
- If favorites full (8/8): disable with toast "Saved stops full — remove one first"

**Network error handling:**
```
┌──────────────────────────────────────────────┐
│   ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓   │
│   ┃  3 min          14:32              ┃   │  ← Keep last good data
│   ┃  11 min         14:40              ┃   │
│   ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛   │
│                                              │
│   ⚠ Couldn't refresh · showing 4 min old    │  ← Error + age
│                                              │
└──────────────────────────────────────────────┘
```
**Never blank a good reading to report a bad request.**

**Why this works:**
- Both minutes + clock time (minutes = actionable, clock = verifiable if stale)
- Age always visible (trust model)
- Errors don't destroy information

---

## 3. Navigation & Gestures

### 3.1 Tab Bar (Top)
```
┌──────────────────────────────────────────────┐
│  ★ Favorites   🔍 Search                     │  ← 2 tabs only
├──────────────────────────────────────────────┤
```
- Always visible on pages 1-2
- Hidden on pages 3-4 (modal/detail pages)

### 3.2 Back Button Behavior
```
Page 4 (ETA Board)  ──Back──▸  Page 3 (Stop Picker)
Page 3 (Stop Picker) ──Back──▸  Page 2 (Route Search) or Direction Chooser
Direction Chooser   ──Back──▸  Page 2 (Route Search)
Page 2 or Page 1    ──Back──▸  Exit app (shell handles)
```

### 3.3 Gesture Support
- **Horizontal swipe (edge)** → App switcher (shell-owned)
- **Vertical swipe** → Scroll lists (LVGL default)
- **Long press** → Context actions (Remove from favorites)
- **Bottom edge** → Home pill (shell-owned, exits app)

---

## 4. Visual Design System

### 4.1 Color Palette
```
Background:       #0F172A  (deep navy)
Card background:  #1E293B  (slate)
Text primary:     #F8FAFC  (off-white)
Text secondary:   #94A3B8  (slate-400)
Border:           #334155  (slate-700, 1px hairline)
Accent (primary): #38BDF8  (sky blue, for active states)
Accent (warning): #F59E0B  (amber, for staleness)
Accent (success): #22D3EE  (cyan, for refresh success)
```

### 4.2 Typography
```
Route numbers:    28px, bold (large, scannable)
Stop names:       20px, medium (readable)
ETA times:        24px, bold (primary info)
Body text:        16px, regular
Captions:         14px, regular (age, metadata)
```
Font: Inter or system sans (no custom fonts yet, waiting for Phase 14)

### 4.3 Component Specs

**Favorite Card:**
- Height: 80px
- Padding: 16px
- Border-radius: 8px
- Border: 1px #334155
- Box-shadow: 0 1px 3px rgba(0,0,0,0.3)

**Number Button (Fixed Grid):**
- Size: 64×64px
- Border-radius: 8px
- Active state: #38BDF8 background
- Greyed state: 30% opacity
- Tap feedback: 120ms scale(0.95)

**Letter Button (Scrollable Strip):**
- Size: 56×56px
- Border-radius: 8px
- Horizontal spacing: 8px between buttons
- Container: horizontal scroll, no scrollbar
- Active state: #38BDF8 background
- Greyed state: 30% opacity
- Tap feedback: 120ms scale(0.95)

**Stop List Row:**
- Height: 52px
- Padding: 12px 16px
- Divider: 1px #334155
- Tap feedback: #1E293B background

---

## 5. States & Error Handling

### 5.1 Network States

| State | UI Behavior |
|-------|-------------|
| **No WiFi** | Show cached favorites with age, disable refresh, no spinners |
| **Timeout** | Keep last good data, show `⚠ Couldn't refresh · showing N min old` |
| **Empty response** | Show "No departures scheduled" (not an error, just no buses) |
| **404 on saved stop** | After 3 failures: show `Route may have changed` with Remove button |
| **API error** | One toast, cache untouched, retry on next tick |

### 5.2 Clock States

| State | UI Behavior |
|-------|-------------|
| **Clock set** | Show both minutes + absolute times |
| **Clock not set** | Show only minutes (from ETA timestamps' own arithmetic), hide clock column, age = "unknown" |

### 5.3 Loading States

| Operation | UI Treatment |
|-----------|--------------|
| **Initial favorites load** | Show cards with last cached ETAs + age immediately, refresh in background |
| **Stop names loading** | Show `Stop N` with `···` spinner, replace with name when ready (no reflow) |
| **ETA refresh** | Subtle spinner in ⟳ button, don't blank existing ETAs |
| **Nearest stop resolve** | Modal progress: `Checking stops… 34 of 118` with Cancel |

---

## 6. Data Architecture (UI Perspective)

### 6.1 What Loads When

```
App Launch (onCreate)
  ↓
Load 6KB route index to RAM (instant, from flash)
  ↓
Load saved favorites from NVS (instant)
  ↓
Render favorites page with cached ETAs + age
  ↓
(Background) Fetch fresh ETAs for all favorites
  ↓
Update cards as responses arrive
```

### 6.2 What's Cached Where

| Data | Storage | TTL | Size |
|------|---------|-----|------|
| Route name index | Flash (.rodata) | Refreshed weekly | ~6 KB |
| Recent routes | NVS | Forever | <100 B |
| Saved favorites (route+stop IDs) | NVS | Forever | <500 B |
| Stop names (LRU cache) | RAM | Session | ~10 KB |
| Last ETA per favorite | NVS | Session | ~2 KB |

**Rule:** Never fetch >2MB datasets. Always use APIs incrementally.

---

## 7. Comparison: Old vs New Design

| Aspect | Old (Failed) | New (This Doc) |
|--------|--------------|----------------|
| **Landing page** | Route search? Map? Unclear | Favorites (80% use case) |
| **Data loading** | Download 3MB on launch | 6KB index in flash, lazy API calls |
| **Stop names** | All 120 upfront (blocked) | Lazy load as you scroll |
| **Nearest stop** | Automatic? Unclear | Explicit opt-in with progress |
| **Network errors** | Spinners? Crashes? | Degrade gracefully, keep cached data |
| **Navigation** | Deep nesting? | 2 tabs + 2 detail pages max |
| **Primary flow** | Multi-step discovery | One-tap favorites |
| **Exit criteria** | None stated | "Can I check my bus in <5s?" |

---

## 8. Success Metrics

### 8.1 Performance Gates
- **First render** → <80ms from onCreate
- **Route search** → Keypad responsive <16ms per keystroke
- **Stop list** → First 20 rows visible <200ms
- **ETA fetch** → Results shown <2s (network dependent)
- **Memory** → <400 KB peak PSRAM, <60 KB heap

### 8.2 UX Gates
- **Cold start to first ETA** → <3 taps after favorites set up
- **Repeat check** → 1 tap (open app, favorites auto-refresh)
- **Find new stop** → <10 taps (search → pick stop → save)
- **Error recovery** → No blank screens, always show last good data

---

## 9. Open Design Questions

1. **Language**: English-only for v1 (Phase 9.7), or TC/EN toggle?
   - **Recommendation**: English-only until Phase 14 (font assets are 0.6-2.5MB)
   
2. **Favorites sorting**: Manual order, or always by distance?
   - **Recommendation**: Manual order (preserves user's mental model), with optional "Nearby" view
   
3. **Long route names**: Some CTB routes have long destinations (>30 chars)
   - **Recommendation**: Truncate with ellipsis, show full on detail page
   
4. **Night mode**: Auto-dim after sunset?
   - **Recommendation**: Defer to system brightness (Weather doesn't have night mode either)

5. **Offline mode**: Show "Last updated N days ago" for week-old cache?
   - **Recommendation**: Yes, with warning after 24h: `⚠ Last updated 3 days ago`

---

## 10. References

- [hk-independent-bus-eta](https://github.com/hkbus/hk-independent-bus-eta) — Design inspiration, "clutter-free" philosophy
- [hk-bus-crawling](https://github.com/hkbus/hk-bus-crawling) — Data normalization patterns
- Phase 9.7 documents — Technical architecture, scope decisions
- Crystal OS Design.md §10 — Type scale, motion, gestures

---

## 11. App Icon Design

### 11.1 Reference Analysis (Official KMB App)

The official KMB app icon features:
- **Color:** Bold red (#E31E24 or similar KMB brand red)
- **Subject:** Front view of double-decker bus (simplified, white silhouette)
- **Style:** Flat, iconic, high contrast
- **Identifier:** Route number "1933" below bus (KMB founding year)
- **Shape:** Rounded square with generous padding

### 11.2 Crystal OS Bus App Icon Proposal

**Constraints:**
- 64×64px procedural generation (`bus_icon.c`, like `clock_icon.c`)
- Must work on dark launcher background
- No external assets (all drawn with LVGL primitives)

**Design: Simplified Bus Front View**

```
┌─────────────────────────────────┐
│         (64×64 canvas)          │
│                                 │
│      ┏━━━━━━━━━━━━━━━┓         │  ← Bus outline (white)
│      ┃  ▪ ▪     ▪ ▪  ┃         │  ← Headlights (amber/yellow)
│      ┃ ┌───┐   ┌───┐ ┃         │  ← Windows (light blue)
│      ┃ │   │   │   │ ┃         │
│      ┃ └───┘   └───┘ ┃         │
│      ┃ ┌─────────┐   ┃         │  ← Windshield
│      ┃ │         │   ┃         │
│      ┃ └─────────┘   ┃         │
│      ┗━━━━━━━━━━━━━━━┛         │
│         ◉       ◉              │  ← Wheels (dark gray)
│                                 │
└─────────────────────────────────┘
```

**Color Palette:**
```c
// bus_icon.c
#define ICON_BG_COLOR      lv_color_hex(0x1E293B)  // Slate (launcher bg)
#define ICON_BUS_BODY      lv_color_hex(0xE31E24)  // KMB red
#define ICON_BUS_OUTLINE   lv_color_hex(0xF8FAFC)  // Off-white
#define ICON_WINDOW        lv_color_hex(0x38BDF8)  // Sky blue (glass tint)
#define ICON_HEADLIGHT     lv_color_hex(0xFBBF24)  // Amber
#define ICON_WHEEL         lv_color_hex(0x334155)  // Dark slate
```

**Implementation Notes:**
- Bus body: filled rounded rectangle (48×40px, centered)
- Outline: 2px white stroke
- Windows: 2 small rectangles (8×12px each) + 1 large windshield (20×10px)
- Headlights: 4 small circles (3px radius, amber)
- Wheels: 2 circles (6px radius, bottom edge)
- No text (too small at 64px, route number in app name instead)

**Alternative (Simpler):**
- Single solid red rounded square with white bus silhouette (no interior details)
- Easier to render procedurally, still recognizable at small size

### 11.3 App Name Display

**In Launcher:**
```
┌─────────┐
│  [Icon] │  ← 64×64 bus icon
├─────────┤
│   Bus   │  ← App name (16px)
└─────────┘
```

**In App Switcher (Recent Apps):**
- Snapshot shows current page (Favorites or ETA Board)
- Icon in corner for identification

---

## 12. Design Revisions from Phase 9.7

### 12.1 Changes from Original Transit Design

| Aspect | Phase 9.7 Design | This Design (v2) |
|--------|------------------|------------------|
| Tabs | 3 tabs (Saved, Route, Nearby) | 2 tabs (Favorites, Search) |
| Keypad | Fixed grid (all chars visible) | Scrollable letter strip (less clutter) |
| Nearby tab | Saved stops by distance | Removed (add later if needed) |
| Empty state | Text prompt only | Text + CTA button to Search |
| Favorite limit | 8 (stated) | 8 (enforced with UI feedback) |
| Language | English only | User-selectable (EN/TC in Settings) |

### 12.2 Rationale for Changes

**Why 2 tabs instead of 3:**
- "Nearby" tab requires all stops to have coordinates cached (expensive)
- Can be added later as a sort option within Favorites
- Simpler navigation = faster to learn

**Why scrollable keypad:**
- User feedback: fixed grid with 18+ buttons felt "overwhelming"
- Mobile apps (official KMB, hkbus) use scrollable strips
- Numbers stay fixed (muscle memory), letters scroll (exploration)

**Why user-selectable language:**
- Traditional Chinese requires 0.6-2.5MB font assets (Phase 14)
- Store one language per user, not both (memory constraint)
- Settings toggle: `Language → English / 繁體中文`

---

## 13. Next Steps

1. **Implement procedural icon** (`bus_icon.c`) and validate on device
2. **Review this UI/UX design** with the team
3. **Build scrollable keypad prototype** (validate that scroll is better than grid)
4. **Draft implementation plan** (break into 5-7 sub-phases)
5. **Prototype favorite card** → ETA board flow (the 80% path)

---

**Exit question**: "Can a user check when their bus arrives in under 5 seconds on a cold start, after one-time setup?"

If yes → ship it.  
If no → the design has failed its job.
