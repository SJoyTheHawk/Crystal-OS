# Crystal OS — Code Guide

Skeletons for each phase of `IMPLEMENTATION_PLAN.md`. These are shapes and
contracts, not finished code — bodies are trimmed to what carries a decision.

## Layout

```
crystal-os/
├── CMakeLists.txt
├── NOTICE                     # esp-brookesia + ESP-IDF attribution
├── partitions.csv
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt         # PSRAM allocator override for Brookesia
│   └── main.cpp
├── components/
│   ├── crystal_hal/           # Phase 2 — IBrightness, IWifi, IRtc, IStorage
│   ├── crystal_core/          # Phase 3 — event queue, service task, toast, time
│   ├── crystal_app/           # Phase 4 — CrystalApp, CrystalState
│   ├── crystal_shell/         # Phases 6-10 — arbiter, switcher, quick settings, keyboard
│   ├── bsp_extra/             # reused from the reference
│   └── apps/
│       ├── app_table.cpp      # Phase 5 — the compiled-in catalog
│       └── <app>/
├── sim/                       # Phase 2 — LVGL SDL host
└── assets/                    # → SPIFFS, not compiled in
```

## Phase 0 — boot (complete)

The Phase 0 skeleton below has been implemented and verified on the physical
Waveshare ESP32-S3-Touch-LCD-4B. The Hello app opens and returns to the launcher;
the first-frame baseline is 1933 ms. RTC, service, registry, and other calls
shown below remain contracts for their later phases rather than Phase 0 exit
criteria.

## Phase 1 — performance spike (complete)

The `Phase 1 Perf` diagnostic app is intentionally isolated from the production
shell. It creates a dense static backdrop and a draggable live card, then uses
`lv_refr_get_fps_avg()` plus the built-in LVGL performance monitor to report the
display-path result. Its component is retained for future regression checks but
is not registered in the production app table.

The hardware run on 2026-09-02 showed approximately 7--8 visible FPS while
dragging with one RGB buffer. LVGL reported 19--30 FPS, which is useful
diagnostic data but does not override the visible panel measurement. Repeating
the run with `CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2` remained at approximately 7--8
visible FPS, so double buffering is not the Phase 6 solution. The switcher must
use the documented simplified animation fallback: no live full-screen tracking
card, blur, or shadow during the drag; use a short cross-fade or snap instead.

The Phase 6.5 hardware re-test corrected the display configuration to two RGB
framebuffers with avoid-tear direct mode. Instrumentation measured 3-10 ms of
synchronous flush time but 91-106 ms of render time, with only 8-10 refreshes
completed per second during sustained dragging. Obvious snapshot tearing was
removed, but the result remains below the 12 FPS crossover gate. Production
therefore carries the snap/fade transition and the direct-mode tearing fix as an
**interim baseline**, not as the final interaction: the 50% finger-tracked
crossover specified in `DESIGN.md` §5 is a requirement and moves to Phase 7.5,
after gesture arbitration. Treat the 91-106 ms render time as the blocking defect
to fix, not as grounds to redesign the switch. The diagnostic app is not
registered in the production app table.

Order matters: RTC before the first frame, WiFi after it.

```cpp
extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "reset reason: %d", esp_reset_reason());   // Phase 12
    crystal_coredump_check();                                // Phase 12
    ESP_ERROR_CHECK(bsp_spiffs_mount());
    ESP_ERROR_CHECK(bsp_extra_codec_init());

    crystal_time_init();          // PCF85063 -> settimeofday, before first frame

    lv_display_t *disp = bsp_display_start();
    if (disp && disp->driver) {
        disp->driver->rounder_cb = crystal_rounder_cb;   // panel needs even alignment
    }

    bsp_display_lock(0);

    auto *phone = new ESP_Brookesia_Phone(disp);
    auto *sheet = new ESP_Brookesia_PhoneStylesheet_t
                      ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET();
    phone->addStylesheet(sheet);
    phone->activateStylesheet(sheet);
    delete sheet;                                  // Brookesia copies it

    phone->setTouchDevice(bsp_display_get_input_dev());
    phone->registerLvLockCallback((ESP_Brookesia_LvLockCallback_t)bsp_display_lock, 0);
    phone->registerLvUnlockCallback((ESP_Brookesia_LvUnlockCallback_t)bsp_display_unlock);
    phone->begin();

    crystal_core_init(phone);     // event queue + service task + toast layer
    crystal_registry_install(phone);   // Phase 5: only enabled apps

    bsp_display_unlock();

    crystal_wifi_start();         // returns immediately; never blocks first frame
}
```

`crystal_rounder_cb` is the reference's `my_rounder_cb` unchanged:

```cpp
static void crystal_rounder_cb(lv_disp_drv_t *drv, lv_area_t *area)
{
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}
```

`main/CMakeLists.txt` keeps the reference's PSRAM allocator override verbatim —
it routes Brookesia's internal allocations to SPIRAM and is why
`ESP_BROOKESIA_MEMORY_USE_CUSTOM` is set.

## Phase 2 — HAL

Narrow interfaces, one per hardware concern. The point is the simulator, but the
seam is worth having regardless.

```cpp
// crystal_hal/include/crystal_hal.hpp
struct IBrightness {
    virtual ~IBrightness() = default;
    virtual void set(uint8_t pct) = 0;      // clamped to BSP max (95), not 100
    virtual uint8_t get() const = 0;
};

struct IRtc {
    virtual ~IRtc() = default;
    virtual bool read(struct tm *out) = 0;
    virtual bool write(const struct tm *in) = 0;
};

struct IWifi {
    virtual ~IWifi() = default;
    virtual void start() = 0;
    virtual void scan() = 0;                // async; results via UI queue
    virtual void connect(const char *ssid, const char *pass) = 0;
    virtual bool connected() const = 0;
};

struct CrystalHal {
    IBrightness *brightness;
    IRtc        *rtc;
    IWifi       *wifi;
    IStorage    *storage;
};
CrystalHal &hal();
```

The device `IBrightness` is where the 95 clamp lives, so no caller has to know:

```cpp
void DeviceBrightness::set(uint8_t pct)
{
    if (pct > BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX) pct = BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX;
    bsp_display_brightness_set(pct);
    _pct = pct;
}
```

## Phase 3 — UI event queue

The single rule this enforces: no task other than the LVGL task calls `lv_*`.

```cpp
// crystal_core/include/crystal_event.hpp
enum crystal_evt_t {
    UI_EVT_WIFI_GOT_IP, UI_EVT_WIFI_DISCONNECTED, UI_EVT_WIFI_SCAN_DONE,
    UI_EVT_WIFI_CONNECT_FAILED, UI_EVT_TIME_SYNCED, UI_EVT_BATTERY, UI_EVT_TOAST,
};

// Safe from any task, any core. Never blocks longer than `ticks`.
bool crystal_ui_post(crystal_evt_t type, const void *data, size_t len,
                     TickType_t ticks = 0);
```

```cpp
// crystal_core/src/crystal_event.cpp
static QueueHandle_t s_q;      // holds fixed-size slots; payloads memcpy'd in

void crystal_core_init(ESP_Brookesia_Phone *phone)
{
    s_q = xQueueCreate(16, sizeof(crystal_msg_t));
    // Runs on the LVGL task, so handlers are already inside the LVGL lock.
    lv_timer_create(drain_cb, 33, phone);
    crystal_toast_init();
    xTaskCreatePinnedToCore(service_task, "crystal_svc", 4096, nullptr, 2, nullptr, 0);
}

static void drain_cb(lv_timer_t *t)
{
    crystal_msg_t m;
    while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
        dispatch(&m, static_cast<ESP_Brookesia_Phone *>(t->user_data));
    }
}
```

Producers stay trivial, and cross-core UI mutation stops being possible:

```cpp
static void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *e = static_cast<ip_event_got_ip_t *>(data);
        crystal_ui_post(UI_EVT_WIFI_GOT_IP, &e->ip_info.ip, sizeof(esp_ip4_addr_t));
    }
}
```

Prefer the queue over taking `bsp_display_lock` from core 0 — that lock stalls
rendering on core 1 for as long as it is held.

## Phase 3 — toast

Non-interactive, above apps, below quick settings, and structurally unable to
steal a gesture.

```cpp
void crystal_toast_init(void)
{
    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);   // never intercepts touch
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -60);
    s_label = lv_label_create(s_toast);
}

void crystal_toast(const char *msg)   // LVGL task only; others post UI_EVT_TOAST
{
    lv_label_set_text(s_label, msg);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_fade_in(s_toast, 150, 0);
    lv_timer_t *h = lv_timer_create(hide_cb, 2500, nullptr);
    lv_timer_set_repeat_count(h, 1);
}
```

Use toasts for outcomes ("Connected to MyNetwork"), dialogs for input. The WiFi
success path in Phase 9 is a toast, not a dialog.

## Phase 3 — time

```cpp
void crystal_time_init(void)
{
    struct tm t;
    if (hal().rtc->read(&t)) {              // valid before any network exists
        struct timeval tv = { .tv_sec = mktime(&t), .tv_usec = 0 };
        settimeofday(&tv, nullptr);
    }
    char tz[40];
    crystal_nvs_get_str("tz", tz, sizeof(tz), "UTC0");   // Phase 11 setting
    setenv("TZ", tz, 1);
    tzset();
}

static void sntp_synced_cb(struct timeval *tv)   // SNTP callback
{
    struct tm t;
    localtime_r(&tv->tv_sec, &t);
    hal().rtc->write(&t);                        // push drift correction back
    crystal_ui_post(UI_EVT_TIME_SYNCED, nullptr, 0);
}
```

Without the `TZ` step, SNTP leaves you on UTC and the indicator bar shows the
wrong hour.

## Phase 4 — CrystalApp

The current checkpoint is implemented in `components/crystal_app`. `CrystalApp`
seals Brookesia's lifecycle entry points and forwards them to the four borrowed
Android lifecycle hooks — `onCreate()`/`onResume()`/`onPause()`/`onDestroy()` —
plus `onBack()`. `onStart()`/`onStop()` are declared but not dispatched in v1 (see
"Occlusion hooks" below). `init()` and `deinit()` are sealed bookkeeping entry points and do
not dispatch app hooks. Because
Brookesia owns screen creation and recycling, `onCreate()` builds the active
screen tree whenever `run()` creates a screen; app data belongs in
`CrystalState`, not in LVGL objects. `StateTestApp` is the reference conversion
used to verify that a counter survives app switching and reboot. Each lifecycle
entry is logged with the app name under the `crystal_app` tag; resumes over
80 ms also emit a warning.

`run()` and `close()` are sealed `final` so no app can bypass the lifecycle. The
reference's apps call `getVisualArea()` themselves and one of them hardcodes a
40px status-bar offset (`Drawpanel::touch_event_cb`); the base class removes the
need by handing over a pre-translated content root.

```cpp
// crystal_app/include/crystal_app.hpp — as shipped in Phase 4
class CrystalApp : public ESP_Brookesia_PhoneApp {
public:
    CrystalApp(const char *name, const void *launcher_icon = nullptr);

    CrystalState &state() { return state_; }

protected:
    bool init()   final;    // lifecycle bookkeeping only
    bool deinit() final;    // lifecycle bookkeeping only
    bool run()    final;    // -> onCreate()
    bool pause()  final;    // -> onPause()
    bool resume() final;    // -> onResume(), with the 80 ms budget check
    bool close()  final;    // -> onDestroy()
    bool back()   final;    // -> onBack()

    virtual bool onCreate()  { return true; }   // every launch: build the UI
    virtual bool onStart()   { return true; }   // reserved in v1
    virtual bool onPause()   { return true; }   // serialize state out
    virtual bool onResume()  { return true; }   // re-select of a still-live app
    virtual bool onStop()    { return true; }   // reserved in v1
    virtual bool onDestroy() { return true; }
    virtual bool onBack()    { return notifyCoreClosed(); }

private:
    CrystalState state_;
};
```

`onCreate()` fires on **every** launch, not once at install — it is Android's
`onCreate` + `onStart` + `onResume` collapsed into one event, because Brookesia
recreates the screen tree each time `run()` is called. `onResume()` is a
different event: it only fires when a still-resident, paused app is re-selected.
Phase 4.5 closes the gaps this mapping leaves.

Rebuild speed is why destroy-on-switch works. An `onCreate()` or `onResume()`
that cannot hold 80ms is an app that needs its heavy work moved off the UI path —
and the `ESP_TASK_WDT_TIMEOUT_S=5` watchdog is the failure mode if it is ignored,
since both run under the LVGL lock.

## Phase 4 — CrystalState

Per-app namespace, hard cap. Scroll offsets and draft strings, never pixels.

```cpp
// as shipped: a blob store plus u32 helpers, keyed by an FNV-1a hash of the name
class CrystalState final {
public:
    bool get(const char *key, void *value, size_t *length) const;   // <= 2048 bytes
    bool set(const char *key, const void *value, size_t length);
    bool erase(const char *key);
    bool get_u32(const char *key, uint32_t *value) const;
    bool set_u32(const char *key, uint32_t value);
private:
    std::string prefix_;          // "a<hash6>." — the sandbox boundary
};
```

Still owed, and needed by Phase 13's clear-data row: `clear()` over the whole
prefix. String helpers (`set_str`/`get_str`) are worth adding when the first app
stores draft text — Notes and the Wi-Fi credential flow both will.

The namespace is not cosmetic. It is what makes a future script sandbox
possible: an app that can read arbitrary NVS can read WiFi credentials.

A converted app builds from state in `onCreate()` and writes back in `onPause()`:

```cpp
bool Notes::onCreate()
{
    ta_ = lv_textarea_create(root_);
    lv_textarea_set_text(ta_, load_draft().c_str());
    uint32_t scroll = 0;
    (void)state().get_u32("scroll", &scroll);
    lv_obj_scroll_to_y(root_, scroll, LV_ANIM_OFF);
    return true;
}
```

The `onPause()` half is in the Phase 4.5 section — it only runs reliably once
that phase lands.

## Phase 4.5 — Lifecycle correctness

Five changes, all inside `crystal_app` except the `max_running_num` override.
`IMPLEMENTATION_PLAN.md` §"Phase 4.5" carries the reasoning; this is the shape.

A state member makes the ordering enforceable rather than assumed:

```cpp
enum class LifecycleState { INSTALLED, CREATED, STARTED, RESUMED, PAUSED, DESTROYED };
```

**`onPause()` before `onDestroy()`.** The guarantee Android gives and Phase 4
does not. `close()` becomes:

```cpp
bool CrystalApp::close()
{
    if (state_machine_ != LifecycleState::PAUSED) {
        // Not already paused by Brookesia: this is the common path (return to
        // launcher, or destroy-on-switch). Give the app its chance to write.
        dispatch_pause();
    }
    ESP_LOGI(TAG, "%s lifecycle: onDestroy", getName());
    (void)onDestroy();                      // see "return values" below
    state_machine_ = LifecycleState::DESTROYED;
    return true;
}
```

The LVGL tree is **still alive** inside both hooks — Brookesia's `processClose()`
calls `close()` before `enableAutoClean()`/`cleanResource()`. So this is legal
and is the intended way to save:

```cpp
bool Notes::onPause()
{
    state().set_str("draft",  lv_textarea_get_text(ta_));
    state().set_u32("scroll", lv_obj_get_scroll_y(root_));
    return true;
}
```

Do not carry the Android habit of caching widget values earlier "because the
views are gone by `onDestroy`". Here they are not.

**Install bookkeeping.** `init()`/`deinit()` were unsealed in Phase 4, so an app
could override them and step outside the framework. They are sealed and only
update the framework state:

```cpp
bool init()   final { state_machine_ = LifecycleState::INSTALLED; return true; }
bool deinit() final { state_machine_ = LifecycleState::DESTROYED; return true; }
```

There are no `onInstall()`/`onUninstall()` app hooks. Once-per-boot work belongs
in `onCreate()` behind an instance member guard. Clock uses that pattern to
reconcile its service and app-owned timer keys without repeating the reset on
every launch. Registry installation and Phase 13 clear-data are platform
operations, not app lifecycle callbacks.

**Occlusion hooks.** Defined here, fired by the Phase 7 arbiter — it is the only
component that knows what covers what. Until Phase 7 they are never called.

```cpp
virtual bool onStart() { return true; }  // becoming visible again
virtual bool onStop()  { return true; }  // fully occluded: stop timers, drop work
```

Clock's 1s `lv_timer` is the motivating case: redrawing behind an opaque
quick-settings panel costs bandwidth on a panel that measured 7-8 FPS at G1.

**Return values are advisory.** Brookesia force-closes an app whose `pause()`
returns `false` (`core_manager.cpp:279`), which punishes an app for honestly
reporting a failed save. Crystal logs and continues:

```cpp
void CrystalApp::dispatch_pause()
{
    ESP_LOGI(TAG, "%s lifecycle: onPause", getName());
    if (!onPause()) {
        ESP_LOGW(TAG, "%s onPause reported failure; continuing teardown", getName());
    }
    state_machine_ = LifecycleState::PAUSED;
}
```

`pause()` therefore returns `true` unconditionally. Same for `onDestroy()` —
there is no useful recovery from a failed teardown.

**One resident app.** In `main.cpp`, after the stylesheet is created and before
`addStylesheet()`:

```cpp
stylesheet->core.manager.app.max_running_num = 1;   // destroy-on-switch, per PLAN §2
```

Keep `enable_app_save_snapshot = 1` — Phase 6 needs the snapshot. With this,
every launch is `onCreate()` and every switch away is `onPause()` then
`onDestroy()`, so an app never has to ask which path it is on.

## Phase 5 — registry

One table of everything compiled in; NVS decides what installs.

The logical settings are `app.<id>.enabled` and `app.<id>.slot`, but those names
exceed NVS's 15-character key limit. `crystal_registry` hashes the stable app ID
and stores the values as `r<8-hex-hash>.e` and `r<8-hex-hash>.s` inside the
existing `crystal` namespace. App IDs therefore must remain stable across
firmware updates.

```cpp
// components/apps/app_table.cpp
struct AppEntry {
    const char *id;
    CrystalApp *(*factory)();
    bool default_enabled;
};

static const AppEntry kApps[] = {
    { "notes",      []() -> CrystalApp * { return new Notes(); },      true  },
    { "calculator", []() -> CrystalApp * { return new Calculator(); }, true  },
    { "draw",       []() -> CrystalApp * { return new Drawpanel(); },  false },
};
```

```cpp
void crystal_registry_install(ESP_Brookesia_Phone *phone)
{
    struct Row { const AppEntry *e; int slot; };
    std::vector<Row> rows;

    for (auto &e : kApps) {
        char key[32];
        snprintf(key, sizeof(key), "app.%s.en", e.id);
        if (!crystal_nvs_get_bool(key, e.default_enabled)) continue;   // "uninstalled"
        snprintf(key, sizeof(key), "app.%s.slot", e.id);
        rows.push_back({ &e, crystal_nvs_get_i32(key, 999) });
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row &a, const Row &b) { return a.slot < b.slot; });

    for (auto &r : rows) {
        CrystalApp *app = r.e->factory();          // constructed only if enabled
        if (phone->installApp(app) < 0) {
            ESP_LOGE(TAG, "install failed: %s", r.e->id);
            delete app;
        }
    }
}
```

Disabled apps are never constructed, so a hidden app costs flash but no RAM.
This function is the whole of Phase 13's backend — install/uninstall is a bool
flip, reorder is an int, clear-data is `CrystalState::clear()`.

Assets load from SPIFFS in `onResume()` rather than compiling in, which is what
keeps 15-25 apps inside a 5M slot:

```cpp
lv_img_set_src(img, "S:/assets/notes/header.bin");   // not LV_IMG_DECLARE
```

## Phase 6 — snapshot and blur

Full-size is 460KB; the 1/8 buffer is ~7KB. Blur the small one and let LVGL
upscale on draw.

```cpp
#define SNAP_W 60      // 480 / 8
#define SNAP_H 60

struct CrystalSnapshot {
    lv_color_t  px[SNAP_W * SNAP_H];    // ~7KB, PSRAM
    lv_img_dsc_t dsc;
};

void crystal_snapshot_take(lv_obj_t *scr, CrystalSnapshot *out)
{
    // Draw the screen into a full-size scratch buffer, then box-downsample 8x.
    // Averaging the 8x8 block (rather than point-sampling) is most of the blur.
    lv_img_dsc_t *full = lv_snapshot_take(scr, LV_IMG_CF_TRUE_COLOR);
    downsample_8x_average(full, out->px);
    lv_snapshot_free(full);

    box_blur_3x3(out->px, SNAP_W, SNAP_H);   // cheap on 3600 px; ~60x less work

    out->dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    out->dsc.header.w  = SNAP_W;
    out->dsc.header.h  = SNAP_H;
    out->dsc.data      = (const uint8_t *)out->px;
    out->dsc.data_size = sizeof(out->px);
}
```

Two snapshots exist at a time — outgoing and incoming — so steady-state cost is
~14KB regardless of how many apps have ever been opened. That is the property
that replaced keeping apps resident.

The scratch buffer for `lv_snapshot_take` is the one full-size allocation; take
it from PSRAM and free it immediately.

## Phase 7 — gesture arbiter

One owner, claimed once per touch, released on lift. The implementation reuses
Brookesia's global gesture sampler and mask object. The stylesheet disables
Brookesia's eager time-based mask; `crystal_shell` raises that mask only when its
12px direction lock selects an OS owner. Raising it resets the app's active LVGL
target, preventing a later release or click from leaking through.

```cpp
enum class CrystalGestureOwner { None, AppSwitch, QuickSettings, App };

static constexpr int kLockThreshold = 12;    // px of travel before direction locks
static constexpr int kEdgeBand      = 24;    // px from left/right edge
static constexpr int kTopBand       = 20;    // px from top for quick settings

static CrystalGestureOwner s_owner = CrystalGestureOwner::None;
```

```cpp
static void on_pressing(const ESP_Brookesia_GestureInfo_t &info)
{
    if (s_owner != CrystalGestureOwner::None ||
        info.direction == ESP_BROOKESIA_GESTURE_DIR_NONE) return;
    // Apply modal, quick-settings, keyboard, Settings, then OS-edge precedence.
    // Call gesture->setMaskObjectVisible(true) only for an OS owner.
}
```

The `start.y <= kTopBand` condition is the one that matters. Without it, a
swipe-down inside a scrolled app view opens quick settings when the user meant
to scroll up — the only case where OS-over-app priority feels like a bug.
Quick-settings-open also suppresses app switching, as decided.

Future overlay phases must call the corresponding
`crystal_shell_set_*_open(bool)` API when their visibility changes. Do not infer
overlay state by scanning the LVGL tree. Battery reads also stay out of the LVGL
task: `IPower::readBattery()` runs on `crystal_service` no faster than every 30
seconds and posts the result back through the UI queue.

## Phase 7.5 — finger-tracked crossover

The crossover uses an app-area-clipped overlay on `lv_layer_top()`. The outgoing
app is captured at direction lock and remains visually stationary. A single
incoming card moves 1:1 with horizontal touch distance; the outgoing/current app
remains live and full-resolution underneath, while the incoming half-resolution RGB565 pane is
enlarged to the card area during motion and comes from the bounded neighbour cache when that card has previously
been visited. An unvisited neighbour immediately shows a shell-rendered card face
with its launcher icon and app name while its real app is prepared at direction
lock. The destination is captured and attached behind the overlay as soon as the
drag begins; 10% controls only the live crossover commit. Never use a blank black
pane for this state.

The state machine is `Idle` -> `Dragging` -> `Settling`. Crossing 10% of the app
width commits the already-prepared destination without the Phase 6 snap/fade. Release at or beyond
10% animates the card to full width; release below 10% restores the original app
when necessary and animates the card off-screen. The overlay blocks input through
the 250 ms settle and is clipped to the app visual area, so neither drag nor shadow
can cover the indicator bar.

Pane ownership stays bounded: the active drag owns one outgoing capture, and the
cache retains only the current card's immediate neighbours. A neighbour is
prepared only for an active edge drag, never speculatively while the app is idle.

## Phase 8 — quick settings

```cpp
void quick_settings_on_release(int y_offset)
{
    // Past half-open, finish opening; otherwise snap back.
    bool open = y_offset > PANEL_H / 2;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_values(&a, y_offset, open ? PANEL_H : 0);
    lv_anim_set_time(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void brightness_bar_cb(lv_event_t *e)
{
    int pct = lv_bar_get_value(lv_event_get_target(e));
    hal().brightness->set(pct);          // clamp lives in the HAL, not here
    crystal_nvs_set_i32("brightness", pct);
}
```

Bar range should be 0..95 to match `BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX`. Setting
0..100 makes the top 5% of the slider travel do nothing.

## Phase 10 — keyboard field centering

```cpp
static void on_keyboard_shown(lv_obj_t *field, int kb_top)
{
    lv_area_t f;
    lv_obj_get_coords(field, &f);

    if (f.y2 < kb_top) return;      // already visible — leave it exactly where it is

    int target = (kb_top + INDICATOR_H) / 2;    // midpoint of the free band
    int delta  = (f.y1 + (f.y2 - f.y1) / 2) - target;
    lv_obj_scroll_by(lv_obj_get_parent(field), 0, -delta, LV_ANIM_ON);
}
```

The early return is the requirement: a field that is already visible must not
jump, even when the keyboard is not covering it.

## Phase 12 — coredump

```cpp
void crystal_coredump_check(void)
{
    esp_core_dump_summary_t s;
    if (esp_core_dump_get_summary(&s) != ESP_OK) return;

    ESP_LOGE(TAG, "coredump: PC=0x%08lx task=%s", s.exc_pc, s.exc_task);
    crystal_nvs_set_bool("recovered", true);   // surfaced quietly after boot
    esp_core_dump_image_erase();               // so it reports once
}
```

Decode with `idf.py coredump-info` / `coredump-debug`. **This only works against
the exact ELF that crashed** — archive `build/crystal_os.elf` alongside every
image handed to anyone, or the dump is unreadable hex.

## Phase 5.5 — timer that outlives its app

The pattern that makes destroy-on-switch survivable for time-based apps. Store
the **absolute end instant**, derive everything else.

```cpp
// Wrong: decremented by the app, so it stops when the app is destroyed
state().set_i32("remaining", remaining - 1);

// Right: fixed point in time, correct whenever it is read
state().set_i32("end_at", (int32_t)(time(nullptr) + duration_s));
```

```cpp
bool Clock::onResume()
{
    int32_t end_at = state().get_i32("end_at", 0);
    if (end_at > time(nullptr)) {
        // Timer is still running — rejoin it mid-flight, do not restart it.
        _remaining = end_at - time(nullptr);
        start_ring_animation(_remaining);
    }
    _tick = lv_timer_create(tick_cb, 200, this);   // display only; owns nothing
    return true;
}

void Clock::onPause()
{
    lv_timer_del(_tick);      // the countdown itself is not the app's to stop
}
```

Deriving from an absolute instant also survives an SNTP correction moving the
wall clock, which a decrementing counter would not.

Expiry belongs to the service, not the app:

```cpp
// crystal_service, core 0 — runs whether or not Clock exists
static void service_tick_1s(void)
{
    int32_t end_at = crystal_nvs_get_i32("app.clock.end_at", 0);
    if (end_at && time(nullptr) >= end_at) {
        crystal_nvs_set_i32("app.clock.end_at", 0);
        crystal_ui_post(UI_EVT_TIMER_EXPIRED, nullptr, 0);   // toast + chime
    }
}
```

Paused state stores remaining seconds plus a flag rather than an end time, since
there is no end instant while paused.

## Phase 9.5 — network fetch off the UI task

```cpp
// crystal_service task. Never on the LVGL task — TLS blocks for seconds.
static void weather_fetch(void)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m",
             s_lat, s_lon);

    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,   // no key to leak
        .timeout_ms = 8000,
    };
    // ... perform, parse into a small POD, then hand it to the UI by value:
    crystal_ui_post(UI_EVT_WEATHER, &reading, sizeof(reading));
}
```

The handler on the LVGL side writes the cache and refreshes labels. Nothing in
the fetch path touches `lv_*`, which is the whole point.

Cache with its timestamp so the app opens with data rather than a spinner, and so
staleness is displayable:

```cpp
state().set_i32("temp_c10", reading.temp_c * 10);   // avoid float in NVS
state().set_i32("humidity", reading.humidity);
state().set_i32("wmo",      reading.weather_code);
state().set_i32("fetched",  (int32_t)time(nullptr));  // drives "Updated N min ago"
```

## Assets: never compile images in

The reference makes the cost concrete — same image, two forms:

```
img_app_calculator.png        2,951 bytes
img_app_calculator.c        906,288 bytes     # 307x larger
img_app_drawpanel.png        21,734 bytes
img_app_drawpanel.c        906,282 bytes
```

Two icons in C-array form would spend ~1.8MB of a 5M app slot. Convert to LVGL
binary images and load from SPIFFS:

```cpp
lv_img_set_src(icon, "S:/assets/clock/icon.bin");   // not LV_IMG_DECLARE
```

## Conventions

- Any `lv_*` call happens on the LVGL task. No exceptions. Post to the queue.
- Apps never read absolute screen coordinates; use `content()`.
- No app touches NVS directly; use `state()` so the namespace holds.
- Assets go to SPIFFS. Compiling images in is what forced the reference's 9MB
  partition.
- Every hardware access goes through `hal()`, which is what keeps the simulator
  viable.
