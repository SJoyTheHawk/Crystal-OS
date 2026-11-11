# Crystal OS App Platform — v2 Implementation Plan

**Version:** 2.0  
**Status:** Phase 14 closed; Phases 15–17 pending  
**Date:** 2026-11-11  
**Scope:** Package format, registry system, control panel, and three runtime tiers (declarative, script, native)

**Versioning note:** Crystal OS v1 is Phases 1-13 — the baseline OS with bundled Clock, Weather, and Calculator apps. Crystal OS v2 is Phase 14 and beyond — the app platform and publication release. All documents in this series describe v2.

---

## 1. Core Architecture Decisions

### 1.1 Design Principles

**Resource cost is the primary constraint.** The ESP32-S3 has ~100KB usable internal DRAM after WiFi, 8MB PSRAM, and 16MB flash. Every app platform feature must justify its cost in bytes and clock cycles.

**Apps are immutable at the binary level, mutable at the state level.** A `.capp` package installs to `/apps/<id>/` as read-only code/assets and writable `data/` for state. Deleting the folder is a complete, clean uninstall.

**Animation capability emerges from resource discipline, not from raw FPS.** The bundled Clock, Weather, and Calculator apps demonstrate what smooth, responsive UX looks like on this hardware. Package apps must respect the same frame budget and input latency constraints.

**The ABI is the product.** Runtime tiers (declarative, script, native) are swappable implementations of one narrow handle-based API. Changing the ABI is a major version bump; adding to it is a minor version bump.

### 1.2 Settled Decisions (No Reopening)

1. **Manifest-driven, not auto-discovered.** Every app declares its entry point, permissions, and resource budget in `app.yaml`. The device reads `app.json` (compiled manifest).
2. **Reverse-DNS app IDs.** `com.example.myapp` prevents collisions in an open catalog. Display name comes from manifest, not folder name.
3. **LittleFS for `/apps` partition, SPIFFS for `/storage`.** LittleFS survives repeated install/delete cycles; SPIFFS does not.
4. **No app-to-app IPC in v1.** Apps are isolated. System services are the only cross-app boundary.
5. **Bundled apps stay bundled.** Clock, Weather, Calculator compile into the firmware as `origin = BUILTIN` and use `CrystalState` NVS directly. Package apps use `app.yaml` and `/apps/<id>/data/state.json`.
6. **Script is the untrusted tier, native is curated.** No MMU on the S3, so native apps run in kernel memory. Only vetted first-party apps load as native.

---

## 2. On-Device Storage Layout

```
/spiffs/                          (storage partition, 2 MB)
  assets/
    wallpaper.bin
    icons/                        (bundled app icons, LVGL binary format)
    fonts/

/littlefs/                        (apps partition, 5.5 MB)
  apps/
    com.example.clock3p/
      app.json                    (read-only, manifest converted from YAML)
      main.lua                    (entry point, read-only)
      assets/                     (app-provided icons, images, sounds)
        icon.bin
      data/                       (writable, quota'd)
        state.json                (auto-created on first write)
    
    com.example.weather/
      (same structure)
    
    .staging/                     (in-flight install; pruned at boot if present)
      app.json
      main.lua
      assets/
      data/
        manifest.crc32            (marker)

/nvs/                             (NVS partition)
  CrystalAppEntry[] registry      (installed apps metadata)
  CrystalState/<app_id>:*         (package app state keys, namespaced)
  CrystalState/*                  (bundled app state keys, not namespaced)
```

---

## 3. Manifest Schema

**Source: `app.yaml`** (authored by developer)  
**Compiled: `app.json`** (read by device)

```yaml
# Metadata
abi: 1                            # Host API major version; refuse install if unsupported
id: com.example.clock3p           # Reverse-DNS; must match folder name
name: Clock                       # Display name in launcher
version: 1.0.0                    # Semver; shown in version info
min_os: 1.0.0                     # Minimum OS version required
author: Example Co.
license: MIT
description: "A simple clock app with timer and alarm"

# Runtime and entry
runtime: lua                      # lua | declarative | native
entry: main.lua                   # Module/file to execute first
icon: assets/icon.bin             # LVGL binary image, 96×96 or 128×128

# Visual configuration
bar_style: dark                   # Status bar contrast: dark | light (DESIGN.md §2)

# Resource budget (admission gate)
memory_kb: 160                    # Peak PSRAM used by this app instance
storage_kb: 32                    # Persistent data quota under data/
worker_threads: 0                 # Background tasks (0 = UI thread only; 1-2 for network/compute)

# Permissions (absent = denied)
permissions:
  notify: true                    # Can call crystal.service.notify.toast/chime
  audio: true                     # Can call crystal.service.notify.chime (requires speaker)
  system_timer: true              # Can control OS countdown timer
  network_hosts:                  # HTTPS/HTTP allowlist; empty = no network
    - api.open-meteo.com
    - data.example.com
  location: false                 # Can read lat/long from Settings › Region & Time

# Optional: lifecycle hints (for future phases)
onStart_budget_ms: null           # Reserved for Phase 8 (overlay lifecycle)
onStop_budget_ms: null
```

**Key enforcement points:**
- `abi: 1` is the gate. A manifest with `abi: 2` refuses to install on v1 hardware.
- Permissions are additive only: `network_hosts: []` means no network access at all.
- Resource fields are admission gates: install fails if app's `memory_kb + storage_kb` exceeds remaining capacity.
- `worker_threads` is validated when `network_hosts` is non-empty: must be >= 1 to use `http.fetch`.

---

## 4. Package Format: `.capp`

**Transport-only binary format.** Streamable front-to-back (no central directory), optimized for USB serial delivery where RAM is constrained.

```
magic "CAPP" | u16 fmt_ver | u32 manifest_len | manifest.json
entries[] : u16 path_len | path | u32 len | u32 crc32 | bytes
trailer   : u8 sig_alg | ed25519 signature over all bytes above | u8 key_id
```

### 4.1 Format Specification

| Field | Type | Notes |
|-------|------|-------|
| `magic` | char[4] | "CAPP" (0x4341 5050) |
| `fmt_ver` | u16 | Format version; currently 1. Unrecognized versions are rejected. |
| `manifest_len` | u32 | Byte count of JSON manifest (big-endian) |
| `manifest` | u8[] | UTF-8 JSON, no compression (so LVGL images can be read from flash in place) |
| `entries[]` | TLV stream | Repeating: path_len, path, data_len, crc32, bytes |
| `sig_alg` | u8 | 0x01 = Ed25519 |
| `signature` | u8[64] | Ed25519 signature over magic + fmt_ver + manifest_len + manifest + entries |
| `key_id` | u8 | Key index; host maintains table of known developer keys |

### 4.2 Packing

```bash
crystal pack ./com.example.clock3p/
# Reads app.yaml, converts to app.json
# Hashes each file under the folder
# Signs with developer's Ed25519 private key
# Emits: com.example.clock3p-1.0.0.capp (~50-150 KB for typical app)
```

### 4.3 Installation Flow

1. **Stream to `/apps/.staging/`** — Write each entry, verify CRC on arrival.
2. **Verify manifest** — Check `abi`, `id`, `memory_kb` sum against available resources.
3. **Verify signature** — Reject if key_id unknown and Developer Mode off.
4. **Atomic install** — Rename `/apps/.staging/` → `/apps/<id>/`; write NVS registry row.
5. **Boot reconcile** — If `.staging/` exists at next boot, prune it (power-fail recovery).

---

## 5. Registry and Boot Reconciliation

### 5.1 Registry Entry Structure

```c
typedef enum {
    CRYSTAL_ORIGIN_BUILTIN,   // Clock, Weather, Calculator
    CRYSTAL_ORIGIN_PACKAGE,   // Third-party .capp
} crystal_app_origin_t;

typedef struct {
    char id[64];                         // com.example.myapp
    uint16_t slot;                       // Launch order (0-255)
    bool enabled;                        // User can disable/enable
    crystal_app_origin_t origin;
    uint32_t version;                    // From manifest
    uint32_t installed_at;               // Timestamp
    uint16_t error_count;                // Consecutive launch failures
    char details[256];                   // Error message or metadata
} CrystalAppEntry;
```

### 5.2 Boot Sequence

```
1. Load compiled-in app table
   └─> For each: create NVS row with origin=BUILTIN, enabled=true by default

2. Scan /littlefs/apps/ for app.json files
   └─> Parse manifest, create entries with origin=PACKAGE

3. Reconcile
   ├─> Folder exists, row exists
   │   └─> Match by ID; update row.origin from folder
   ├─> Folder exists, row missing
   │   └─> Newly sideloaded; assign next free slot, enabled=true
   ├─> Row exists, folder missing
   │   └─> Deleted behind OS's back; prune row (unless origin=BUILTIN)
   └─> BUILTIN row, no folder
       └─> Keep (builtin apps are in firmware)

4. Sort by slot, call installApp() for enabled=true apps in order
```

### 5.3 Lifecycle Consequences

- **Hand-deleting a folder over USB is a legitimate uninstall.** Boot reconcile notices and cleans the registry.
- **Disable vs. Uninstall:** Bundled apps show "Disable" button; package apps show "Uninstall" button.
- **Reordering:** Drag-reorder in Manage Apps writes two `u16` slot values to NVS (fast, atomic).
- **Error tracking:** Three consecutive launch failures → auto-disable with notice in Manage Apps.

---

## 6. Host ABI v1

**Core principle:** Handles, not pointers. Every object is a `u32` index into a per-app table. A fabricated or stale handle is a caught error, not a jump into garbage.

All handles die at `onDestroy()`. Apps rebuild their UI trees in `onCreate()` on every launch (no saved state tree).

### 6.1 `crystal.ui` — Tree Building

| Function | Returns | Notes |
|----------|---------|-------|
| `screen()` | handle | Root container for this app's visual area. Always available. |
| `container(parent, opts)` | handle | Flex or grid layout. Opts: `{flex_direction, flex_wrap, gap}`. flex_direction: `"row"` \| `"column"`. |
| `row(parent, opts)` / `col(parent, opts)` | handle | Shorthand for horizontal/vertical flex. Same opts as container. |
| `scroll_view(parent, opts)` | handle | Vertically scrollable container for mixed-height children. Opts: `{width, height}`. Use for settings pages, detail views, long-form content. |
| `label(parent, opts)` | handle | Static or dynamic text. Opts: `{text, font, color, align}` |
| `button(parent, opts)` | handle | Interactive button. Opts: `{text, on_click, style}`. Min height 44px. |
| `image(parent, opts)` | handle | Raster image from bundle. Opts: `{src, scale}`. `src` is bundle-relative path under `assets/`. Network images not supported in v2; download to `data/` via `http.fetch` and pass the `data/` path. |
| `list(parent, opts)` | handle | Scrollable list of uniform or rich items. Opts: `{item_height}`. See list API below. |
| `slider(parent, opts)` / `switch(parent, opts)` / `arc(parent, opts)` | handle | Input controls. Opts: `{value, min, max, on_change}` |
| `chart(parent, opts)` | handle | Data visualization. Opts: `{type, on_click}`. `type`: `"line"` \| `"bar"` \| `"scatter"`. See chart API below. |
| `textarea(parent, opts)` | handle | Text input field. Opts: `{placeholder, max_length, on_change, on_focus}`. Keyboard auto-shown on focus. |
| `tabview(parent, opts)` | handle | Tab bar container. Opts: `{tab_labels: ["Tab1", "Tab2"]}`. Tab labels set at creation; not changeable after. |
| `tab(tabview, index)` | handle | Individual tab container. `index` is 0-based. Must be created in order. |
| `spinner(parent, opts)` | handle | Animated loading indicator. Opts: `{size, color}`. Rotates continuously until hidden or deleted. Use for lazy-loading states. |
| `dialog(opts)` | handle | Modal confirmation dialog. Opts: `{title, message, buttons: [{text, on_click}]}`. Auto-dismissed on any button press. Max two buttons. |
| `delete(handle)` | void | Destroy object and children. Triggers `on_delete` callback if registered. Returns nil. |

**List item API:**

```
list_add(list, item)         item is string (text-only) or {icon, primary, secondary, on_click}
list_insert(list, idx, item) insert at 0-based index
list_remove(list, idx)       remove item at index
list_clear(list)             remove all items
list_count(list)             return current item count
```

`icon` in a rich list item is a bundle-relative path (`assets/icon.bin`). `secondary` is subtitle text; can be nil.

**Chart series API:**

```
chart_add_series(chart, color)         returns series handle
chart_set_data(chart, series, values)  values is array of numbers
chart_set_range(chart, min, max)       y-axis range; auto if not set
chart_clear(chart)                     remove all series data
```

### 6.2 Properties and Layout

| Function | Parameters | Notes |
|----------|------------|-------|
| `set_text(h, text)` / `get_text(h)` | handle, string | Get/set label or button text. |
| `set_value(h, n)` / `get_value(h)` | handle, number | Get/set slider, arc value. |
| `set_hidden(h, bool)` | handle, bool | Hide/show object without deleting. |
| `set_enabled(h, bool)` | handle, bool | Enable/disable interaction. Greyed out when disabled. |
| `set_focus(h)` | handle | Move input focus to widget. Required for programmatic keyboard show/textarea focus. On keyboard teardown, host sends DEFOCUSED automatically (see known issue §13). |
| `set_style(h, tbl)` | handle, table | Whitelisted style keys: `bg_color`, `text_color`, `radius`, `padding`, `opacity`, `font_size`, `border_width`, `border_color`, `align`. All colors are `0xRRGGBB` integer (e.g., `0xFF4040` for red). Opacity is 0–255. Padding is pixels. |
| `scroll_to(h, opts)` | handle, table | Scroll to make object visible. Opts: `{animate: bool, align: "top"\|"center"\|"bottom"}`. |
| `set_parent(h, new_parent)` | handle, handle | Reparent object (rare; mostly for dynamic lists). |
| `get_size(h)` | handle | Returns `{w, h, x, y}` in screen coordinates. |
| `on(h, event, fn)` | handle, string, function | Register event callback. Events: `click`, `long_press`, `change`, `focus`, `blur`, `scroll`, `delete`. Returns nil if handle invalid. |
| `off(h, event)` | handle, string | Unregister callback for event. |
| `timer(period_ms, fn)` / `timer_delete(h)` | u32, function | LVGL timer; fires repeatedly. Returns timer handle. Timer is automatically deleted at `onDestroy()`. |
| `after(delay_ms, fn)` | u32, function | One-shot callback after delay. Returns timer handle. |

**On failure:** All `crystal.ui.*` functions return `nil` on failure (invalid handle, invalid args). Check for nil before using returned handle.

**Fonts are named, not numeric:**
- `small` (16px)
- `medium` (20px)
- `large` (28px)

Changing the compiled font set never breaks an app.

### 6.3 `crystal.ui` — Animations

**Principle:** Apps should feel as alive as the bundled Clock, Weather, and Calculator. These two calls are enough to match the system's motion language without exposing raw LVGL animation internals.

| Function | Parameters | Notes |
|----------|------------|-------|
| `anim(h, props, duration_ms, opts)` | handle, table, u32, table | Animate one or more style properties from their current value to the target. `props` is the same subset as `set_style`: `opacity`, `bg_color`, `text_color`, `radius`. `opts`: `{easing: "ease_in"\|"ease_out"\|"ease_in_out"\|"linear", delay_ms: 0, on_done: fn}`. Returns anim handle. |
| `anim_cancel(anim_h)` | anim handle | Stop animation immediately at current value. |

**Easing defaults:** `ease_out` for most transitions; `ease_in_out` for looping effects.

**Standard motion recipes for package apps:**

```
Fade in:    anim(h, {opacity=255}, 200, {easing="ease_out"})
Fade out:   anim(h, {opacity=0},   200, {easing="ease_in"})
Pop:        anim(h, {opacity=255}, 150, {easing="ease_out"})
            then: anim(h, {radius=12}, 150, {easing="ease_out"})
Highlight:  anim(h, {bg_color=0x4A90D9}, 100, {easing="ease_in_out"})
            on_done: anim(h, {bg_color=original}, 300, {easing="ease_in_out"})
```

**What is not in the ABI:** Slide/translate animations (position-based). Moving objects by X/Y requires direct LVGL access that is too pointer-heavy for the ABI boundary. Use `set_hidden` + fade instead to achieve appear/disappear effects cleanly.

**Cost:** Each `anim()` call adds one LVGL animation entry (~80 bytes). Clean up with `anim_cancel` if the app is destroyed before the animation completes — the host does not auto-cancel in-flight animations at `onDestroy()` (they are LVGL-owned, not handle-owned).

### 6.3 `crystal.service` — System Services

| Function | Returns | Notes |
|----------|---------|-------|
| `time.now()` | number | Epoch seconds (Unix timestamp). |
| `time.format(fmt)` | string | Format time as string. Fmt is strftime format. Respects OS timezone. |
| `time.timezone_name()` | string | E.g., "HKT" or "PST". Read-only. |
| `timer.arm(seconds)` / `timer.start()` / `timer.pause()` / `timer.resume()` / `timer.reset()` | bool | Control OS countdown timer. Can be called from any app; affects system UI. Returns success. |
| `timer.status()` | table | Returns `{state, remaining_sec, total_sec}`. State: `idle`, `ready`, `running`, `paused`, `finished`. |
| `notify.toast(text)` | void | Non-interactive popup. Holds 2.5s automatically. Permission: `notify: true` |
| `notify.chime()` | bool | Play system chime. Permission: `audio: true`. Returns false if speaker unavailable. |
| `http.fetch(url, opts, cb)` | u32 (request_id) | Async HTTP GET/POST. Opts: `{method, headers, body, timeout_sec}`. Callback: `(request_id, status, body)`. Returns nil on failure (worker_threads: 0, permission denied, invalid URL). URL must be in `network_hosts` allowlist. **Response body limit:** 64 KB for script tier; larger responses are truncated and `status` is set to -1. Apps that need larger payloads (catalog sync, large datasets) should use native tier or stream to `data/` via host-managed download (future API, not in v2). |
| `http.cancel(request_id)` | void | Abort in-flight request; suppress callback. |
| `net.status()` | table | Returns `{state, ssid, ip}`. State: `disconnected`, `connecting`, `connected`. Read-only; no credentials. |
| `location.get()` | table | Returns `{lat, long}`. Read from Settings › Region & Time. Permission: `location: true`. Returns nil if not set. |
| `system.info()` | table | Returns `{os_version, abi_version, free_psram_kb, free_storage_kb}`. |

**Key contracts:**
- `http.fetch` is **async only.** No blocking calls; script runs on LVGL task and must not stall it.
- `timer.*` is **global and persistent.** If app armstimer in `onCreate` and is destroyed before it fires, the timer still fires and shows a toast to the system.
- `http.cancel` suppresses the callback; if app is destroyed mid-flight, host cancels automatically.

### 6.4 `crystal.state` — Persistent Storage

| Function | Notes |
|----------|-------|
| `get(key)` | Returns value or nil. |
| `set(key, value)` | Persist value. Nil erases key. For script apps, written to `/apps/<id>/data/state.json`. For bundled apps, written to NVS. |
| `erase(key)` | Explicit erase (same as `set(key, nil)`). |
| `flush()` | Force write to disk. Implicit at `onPause()`. |

**Data types:** Numbers, strings, booleans, and tables — including **nested tables**. The backing store for script apps is `data/state.json` (not NVS), so full JSON nesting is supported. Arrays of objects, nested records, etc. are all valid values.

```lua
-- Supported: array of objects
crystal.state.set("favorites", {
    {name = "Route 1", id = "kmb_001", stop = "ABC"},
    {name = "Route 2", id = "ctb_002", stop = "DEF"},
})

-- Supported: nested config
crystal.state.set("settings", {
    display = {brightness = 80, theme = "dark"},
    network = {timeout = 10},
})
```

**Limitation:** Functions, userdata, and metatables are not serializable and will raise an error if passed to `set()`. Keep values to plain data.

**NVS note for bundled apps:** Bundled apps write to NVS via `CrystalState`, which uses NVS blobs and does not support nesting. Bundled apps that need nested data should serialize with `crystal.json.encode` and store as a string.

**Namespace isolation for script apps:**
- When script calls `state.set("fav_data", {...})`, the host writes to `data/state.json` under the key `fav_data` (no NVS prefix needed; file is already app-isolated).
- On uninstall, the entire `data/` folder is deleted.
- Bundled apps still use NVS with automatic `<app_id>:` prefix.

### 6.4a `crystal.json` — JSON Encode/Decode

Bundled in the script runtime. Available to all script apps without any import.

| Function | Returns | Notes |
|----------|---------|-------|
| `crystal.json.encode(value)` | string | Encode Lua value (table, number, string, bool, nil) to JSON string. Raises error if value contains functions or userdata. |
| `crystal.json.decode(str)` | value | Decode JSON string to Lua value. Returns nil on parse error (does not raise). |

**Primary use cases:**
1. Parsing HTTP response bodies from `http.fetch`.
2. Serializing complex objects for `crystal.fs.write` (caching large datasets to `data/`).
3. Bundled apps using NVS that need nested structures.

```lua
-- Parse HTTP response
local data = crystal.json.decode(body)
if data == nil then
    -- handle parse error
end

-- Cache to file
crystal.fs.write("data/cache.json", crystal.json.encode(route_list))

-- Reload from file
local cached = crystal.json.decode(crystal.fs.read("data/cache.json"))
```

**Cost:** `cjson` (C implementation, ~8 KB flash). Shared across all script apps; not duplicated per app.

### 6.5 `crystal.fs` — File Access

| Function | Notes |
|----------|-------|
| `read(path)` | Returns file contents as string. Path is bundle-relative; `..` rejected. Can read from `assets/` or `data/`. |
| `write(path, data)` | Write to `data/` only. Path must be under `data/`. Quota'd. |
| `exists(path)` | Check existence (bundle or data). |
| `list(dir)` | List files in `data/` or `assets/`. Returns array of filenames. |

**Constraints:**
- Absolute paths rejected.
- No access to `/apps/<other_id>/` or `/spiffs/`.
- `data/` writes count against `storage_kb` quota.

### 6.6 Lifecycle: Four States

Apps implement a Lua module returning a table of lifecycle hooks:

```lua
return {
    onCreate = function()
        -- Called every launch on a fresh tree.
        -- Budget: 80ms (simple apps), 200ms (standard), 500ms (data-heavy).
        -- Must build all UI here or defer explicitly with timer.
        return true  -- false is logged, not fatal.
    end,
    
    onResume = function()
        -- Called when app becomes visible.
        -- Safe to start timers, request updates.
        return true
    end,
    
    onPause = function()
        -- GUARANTEED write point before destruction or switch.
        -- Stop timers, cancel requests, write state.
        -- Called on every path including return-to-launcher.
        return true
    end,
    
    onDestroy = function()
        -- Clean shutdown.
        -- All handles are dead after this returns.
        -- Called after onPause (or skip onPause if app never resumed).
        return true
    end,
    
    onBack = function()
        -- Gesture callback: user swiped back.
        -- Return true to consume; false to pass to next app in stack.
        -- onBack fires without state change; no onPause until dismissed.
        return false
    end,
    
    -- Reserved for Phase 8 (overlay lifecycle):
    onStart = function() return true end,
    onStop = function() return true end,
}
```

**Critical contracts:**
- **`onCreate` runs every launch.** No saved state tree to restore. `max_running_num = 1` (only one app instance at a time in v2).
- **`onPause` is the only guaranteed write point.** It precedes `onDestroy` or any switch-away. Use it to save state.
- **Handles don't survive `onDestroy`.** Rebuild the entire UI in the next `onCreate`.
- **v2 dispatches four states:** `onCreate`, `onResume`, `onPause`, `onDestroy` (plus `onBack`). `onStart` / `onStop` are declared but **never called in v2.** Don't put required logic there. They are reserved for a future phase when overlays can fully cover a card.

**onCreate Budget Guidance:**

| Complexity | Budget | What Fits | Examples |
|------------|--------|-----------|----------|
| Simple | 80ms | Basic layout, 10-20 LVGL objects, no network checks | Clock, Calculator |
| Standard | 200ms | 30-50 LVGL objects, one lightweight network call, startup setup | Weather (network status) |
| Data-heavy | 500ms | 100+ LVGL objects, large dataset validation, multi-provider checks | Bus app (catalog validation + route merging) |

Budgets are warnings in debug builds, not hard limits. Exceeding budget is logged but doesn't block launch (same fix as earlier phases).

---

## 7. Runtime Tiers

Three tiers, one ABI. Tiers differ in trust level, resource cost, and performance:

### 7.1 Declarative Tier

**File:** `ui.json` + optional `bindings.json`

**For:** Dashboards, remotes, simple readouts, static UIs with live service values.

**Cost:** ~20 KB flash per app. No runtime interpreter; guaranteed predictable performance.

**Capability:** Fixed UI layout, data binding to `crystal.service` values (time, network status, timer state), event routing to system actions.

**Example:**
```json
{
  "screen": {
    "type": "container",
    "flex_direction": "column",
    "children": [
      {
        "type": "label",
        "text": "{{time.now | format '%H:%M:%S'}}",
        "font": "large"
      },
      {
        "type": "button",
        "text": "Start Timer",
        "on_click": "timer.arm(300)"
      }
    ]
  }
}
```

**Phase 16 deliverable.** Freeze ABI v1 at the end of this phase.

### 7.2 Script Tier

**File:** `main.lua` (Lua 5.4)

**For:** Real third-party apps. General-purpose computation, rich interactivity, background tasks.

**Cost:** ~150-200 KB flash per app, ~40 KB RAM per instance (app allocator + heap).

**Capability:** Full `crystal.*` API, timers, async HTTP, file I/O, state persistence.

**Sandbox:**
- Own `_ENV` per app. `os`, `io`, `package`, `require` of non-Crystal modules removed.
- Count hook: ~10K VM instructions → 50ms deadline per callback. Overrun raises error, app torn down.
- Allocator hook: Per-app memory ceiling from manifest `memory_kb`. Exhaustion is a Lua error, not system panic.
- Filesystem: `/apps/<id>/` read-only, `/apps/<id>/data/` quota'd, nothing else addressable.

**Phase 17 deliverable.**

### 7.3 Native Tier

**File:** Position-independent ELF (prebuilt binary, Xtensa architecture)

**For:** Curated first-party apps only. Complex algorithms, DSP, performance-critical code.

**Cost:** Varies; typically 50-200 KB for app + 8-16 KB loader/dispatch.

**Capability:** Full C/C++ access to `crystal_*` APIs. No sandboxing; runs in kernel memory.

**Safety model:** No MMU protection, so a bug corrupts the OS heap. Native apps are trusted, not isolated.

**Spike required:** Evaluate SPIRAM code loading, symbol table export, cleanup verification for unload/reload.

**Not v2 scope.** Deferred to Phase 18+.

---

## 8. Sandbox and Safety

### 8.1 Responsiveness Watchdog (Script Tier)

Script runs on the LVGL task. A runaway loop holds the LVGL lock and eventually trips the 5-second task watchdog. To prevent this:

1. **Count hook:** Lua VM calls a C function every ~10K instructions.
2. **Deadline check:** Per-callback deadline of 50ms. If exceeded, raise a Lua error.
3. **Teardown:** Error bubbles up, app calls `onDestroy`, user sees toast: *"App stopped responding."*
4. **OS stays up:** No watchdog reset; app is cleanly removed.

**Script behavior under watchdog stress test:**
```lua
-- This triggers the watchdog:
while true do end

-- Result: Lua error raised after ~50ms, app destroyed.
```

### 8.2 Memory Ceiling

Script apps have a per-app allocator:
- Draws from PSRAM against `memory_kb` declared in manifest.
- Exhaustion is a Lua error: `"not enough memory"` at allocation point.
- App tears down cleanly; OS continues.

Admission control at install time prevents overcommit:
```
sum(memory_kb across all installed apps) <= available_psram
```

### 8.3 Environment Isolation

Each script gets its own `_ENV`:
- `crystal.*` modules available.
- `math`, `string`, `table`, `coroutine` available (standard library).
- `os`, `io`, `package`, `require` removed (for untrusted code).
- `load`, `loadstring` removed (code injection risk).
- `os.time` re-provided as `crystal.service.time.now()`.

### 8.4 Filesystem Isolation

- `/apps/<id>/` is read-only (bundle).
- `/apps/<id>/data/` is writable and quota'd.
- `..` in paths rejected.
- Absolute paths rejected.
- No access to `/apps/<other_id>/` or system partitions.

### 8.5 Crash Containment

- **Three strikes:** Three consecutive launch failures → app auto-disabled.
- **Notice in Manage Apps:** User can re-enable or uninstall.
- **Disabled app doesn't load:** Slot reserved; user can delete and reclaim space.

---

## 9. Control Panel

Two panels (on-device + external), one API, identical JSON.

### 9.1 On-Device Manage Apps

Built into the launcher (DESIGN.md §9, Phase 13). User can:
- Reorder apps (drag-and-drop)
- Install via USB (browse `.capp`, tap Install)
- Uninstall package apps
- Disable/enable bundled apps
- Clear app data

No external tools needed.

### 9.2 External Panel (Web UI + CLI)

Serves a static web UI over WiFi or USB. Accessed via:
- **WiFi:** `http://crystal-<serial>.local` (mDNS)
- **USB:** Web Serial page (same browser URL via USB CDC-ACM)

Features:
- Browse device apps (merged list, origin shown)
- Install `.capp` files
- Stream app logs (SSE)
- Launch app in developer mode
- Clear app data
- View system info

### 9.3 HTTP API

```
GET    /api/v1/apps                  merged list with origin, version, enabled state
POST   /api/v1/apps                  stream .capp, verify, install
DELETE /api/v1/apps/{id}             uninstall (package) / disable (builtin)
PATCH  /api/v1/apps/order            [{id, slot}, ...]
POST   /api/v1/apps/{id}/enabled     {enabled: bool}
DELETE /api/v1/apps/{id}/data        clear persistent state
POST   /api/v1/apps/{id}/launch      launch in dev mode (if enabled)
GET    /api/v1/system                {os_version, abi_version, free_psram_kb, free_storage_kb}
GET    /api/v1/logs                  SSE stream of app stderr
```

**Security:**
- **Off by default** until Settings › Developer › Enable API is toggled.
- **6-digit pairing PIN** shown on device screen at enable time.
- **Bearer token exchange:** One-time PIN traded for 24h token.
- **LAN-only:** Never exposed to internet; not hardened for remote access.

### 9.4 Developer CLI

```bash
crystal pack ./com.example.myapp/
  # -> com.example.myapp-1.0.0.capp (~50-150 KB)

crystal install --usb ./com.example.myapp/
  # Push folder over USB, install unsigned (dev mode only)

crystal logs -f
  # Stream Lua errors and debug output

crystal run com.example.myapp
  # Install + launch + stream logs in one go
```

These are not conveniences; without them the edit-test loop is unusable.

---

## 10. Partition Table

**Current limitation:** v1's partition table doesn't have room for app storage. Repartitioning after devices ship means serial reflash of every unit.

**New partition table:**
```
# Name,     Type,    SubType,   Offset,  Size
nvs,        data,    nvs,       ,        0x6000
nvs_keys,   data,    nvs_keys,  ,        0x1000
otadata,    data,    ota,       ,        0x2000
phy_init,   data,    phy,       ,        0x1000
coredump,   data,    coredump,  ,        0x10000
ota_0,      app,     ota_0,     ,        4M       # was 5M
ota_1,      app,     ota_1,     ,        4M       # was 5M
storage,    data,    spiffs,    ,        2M       # OS assets (wallpaper, bundled icons, fonts)
apps,       data,    littlefs,  ,        5.5M     # App folders + state
```

**Why LittleFS for apps:**
- Real directories (not FAT-like sector chains).
- Power-fail safe (atomic writes).
- Excellent wear behavior under repeated install/delete.

**Why SPIFFS for storage:**
- OS assets written once at flash time.
- Simpler, smaller footprint.

---

## 11. Phases

Phases 14-17 build the app platform. Phase 14 is foundational; 15-17 are independent (can reorder if needed, but 14 first).

### Phase 14: Package Format, Registry, Resource Management

**Closure status (2026-11-11):** Phase 14 is closed at the user's request following the successful Calculator board run. Firmware builds and host integration passed, including 10 Calculator install/uninstall cycles and 100 launches with zero tracked leaks. The board passed 10 cycles, state restoration after reboot, and fresh state after uninstall/reinstall. The `crystal_service`/TLS watchdog timeout is carried forward for a separate investigation. Full Bus network testing, power-loss recovery, and manual touch/timing validation are recorded as deferred hardware follow-ups. See [Phase 14 implementation notes](PHASE_14_IMPLEMENTATION.md) for evidence, implementation adaptations, and the open follow-up list.

**Deliverables:**
1. `.capp` format and `crystal pack` CLI tool
2. LittleFS `apps` partition with boot reconciliation
3. Per-app resource handles and allocation tracking
4. Admission control: reject install when `memory_kb` sum exceeds available PSRAM
5. Transactional teardown verification: `crystal_app_verify_cleanup()` scans for leaked tasks, timers, queue handles
6. Namespaced NVS state: script app keys written as `<app_id>:key`; cleanup on uninstall
7. Convert Calculator to `.capp` and verify end-to-end install/launch/uninstall

**Exit criteria:**
- Calculator installs via `.capp`, launches 10 times, uninstalls cleanly
- `crystal_app_verify_cleanup()` reports zero leaked resources
- Uninstall correctly erases all NVS keys matching `<app_id>:*` prefix

**Bus app role:** Use as stress test for resource tracking. Host tests confirm that retained worker/queue ownership force-disables the app and blocks uninstall. The extracted Bus app is instrumented; its full network workload on the board is a deferred follow-up recorded in the closure notes.

### Phase 15: Control Panel, Signatures, Developer Tools

**Deliverables:**
1. HTTP API (9.3) over WiFi and USB CDC-ACM
2. Static web UI: app browser, installer, log viewer
3. Pairing PIN flow (6-digit exchange for bearer token)
4. Package signature verification (Ed25519)
5. `crystal` CLI: `pack`, `install --usb`, `logs -f`, `run`
6. Install progress bar for packages > 1 MB
7. HTTPS curated catalog using a signed JSON index and separately hosted,
   prebuilt signed `.capp` downloads (no server-side build service)

**Bus app evidence:** 943 KB stop catalogs alone; realistic data-heavy apps may be 2-5 MB. Progress visibility matters.

**Exit criteria:**
- `.capp` installs over WiFi and USB from same browser page
- API rejects all requests without valid bearer token
- On-device Manage Apps and external panel agree after edits
- Install rejects package when resource sum exceeds available capacity

### Phase 16: Declarative Runtime

**Deliverables:**
1. `DeclarativeApp : CrystalApp` class
2. `ui.json` + `bindings.json` parser
3. Widget facade implementation (§6.1-6.2)
4. Live data binding to `crystal.service` values
5. Convert Weather to declarative runtime (or keep as bundled; decision deferred)

**Exit criteria:**
- Declarative app renders and binds live values (time, network status)
- Survives switch-away/switch-back
- All three bundled apps' UIs expressible without ABI additions

**ABI v1 is frozen at end of Phase 16.** This is the last moment changing it is free.

### Phase 17: Script Runtime, Async HTTP, Worker Threads

**Deliverables:**
1. `ScriptApp : CrystalApp` class
2. Lua 5.4 sandbox (environment, count hook, allocator hook)
3. Per-app memory ceiling enforcement
4. Async `http.fetch(url, opts, cb)` with request ownership
5. Worker thread quota: limit `worker_threads` concurrent tasks per app
6. `data/` quota enforcement
7. `examples/clock3p/` as real third-party app

**Bus app evidence requirements:**
1. **Async HTTP pattern validated:** Request started before `onDestroy()` completes cleanly or cancels without crashing worker
2. **Worker thread quota:** Second `http.fetch` while first pending either queues or errors (spec decision)
3. **onCreate budget:** Tiered guidance (80/200/500ms) or lazy UI construction documented

**Exit criteria:**
- Script app runs; deliberate infinite loop produces toast + app death, not watchdog reset
- Timer armed by script fires after app destroyed
- Request started mid-flight is cleaned up on destroy (no leaked worker tasks)

---

## 12. Resource Budgets and Quotas

### 12.1 Per-App Budgets (from manifest)

| Field | Type | Limits | Gating |
|-------|------|--------|--------|
| `memory_kb` | u32 | 40-512 KB typical; max 2048 KB | Install-time admission control |
| `storage_kb` | u32 | 8-1024 KB typical; max 5120 KB | Install-time; `data/` enforced at write |
| `worker_threads` | u8 | 0-4; if `network_hosts` present, min 1 | Manifest validation |

### 12.2 System Totals

| Resource | Limit | Enforcement |
|----------|-------|-------------|
| Total PSRAM apps | 4-5 MB (out of 8 MB, after OS) | Admission control: `sum(memory_kb) <= available` |
| Total LittleFS apps | 5.5 MB (apps partition) | Admission control: `sum(memory_kb + storage_kb + folder overhead) <= 5.5 MB` |
| Total worker threads | CONFIG_FREERTOS_MAX_TASKS - (OS reserved) | Admission control: `sum(worker_threads) <= capacity` |

### 12.3 Sizing Guidance

```
Realistic ecosystem:
  - 100+ simple apps (Clock, Calculator, basic widgets): 30-50 KB each
  - 40-60 standard apps (Weather-class): 80-120 KB each
  - 3-4 data-heavy apps (Bus, transit, maps): 1-2 MB each
  
Realistic install capacity:
  - Mixed: 10-15 apps (5 simple + 8 standard + 2 data-heavy)
  - Simple-only: 100+ apps installed simultaneously
  - Data-heavy-only: 2-3 apps max (hits flash budget)
  
What doesn't fit:
  - Photo galleries > 5 MB
  - Games with large sprite sheets
  - Apps requiring offline map tiles
  
Solution: Stream large assets from network; don't bundle in package.
```

---

## 13. Animation and UX Considerations

### 13.1 Bundled Apps as Baseline

Clock, Weather, and Calculator set the animation and responsiveness baseline for all package apps. They demonstrate:

- **Frame budget discipline:** Animations complete within the render cycle; no stutters from app code.
- **Gesture responsiveness:** Touch latency < 50ms; swipes feel immediate.
- **Transition timing:** 200-400ms cross-dissolves for screen changes; 100-200ms for UI element transitions.
- **Idle animation:** Subtle breathing or pulse effects without burning CPU.

Package apps must respect the same budgets and transitions.

### 13.2 onCreate Performance Impact on UX

A long `onCreate` is felt as a frozen launcher:
- User taps app icon.
- Launcher fade-out begins.
- App `onCreate` runs (80-500ms depending on tier).
- Launcher fade-out completes, but app UI isn't visible yet.
- User perceives: 200-700ms black screen, then app appears.

**Recommended approach:**
- Build minimal UI upfront (80ms budget).
- Defer heavy operations: defer tab 2-5 construction until first tab switch.
- Show "Loading..." state while expensive work runs in background.

### 13.3 State Persistence Across Animations

When an app is switching away:
1. `onPause()` fires.
2. Transition animation runs (200-400ms).
3. `onDestroy()` fires.

State changes in step 2 don't affect the app. Write all persistent state in step 1.

### 13.4 Worker Threads and Animation Frames

The LVGL task (Core 1) does all rendering. Worker threads (Core 0) do I/O and heavy compute. When both cores are busy:
- Animations may skip frames (~7-8 FPS measured on panel with concurrent HTTP traffic).
- This is acceptable as long as transitions don't appear to stutter (60 FPS not required).

Apps should minimize Core 1 work: lazy UI construction, async data loading.

---

## 14. Known Limitations and Constraints

These are accepted constraints for v2. Documenting them prevents re-discussion and sets expectations for developers.

### 14.1 HTTP Response Body Size

**Limit:** 64 KB for script tier.

**Rationale:** Larger response bodies exhaust script app's memory ceiling or cause allocation failure during parse. The bus app's 943 KB catalogs proved this is a real workload, but exceeds script tier limits.

**Workaround:** Native apps can handle larger responses. Script apps can request server-side chunking or pagination (fetch N records at a time).

**Future phase:** Host-managed background download to `data/` with progress callback (not in v2).

### 14.2 Network Image Loading

**Limitation:** `crystal.ui.image` only supports bundle-relative paths (`assets/icon.bin`). Network URLs are not supported.

**Rationale:** Async image decode/cache requires ~40 KB per image and a caching layer we don't have yet.

**Workaround:** Apps can `http.fetch` image bytes, write to `data/icon_cache.bin`, and pass `data/icon_cache.bin` as the `src`. Manual cache management; no auto-expiry.

**Future phase:** `crystal.ui.image_remote(url, {cache_kb})` with host-managed cache (not in v2).

### 14.3 Keyboard Defocus Bug (LVGL 8.4)

**Issue:** LVGL's `textarea` never emits a DEFOCUSED event in LVGL 8.4, so keyboards stay blinking until manually dismissed. The memory note `lvgl-textarea-never-defocuses.md` documents this known issue from v1.

**Workaround for v2:** Host sends synthetic DEFOCUSED event on keyboard teardown (when user dismisses keyboard or switches away from app). Apps should not rely on DEFOCUSED to save state; use `onPause` instead.

**Resolution:** Upgrade to LVGL 9.x in a future phase, where the event is properly fired.

### 14.4 Animations Not Auto-Canceled on onDestroy

**Behavior:** `crystal.ui.anim()` creates LVGL animation entries that are LVGL-owned, not handle-owned. When an app is destroyed, in-flight animations continue until they complete or LVGL reclaims the objects.

**Consequence:** An app with a 500ms fade-out animation that is destroyed at 200ms will leave the animation running for the remaining 300ms, animating a now-dead object (safely ignored by LVGL).

**Workaround:** Apps that care should track anim handles and call `anim_cancel` in `onDestroy` for any unfinished animations.

**Rationale:** Auto-tracking every `anim()` call adds overhead. Most animations are short (< 300ms) and completing after destroy is harmless.

### 14.5 No Inter-App Communication in v2

**Limitation:** Apps are isolated. No IPC, no shared state, no message passing between apps.

**Rationale:** Simplicity and security. IPC requires permission model, API versioning, and lifecycle coordination across multiple apps (e.g., app A calls app B, which is not running yet).

**Workaround:** System services (`crystal.service.*`) are the only cross-app boundary. An app can write to `crystal.state` and another app can read it only if both explicitly coordinate on key names and formats. Not recommended.

**Future phase:** Intent-style IPC with permission gates (not in v2).

---

## 15. Open Questions and Future Phases

### 15.1 ABI Stability

**Decision needed:** How long does `abi: 1` live? Are two majors supported simultaneously?

Implications:
- `abi: 1` ≥ 12 months: dev-friendly, platform can evolve freely for a year.
- `abi: 1` + `abi: 2` simultaneously: harder to maintain, but third-party apps with long lifecycles don't break.

Recommend: `abi: 1` minimum 18 months; announce `abi: 2` requirements 6 months in advance.

### 15.2 Curated Catalog Transport — resolved before Phase 15

**Decision:** Publish a signed JSON catalog and prebuilt, signed `.capp`
packages on HTTPS static hosting. The user's computer-side catalog client
fetches the live listing and selected package, so curated listings and app
releases can change independently of firmware OTA. The downloaded `.capp` is
then installed onto the device through the developer transport. The project
does not build apps on a server; maintainers build, review, sign, and upload
each package and then publish an updated index.

“Static” describes the files and hosting model, not a catalog embedded in
firmware or an infrequently updated listing. The catalog URL and release public
keys are deployment configuration and remain unset until project hosting and
key ownership are established. The signed document format and host-side
workflow are defined in [APP_CATALOG_FORMAT.md](APP_CATALOG_FORMAT.md). Device
firmware verifies package signatures but does not yet fetch the online catalog.

### 15.3 Native App Isolation

**Decision needed before Phase 18:** Per-app static mutable state isolation vs. force-disable-on-uninstall.

Option A (strict coding rules):
- Native apps cannot use static mutable state or singleton services.
- Runtime verification: scan app's symbol table at load time.
- Clean unload guaranteed.

Option B (accept leaks):
- Native apps disable instead of uninstall.
- Uninstall only during reboot.
- Simpler to implement; acceptable for curated set.

### 15.4 App Lifecycle During OTA

**Decision needed:** Grace period for app cleanup, or forced termination?

Option A (graceful):
- OTA pending → call `onPause()` + `onDestroy()` on running app.
- Wait up to 1000ms for graceful shutdown.
- If not done: force kill and proceed.
- Risk: OTA abort if app's `onDestroy` returns false.

Option B (forced):
- OTA pending → immediately kill running app.
- No cleanup callback.
- Simpler; acceptable since OTA is rare and user is prepared for reboot.

---

## 16. Checklist for Implementation

### Phase 14 Checklist

Closed on 2026-11-11. Checked items reflect the implemented Phase 14 scope;
[closure notes](PHASE_14_IMPLEMENTATION.md) describe the logical NVS namespace,
curated native resource contract, validation evidence, and deferred follow-ups.

- [x] LittleFS apps partition created; empty factory image generated and mounting confirmed on the board
- [x] `.capp` format spec implemented and tested with Calculator
- [x] `crystal pack` CLI tool working
- [x] Boot reconciliation logic implemented and tested with a hand-deleted folder on the host; package discovery after reboot confirmed on the board
- [x] Registry record structure updated with `origin` field
- [x] Admission control implemented: rejects install when `memory_kb` sum exceeds capacity
- [x] Per-app typed resource handles support queues, tasks, timers, allocations, and objects; curated native callers register ownership
- [x] `crystal_app_verify_cleanup()` scans for registered resource leaks and blocks unsafe uninstall
- [x] Namespace isolation: logical `<app_id>:key` entries stored in an app-owned NVS blob within physical key limits
- [x] Uninstall cleanup erases the entire package state blob; fresh state after reinstall verified on the board
- [x] Calculator converted to `.capp` format
- [x] Exit gate: Calculator installs/launches/uninstalls cleanly 10 times on the board with zero tracked resource leaks

### Phase 15 Checklist

- [x] Local HTTP API scaffold implemented for pairing, app list, and system status; hardware/API validation remains
- [ ] Web UI static files served over WiFi and USB (WiFi asset serving is implemented; USB transport remains)
- [ ] Pairing PIN flow working (6-digit code shown on device); hardware flow remains to validate
- [ ] Bearer token exchange and validation; hardware flow remains to validate
- [x] Ed25519 signature verification for packages with configurable trusted public keys (release key still needs configuration)
- [x] `crystal pack --sign` adds signature to `.capp`; host signing and verification tests pass
- [x] Host tooling signs, lists, and downloads packages from a signed HTTPS catalog; device catalog browsing/downloading remains
- [ ] `crystal install --usb` pushes folder over CDC-ACM
- [ ] `crystal logs -f` streams app stderr
- [ ] `crystal run` installs + launches + logs
- [ ] Install progress bar for packages > 1 MB
- [ ] Exit gate: Install via web UI and CLI both work; API rejects unauthenticated requests

### Phase 16 Checklist

- [ ] `DeclarativeApp` class implementing `CrystalApp` interface
- [ ] `ui.json` + `bindings.json` parser
- [ ] Data binding to `crystal.service` (time, network, timer)
- [ ] Event routing (button clicks → system actions)
- [ ] Exit gate: Bundled app UIs expressible without ABI additions; ABI v1 frozen

### Phase 17 Checklist

- [ ] Lua 5.4 sandbox implemented (environment, count hook, allocator)
- [ ] Per-app memory ceiling enforced
- [ ] Async `http.fetch` with callback model
- [ ] Worker thread quota enforcement
- [ ] `data/` quota enforcement and cleanup
- [ ] `crystal run` works end-to-end
- [ ] Bus app converted to Script tier and verified for resource leaks
- [ ] Exit gate: Runaway loop produces app death, not watchdog reset; HTTP request cleaned up on destroy

