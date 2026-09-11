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
│   ├── crystal_registry/      # Phase 5 — enabled/slot flags over NVS
│   └── <app>_app/             # one component per app, not a shared apps/ dir
├── sim/                       # Phase 2 — LVGL SDL host
└── assets/                    # → SPIFFS, not compiled in
```

**As built, not as drawn.** There is no `components/apps/app_table.cpp` and no
`bsp_extra` component: the catalog is `kApps` in `main/main.cpp:33`, each app is a
top-level component (`clock_app`, `weather_app`, `calculator_app`, `dev_tester`),
and codec/volume access lives behind `crystal_hal_set_volume()`. `hello_app`,
`state_test_app`, and `perf_spike` are still on disk but appear in neither
`main/CMakeLists.txt`'s `REQUIRES` nor `kApps`, so they are not linked. They are
kept as references; restoring one means editing both places.

`crystal_shell` is the large component — ~3000 lines carrying the arbiter, the
Phase 7.5 crossover, the quick panel, the keyboard host, the whole Settings page
stack, and the WiFi page. New shell surfaces (including Phase 12's update page)
land there and build on `system_page_push()`. Splitting it is not free: the
Settings pages, the arbiter's suppression flags, and the crossover all share
file-scope state, so a split has to move that state deliberately rather than by
cutting the file in half.

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

    crystal_time_init();          // PCF85063 -> settimeofday, before first frame
    ESP_ERROR_CHECK(bsp_spiffs_mount());

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

    // Status-bar callbacks are passed in, so crystal_core never includes Brookesia.
    crystal_core_init(display, update_status_clock, update_status_connectivity,
                      update_status_battery, phone);
    crystal_registry_install(phone, kApps, sizeof(kApps) / sizeof(kApps[0]));
    crystal_shell_init(phone);

    lv_refr_now(display);
    bsp_display_unlock();
    // WiFi is not started here: service_task calls hal().wifi->start() after a
    // 1200 ms delay, so nothing on the boot path can block the first frame.
}
```

The shipped `app_main` differs from earlier revisions of this section in three
ways worth stating, because the old shapes are still quoted elsewhere: there is no
`crystal_nvs_*` API (shell state is `hal().storage`, app state is `CrystalState`),
`crystal_core_init()` takes the display plus three status-bar callbacks rather than
the phone, and `crystal_registry_install()` takes the app table explicitly. The
Phase 12 boot calls are added to this function in the order given under
"Phase 12 — reliability" below.

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
    char tz[40] = "UTC0";                   // fallback if nothing is stored
    size_t length = sizeof(tz);
    (void)hal().storage->get("tz", tz, &length);   // Phase 11 setting
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
`CrystalState`, not in LVGL objects. `StateTestApp` was the original conversion used
to verify that a counter survives app switching and reboot; it is still in
`components/state_test_app` but is no longer linked, so read `CalculatorApp` for the
current reference conversion — its in-progress formula is the same property, tested by
a shipping app. Each lifecycle entry is logged with the app name under the
`crystal_app` tag; resumes over 80 ms also emit a warning.

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
// As shipped, in crystal_app.hpp:33 — a nested enum, mixed case, not SHOUTING.
class CrystalApp : public ESP_Brookesia_PhoneApp {
public:
    enum class LifecycleState : uint8_t {
        Installed, Created, Started, Resumed, Paused, Destroyed,
    };
    LifecycleState lifecycle_state() const;
};
```

**`onPause()` before `onDestroy()`.** The guarantee Android gives and Phase 4
does not. `close()` becomes:

```cpp
bool CrystalApp::close()
{
    if (lifecycle_state_ != LifecycleState::Paused) {
        // Not already paused by Brookesia: this is the common path (return to
        // launcher, or destroy-on-switch). Give the app its chance to write.
        dispatch_pause();
    }
    ESP_LOGI(TAG, "%s lifecycle: onDestroy", getName());
    (void)onDestroy();                      // see "return values" below
    lifecycle_state_ = LifecycleState::Destroyed;
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
bool init()   final { lifecycle_state_ = LifecycleState::Installed; return true; }
bool deinit() final { lifecycle_state_ = LifecycleState::Destroyed; return true; }
```

There are no `onInstall()`/`onUninstall()` app hooks. Once-per-boot work belongs
in `onCreate()` behind an instance member guard. Clock uses that pattern to
reconcile its service and app-owned timer keys without repeating the reset on
every launch. Registry installation and Phase 13 clear-data are platform
operations, not app lifecycle callbacks.

**Occlusion hooks — declared, never fired.** Phase 4.5 provisionally assigned the
call sites to the Phase 7 arbiter. Phase 7 closed without them and **v1 does not
dispatch them at all**: the lifecycle is four states plus `onBack`, and that is
settled (`DESIGN.md` §5.5). They stay as base-class no-ops so a later version can
fire them without an ABI break.

```cpp
virtual bool onStart() { return true; }  // reserved: becoming visible again
virtual bool onStop()  { return true; }  // reserved: fully occluded
```

An app must not override either one expecting to be called. Anything that has to
happen when the app stops being foreground goes in `onPause()`, which does fire, and
on this device is the same event — a card is destroyed on switch, so there is no
visible-but-not-foreground state to distinguish. Clock's 1s `lv_timer` is deleted in
`onPause()` for exactly this reason.

The reserved case is screen-off, which the four states genuinely cannot express:
backlight off means the card is invisible, but tearing down its tree would make wake
slow. If that ever ships, `onStop()` is where it goes.

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
    lifecycle_state_ = LifecycleState::Paused;
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
// main/main.cpp — the table ships as CrystalAppEntry with an explicit slot
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
        // Hashed keys, not "app.<id>.en": NVS caps a key at 15 characters.
        if (!crystal_registry_enabled(e.id, e.default_enabled)) continue;  // "uninstalled"
        rows.push_back({ &e, crystal_registry_slot(e.id, 999) });
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

This section records the superseded Phase 6 transition baseline. The current
finger-tracked switcher uses the icon-only path documented in Phase 7.5 below;
these buffers are not retained or rendered during an edge drag.

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

**A drag is pixels, not lifecycle.** The icon card is shell-owned UI. It must
never start, resume, pause, or destroy a Brookesia app. The outgoing app is the
only live app until the finger lifts past the commit threshold. `DESIGN.md` §5
is the authority for the interaction; `PHASE_7_5_PREVIEW_LIFECYCLE.md` records
the retired preview implementation.

The crossover uses an app-area-clipped overlay on `lv_layer_top()`. The outgoing
app remains live and visually stationary beneath the transparent overlay. A
single incoming card moves 1:1 with horizontal touch distance and always draws
the destination's launcher icon and name.

There is one threshold, and it is tested on release:

```cpp
constexpr uint32_t kCrossoverCommitPercent = 50;  // of app-area width
```

10% is a **visual reveal point only**. The icon stays clipped outside the card's
visible screen area until its entering edge reaches the boundary at 10%, then
moves to the exposed-area centre at 50%. The name follows the same motion while
fading linearly from transparent at 10% to opaque at 50%. This has no lifecycle
effect; never call `start_card()` before a qualifying release.

The state machine stays `Idle` -> `Dragging` -> `Settling`.

| Event | Visual | Lifecycle |
|---|---|---|
| Direction lock | build transparent overlay and target icon card | none |
| Drag below 10% | card follows finger; identity remains clipped and hidden | **none** |
| Drag from 10% | icon edge enters; name fades toward 50% | **none** |
| Release below 50% | card animates back off-screen | **none** |
| Release at/above 50% | card animates to full width and is painted | `start_card()` once, a frame later |

The commit runs in three stages, each separated by one painted frame. Finishing
the animation is not the same as showing it: an `lv_anim` ready callback fires
inside `lv_timer_handler` *before* the refresh, so at that moment the card sits at
full width in the object tree and has not reached the panel. Doing the switch there
costs the user the final frame and makes the slide look like it overlaps the
switch. Each stage therefore hands off through a one-shot `lv_timer` of
`kCommitStageMs`:

1. `schedule_card_commit()` — anim ready. Yields so the full-width card is flushed.
2. `card_commit_cb()` — sends the start event behind the card: A `onPause()` →
   A `onDestroy()` → B `onCreate()` → B `onResume()`.
3. `card_reveal_cb()` — the target has had a frame to draw, so the overlay goes.

The shell never invokes an app's hooks by hand; it sends the start event and lets
the manager dispatch. `max_running_num = 1`, so there is no state in which two
apps are live.

Do not collapse these stages back into one callback. If the slide starts feeling
like it runs in parallel with the switch, a stage boundary has been removed.

A cancelled drag deletes the overlay and nothing else. It needs no repair step:
because nothing was started, there is nothing to restore. If you find yourself
writing a `start_card(original_index)` in the cancel path, the drag started an app
it should not have.

The overlay blocks input through the 250 ms settle and is clipped to the app
visual area, so neither the card nor its shadow can cover the status bar. Keep it
in place across a committed transition — it is what hides the target's
construction — and delete it only once the target is live and resumed.

## Phase 7.5 — icon identity motion

`set_transition_progress()` owns all drag-derived visuals. It keeps the identity
outside the visible screen area through 10%, interpolates it to the exposed
slice's centre at 50%, then follows that centre until the card settles fully
open. Apply the same calculation during commit and cancel settling so motion
reverses without a discontinuity.

The switcher no longer captures, caches, loads, or persists app previews. Old
`/spiffs/crystal_preview_<stable-app-id>.bin` files are ignored and deliberately
left untouched; do not add startup deletion or migration I/O for them.

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
    // As shipped this goes through crystal_brightness_set(), which owns both the
    // HAL call and the persistence so the ramp and the saved value cannot diverge.
    const uint8_t level = (uint8_t)pct;
    hal().brightness->set(level);        // clamp lives in the HAL, not here
    (void)hal().storage->set("brightness", &level, sizeof(level));
}
```

Bar range should be 0..95 to match `BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX`. Setting
0..100 makes the top 5% of the slider travel do nothing.

## Phase 8.5 — corner panel, grid, colour state

Full spec in `PHASE_8_5_QUICK_PANEL.md`. The three things that bite:

**Grid descriptors must be `static`.** `lv_obj_set_grid_dsc_array()` retains the
pointer rather than copying, so a stack array leaves LVGL reading freed memory on
the next layout pass.

```cpp
static lv_coord_t s_cols[] = {62, 62, 62, 62, LV_GRID_TEMPLATE_LAST};
lv_obj_set_grid_dsc_array(panel, s_cols, s_rows);
lv_obj_set_grid_cell(wifi, LV_GRID_ALIGN_STRETCH, 0, 2,   // col 0, span 2
                           LV_GRID_ALIGN_STRETCH, 0, 2);  // row 0, span 2
```

**Translate, never fade.** In LVGL 8 a non-opaque object with children composites
through an intermediate buffer. On a single-buffer RGB panel that is the whole
reason the panel is opaque instead of frosted — animating its opacity gives the
cost straight back.

**Tap-outside needs a transparent catcher, added late.** LVGL hit-tests by area,
not opacity, so `LV_OPA_TRANSP` still takes clicks and no visible scrim is needed.
Attach it only after the open animation finishes, or the release that ends the
pull gesture lands outside the panel and dismisses it instantly.

State is `LV_STATE_CHECKED` on a tile, not `lv_switch`. Colour is the signal;
text stays wherever more than two states exist — off and unavailable both render
dim, so a colour-only Bluetooth tile is indistinguishable from Energy Saving being
off.

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
    int32_t end_at = 0;
    size_t length = sizeof(end_at);
    (void)hal().storage->get("timer.end", &end_at, &length);
    if (end_at != 0 && time(nullptr) >= end_at) {
        const int32_t cleared = 0;
        (void)hal().storage->set("timer.end", &cleared, sizeof(cleared));
        crystal_ui_post(UI_EVT_TIMER_EXPIRED, nullptr, 0);   // toast + chime
    }
}
```

The shipped service goes further than this sketch: it keeps the deadline in RAM and
falls back to a monotonic uptime deadline when the RTC and SNTP have never produced a
valid wall clock, since a device that does not know the time must still be able to run
a three-minute timer. Only the wall-clock form is persisted, which is what preserves
the documented "countdown cleared after reboot" behaviour. The absolute-instant rule
is the part to copy; the storage call is illustrative.

Paused state stores remaining seconds plus a flag rather than an end time, since
there is no end instant while paused.

## Phase 9 — WiFi scan results and the page

The `esp_event` task has a 2304-byte stack (`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`).
`wifi_ap_record_t` is ~110 bytes, so this overflows it and corrupts memory:

```cpp
// WRONG — ~2.2 KiB on a 2304-byte stack. Crashes on every scan completion.
static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    wifi_ap_record_t records[20] = {};
    esp_wifi_scan_get_ap_records(&fetch, records);
}
```

Keep the buffer out of the handler frame. The adapter already needs the parsed
results to outlive the callback anyway:

```cpp
// Adapter member, not a local. The handler fills it and posts a bare event;
// the UI task reads it back through scan_results().
static wifi_ap_record_t s_records[kMaxNetworks];   // file scope

static void event_handler(void *arg, esp_event_base_t, int32_t id, void *)
{
    auto *self = static_cast<DeviceWifi *>(arg);
    uint16_t fetch = kMaxNetworks;
    if (esp_wifi_scan_get_ap_records(&fetch, s_records) != ESP_OK) {
        self->notify(ScanDone);                    // still notify; empty list
        return;
    }
    // parse s_records into self->scan_results_, sort by RSSI, then:
    self->notify(ScanDone);
}
```

The same rule applies to anything else that runs on that task. Nothing large is
declared in an `esp_event` handler frame.

### The WiFi page, not an inline list

Long-press closes quick settings first and opens a page. The two must not overlap
— the panel is destroyed before the page is built, so the page never has a
translating ancestor and never inherits `QUICK_SETTINGS` ownership:

```cpp
lv_obj_add_event_cb(s_quick_wifi, [](lv_event_t *) {
    close_quick_settings(/*animate=*/true, /*then=*/wifi_page_open);
}, LV_EVENT_LONG_PRESSED, nullptr);
```

`wifi_page_open` runs from the close animation's ready callback, so it starts
with no gesture owner held. It sets the same suppression flag the settings page
uses (`crystal_shell_set_settings_open(true)`), which already blocks the
pull-down and app switching.

Scan results arrive asynchronously, so the page must tolerate opening before any
results exist, and a scan completing after it closed:

```cpp
void crystal_shell_wifi_event(uint8_t event)
{
    if (event == UI_EVT_WIFI_SCAN_DONE) {
        if (s_wifi_page == nullptr) return;   // page closed mid-scan; drop it
        wifi_page_fill_list();
    }
}
```

That null check is the whole defence. The previous version rebuilt a list
parented to `s_quick_panel`, which could already be freed by a dismiss gesture.

Password entry ships before Phase 10's keyboard overlay, so the dialog creates a
plain `lv_keyboard` and deletes it with itself. Phase 10 swaps in the shell
overlay; the dialog must not assume it owns the keyboard's lifetime.

## Phase 9.1 — deriving WiFi state, not remembering it

Every WiFi surface is built lazily and the event stream is silent while an
association is stable. A widget whose constructor hardcodes state is therefore
wrong for as long as it lives:

```cpp
// WRONG — the panel is built on first open, ~2.5 s after boot. If the network
// came up before that, this text is already false and no event is coming.
s_quick_wifi = tile(LV_SYMBOL_WIFI "\nWiFi\nOn\nNot Connected", ...);
if (hal().wifi != nullptr && hal().wifi->enabled()) {
    lv_obj_add_state(s_quick_wifi, LV_STATE_CHECKED);   // enabled != connected
}
```

One formatter, called from both the constructor and the event handler. Build-time
and event-time must not be able to disagree:

```cpp
static void wifi_tile_text(char *out, size_t size)
{
    IWifi *wifi = hal().wifi;
    if (wifi == nullptr || !wifi->enabled())  strlcpy(out, LV_SYMBOL_WIFI "\nWiFi\nOff", size);
    else if (wifi->connected())               snprintf(out, size, LV_SYMBOL_WIFI "\nWiFi\n%.32s", wifi->last_ssid());
    else                                      strlcpy(out, LV_SYMBOL_WIFI "\nWiFi\nOn\nNot Connected", size);
}
```

The rule generalises past this tile: a lazily built widget reads the HAL, it does
not trust that it saw the event.

### Glyphs must be in the linked font

```cpp
// WRONG — U+2713 is not in Montserrat's ASCII + FontAwesome subset. Renders
// as a missing-glyph box, silently, with no build warning.
const char *tick = connected ? "✓ " : "";
```

Use the FontAwesome names from `lv_symbol_def.h`; those are the glyphs actually
compiled in. `LV_SYMBOL_OK` is `0xF00C`:

```cpp
const char *tick = connected ? LV_SYMBOL_OK " " : "";
```

### Attempted is not connected

`s_wifi_connecting` holds the last *attempted* SSID. A tick driven off it marks a
network you failed to join, because nothing clears it on the terminal events:

```cpp
// WRONG — survives ConnectFailed and Disconnected. Reads as success.
strcmp(networks[i].ssid, s_wifi_connecting) == 0 ? LV_SYMBOL_OK " " : ""
```

A tick means connected, so ask what is connected. Both the tick and the row
highlight use the one predicate:

```cpp
const char *joined = (wifi != nullptr && wifi->connected()) ? wifi->last_ssid() : "";
const bool is_joined = strcmp(networks[i].ssid, joined) == 0;
```

Keep `s_wifi_connecting` for an in-progress affordance, distinct from the tick,
and clear it on both `ConnectFailed` and `Disconnected`.

### Forgetting has an order

`esp_wifi_disconnect()` does not forget anything — `WIFI_STORAGE_FLASH` keeps the
credentials and `start()` reconnects next boot. And the retry timer added for
transient `AUTH_EXPIRE` will fight you:

```cpp
// WRONG — disconnect fires the handler, which sees last_ssid_ still set and
// schedules a reconnect to the network being forgotten.
void forget() override
{
    esp_wifi_disconnect();
    last_ssid_[0] = '\0';
}
```

Disarm the retry path before causing the disconnect it watches for, and write a
zeroed config to erase the NVS copy:

```cpp
void forget() override
{
    if (retry_timer_ != nullptr) (void)esp_timer_stop(retry_timer_);
    retries_ = 0;
    last_ssid_[0] = '\0';          // before disconnect: the retry branch gates on this
    (void)esp_wifi_disconnect();
    wifi_config_t empty = {};
    (void)esp_wifi_set_config(WIFI_IF_STA, &empty);   // this is what forgets
    notify(Disconnected);
}
```

### Publish signals on the default event loop

Crystal OS consumes `WIFI_EVENT` and `IP_EVENT` already; publishing its own
signals needs a base, not new machinery:

```cpp
// crystal_core.hpp
ESP_EVENT_DECLARE_BASE(CRYSTAL_NETWORK_EVENT);
enum { CRYSTAL_NETWORK_CONNECTED, CRYSTAL_NETWORK_DISCONNECTED };

// crystal_core.cpp
ESP_EVENT_DEFINE_BASE(CRYSTAL_NETWORK_EVENT);
```

Post from `wifi_event()` before the UI post, and check the result — the queue is
32 deep (`CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE`) and posting can fail:

```cpp
const int32_t id = (event == IWifi::GotIp) ? CRYSTAL_NETWORK_CONNECTED
                                           : CRYSTAL_NETWORK_DISCONNECTED;
const esp_err_t err = esp_event_post(CRYSTAL_NETWORK_EVENT, id, nullptr, 0, 0);
if (err != ESP_OK) ESP_LOGW(TAG, "network signal dropped: %s", esp_err_to_name(err));
```

Subscribers run on the same 2304-byte event task as the Phase 9 scan handler, so
the same rule applies: dispatch work, do not do it. No blocking, no deep frames,
no `lv_*`. Handler order across subscribers is unspecified — no subscriber may
depend on another having run.

### A destroyed app must unregister

Apps are destroyed on switch. A handler left registered points into freed memory
and the next post calls through it:

```cpp
// WRONG — Android's leaked-receiver bug with no GC to soften it. The next
// CONNECTED post is a wild jump, and it will not reproduce on the bench.
bool onCreate() override
{
    esp_event_handler_instance_register(CRYSTAL_NETWORK_EVENT, ESP_EVENT_ANY_ID,
                                        &on_network, this, nullptr);
    return true;
}
```

Store the instance handle and unregister symmetrically, unconditionally:

```cpp
bool onCreate() override
{
    return esp_event_handler_instance_register(CRYSTAL_NETWORK_EVENT, ESP_EVENT_ANY_ID,
                                               &on_network, this, &network_handler_) == ESP_OK;
}
bool onDestroy() override
{
    if (network_handler_ != nullptr) {
        (void)esp_event_handler_instance_unregister_with(
            nullptr, CRYSTAL_NETWORK_EVENT, ESP_EVENT_ANY_ID, network_handler_);
        network_handler_ = nullptr;
    }
    return true;
}
```

Better still, do not subscribe from an app when the work is not an app's. Time
sync outlives every app, so it subscribes once from `crystal_core_init()` and
never unregisters; the Clock app keeps consuming `UI_EVT_TIME_SYNCED` on the UI
task. An app subscribing directly also means the feature only works while that
app happens to be resident.

### SNTP starts on the signal, not at boot

```cpp
// WRONG — start() is non-blocking, so this runs with no interface up. The
// default config has start = true, so the one request goes nowhere and there
// is no interface-up retry. sync_cb never fires.
hal().wifi->start();
start_sntp();
```

Separate init from start, and let the signal drive it:

```cpp
static void on_network_connected(void *, esp_event_base_t, int32_t, void *)
{
    if (!s_sntp_inited) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        config.start   = false;               // so init and start are separable
        config.sync_cb = sntp_synced;
        if (esp_netif_sntp_init(&config) != ESP_OK) return;
        s_sntp_inited = true;
    }
    if (!s_sync_in_flight) {                  // GotIp also fires on DHCP renewal
        s_sync_in_flight = true;
        (void)esp_netif_sntp_start();
    }
}
```

Clear `s_sync_in_flight` in `sntp_synced`. Reconnect re-syncs for free, and a
renewal every few minutes does not become a request storm.

## Phase 9.5 — Weather

The fetch path landed first and works; the app is a placeholder — three labels, no
icon, no location, hardcoded coordinates. This section is the shape of the rest.

### Provider: Open-Meteo, and the question is closed

`DESIGN.md` §Weather and `IMPLEMENTATION_PLAN.md` §9.5 both decided this, for one
reason: **weather.com and every other keyed provider requires a secret in
firmware, and `esptool read_flash` extracts it.** A keyed API means either
treating the key as public or standing up a proxy server Crystal OS does not
have. Do not reopen this to gain a nicer icon set or a 7-day forecast.

### Location: resolve it, do not hardcode it

`weather_fetch()` currently carries this, which ships every device set to Hong Kong:

```cpp
// WRONG — a compiled-in constant is not a location. Every unit reports HK weather.
double latitude  = 22.3193;
double longitude = 114.1694;
```

Three sources, strict precedence, first hit wins:

```text
manual lat/lon (Settings › Region & Time, Phase 11)
  -> cached IP geolocation result in NVS
     -> compiled default (HK, matching the Phase 11 default TZ HKT-8)
```

**IP geolocation is the v1 default, not the later convenience `DESIGN.md`
assumed.** That line was written when Settings was expected to land first; it did
not, and shipping 9.5 against a constant is worse than shipping it against a
city-accurate guess. Manual entry stays the override and remains authoritative
once Phase 11 exposes the field.

Use a keyless HTTPS endpoint (`ipwho.is`, or `ipapi.co/json`) — the provider
choice carries the same no-key constraint as the weather API, so verify the free
tier before wiring one in. Resolve **once**, on the first `CRYSTAL_NETWORK_CONNECTED`
after boot with no location cached, and persist lat/lon plus the returned city
name. It is not per-fetch work: the device does not move, and re-resolving on
every refresh spends a TLS handshake to learn nothing.

Three things IP geolocation gets wrong, all of which the UI must survive:
CGNAT and mobile carriers can place a device in another city; a VPN places it in
another country; and the lookup tells a third party this device's IP. City-level
accuracy is enough for regional weather and is not enough for anything else — do
not grow this into a general location service.

### Show the resolved location, always

`DESIGN.md`'s mock puts `<location>` bottom-right, and with auto-detection it stops
being decoration and becomes the only way a user can tell the guess was wrong.
Show the city name from the geolocation response; fall back to `"22.32, 114.17"`
formatted coordinates when only numbers are known, and `"Location not set"` when
nothing resolved. A silently wrong city is a bug report that reads "weather is
broken".

Tapping it is the natural place for re-detect and manual entry. Ship the label in
9.5; the affordance can wait for Phase 11 to own the input.

### Icons: procedural, like the clock

There is no SPIFFS asset pipeline yet and `assets/` is empty, so the eight WMO
glyphs follow `clock_app/src/clock_icon.c` — draw into a static `lv_color_t` map
once, expose one `lv_img_dsc_t`. Compiling PNG-converted C arrays is what the
"never compile images in" section below forbids, and it costs ~906KB each.

Eight groups, composed from three primitives (sun disc, cloud blob, precipitation
dashes) rather than eight independent bitmaps:

| WMO code | Group |
|---|---|
| 0 | clear |
| 1-3 | partly cloudy |
| 45-48 | fog |
| 51-57 | drizzle |
| 61-67, 80-82 | rain |
| 71-77, 85-86 | snow |
| 95-99 | thunderstorm |
| other | unknown |

`condition()` in `weather_app.cpp` already collapses codes to strings, but its
ranges are wrong at the edges — `code <= 3` catches 2 and 3 as "Partly cloudy"
where 3 is overcast, and `code <= 48` swallows 4-44 into "Fog". One table, mapping
code to `{ label, icon }`, keeps the string and the glyph from disagreeing.

The app also needs a launcher icon — `WeatherApp()` passes none, so it renders as
Brookesia's default. Same procedural pattern, 64x64.

### Widths must match what was written

`CrystalState` is a blob store over `nvs_get_blob`, which happily reads a 2-byte
blob into a 4-byte buffer and reports `length == 2`. Nothing fails, and the top
two bytes keep whatever the stack held:

```cpp
// WRONG — update() wrote int16_t; refresh() reads int32_t. Positive temperatures
// survive on little-endian by luck; -5.0C reads as a large positive number.
int32_t temp = 0;
size_t n = sizeof(temp);
const bool have = state().get("temp_c10", &temp, &n);
```

Read into the exact type that was written, and check the length:

```cpp
int16_t temp = 0;
size_t n = sizeof(temp);
const bool have = state().get("temp_c10", &temp, &n) && n == sizeof(temp);
```

The same mismatch is live on `humid`, `wmo`, and `wind`. The version of this
section that recommended `state().set_i32(...)` was describing an API
`CrystalState` does not have — there is `get`/`set` plus `get_u32`/`set_u32`, so a
typed helper per field is the least error-prone fix.

### The fetch path, unchanged

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

### `perform()` does not leave you a body

This reads correctly and returns an empty buffer every time:

```cpp
// WRONG — read_response() gets 0 bytes, so every parse below it fails.
if (esp_http_client_perform(client) == ESP_OK &&
        esp_http_client_get_status_code(client) == 200) {
    const int length = esp_http_client_read_response(client, buf, sizeof(buf) - 1);
```

`esp_http_client_perform()` drains the body itself to satisfy `content_length`,
and because `response->buffer->output_ptr` is NULL it caches nothing
(`esp_http_client.c` `http_on_body`: *"Do not cache body when http_on_body is
called from esp_http_client_perform"*). It then sets `raw_len = 0` and closes the
connection. The status code is 200 and the buffer is empty, which is why this
failure looks like a broken API rather than a client bug.

Use the sequence that hands the body over, and loop — one `read` is not
guaranteed to return everything:

```cpp
if (esp_http_client_open(client, 0) != ESP_OK) return false;
const int64_t declared = esp_http_client_fetch_headers(client);   // <0 = chunked
if (esp_http_client_get_status_code(client) == 200) {
    int total = 0;
    while (total < limit) {
        const int read = esp_http_client_read(client, buf + total, limit - total);
        if (read <= 0) break;            // 0 = complete, <0 = error
        total += read;
    }
    buf[total] = '\0';
}
esp_http_client_close(client);
```

Both the weather fetch and the geolocation lookup had this bug. It presented as
"Unable to refresh weather" plus a location that looked correct — the location was
the compiled Hong Kong fallback, which is indistinguishable from a successful
resolve for anyone actually in Hong Kong. **A fallback that matches the expected
answer hides the failure it exists to report.** Log resolved-vs-fallback
distinctly.

### Geolocation endpoint choice is not free

`ipapi.co` answers **403 with a Cloudflare interstitial** to a device request, so
it cannot be a fallback. `ip-api.com` gates HTTPS behind a paid plan. `ipwho.is`
answers 200 without a key.

Ask for only the fields you parse:

```text
https://ipwho.is/?fields=success,city,latitude,longitude
```

That trims the body from 957 bytes to ~106. The full response nearly filled the
1023-byte usable buffer, which would have truncated mid-document the moment the
provider added a field.

Its JSON is pretty-printed, so a literal `"city":"` never matches:

```cpp
// WRONG — the body is `"city": "Hong Kong"`. The space defeats the pattern, and
// %[^"] does not skip leading whitespace the way %lf does.
sscanf(c, "\"city\":\"%23[^\"]", city);
```

Find the opening quote, then scan from it. Numbers are more forgiving — `%lf`
skips the whitespace on its own — but do not rely on the two behaving alike.

### The response parser

The response parser uses `strstr` plus `sscanf` against a 1 KiB static buffer.
That is acceptable for four known fields in a compact document and is why no JSON
library is linked — but it must not grow. A forecast array, or anything that can
exceed 1 KiB, needs a real parser and a chunked read, not a bigger buffer.

Cache with its timestamp so the app opens with data rather than a spinner, and so
staleness is displayable. Store integers scaled by 10; there are no floats in NVS:

```cpp
(void)state().set("temp_c10", &reading.temperature_c10, sizeof(reading.temperature_c10));
(void)state().set("wmo",      &reading.weather_code,    sizeof(reading.weather_code));
(void)state().set("fetched",  &reading.fetched_at,      sizeof(reading.fetched_at));
```

### A reading can arrive after the app is gone

`crystal_shell_weather_event()` looks the app up in the registry and calls
`update()` on it. The instance outlives its LVGL tree — destroy-on-switch means a
30-minute service refresh routinely lands while the app is `Destroyed` and every
label pointer is dangling:

```cpp
// WRONG — the failure branch touches a label before the lifecycle is checked.
void WeatherApp::update(const CrystalWeatherReading &reading)
{
    if (!reading.success) { lv_label_set_text(updated_, "Unable to refresh weather"); return; }
```

Check liveness first, for every branch. Persisting is always safe; drawing is not:

```cpp
void WeatherApp::update(const CrystalWeatherReading &reading)
{
    pending_ = false;
    if (reading.success) write_cache(reading);   // safe in any lifecycle state
    if (lifecycle_state() != LifecycleState::Created &&
        lifecycle_state() != LifecycleState::Started &&
        lifecycle_state() != LifecycleState::Resumed) return;
    reading.success ? refresh() : show_refresh_failure();
}
```

`onDestroy()` must null `root_`, `condition_`, `details_`, `updated_`, and
`icon_`. Brookesia frees the tree; the members are the app's to clear, and
`refresh()`'s null guard only works if something actually nulls them.

### Layout uses the visual area, not 480x440

```cpp
lv_obj_set_size(root_, 480, 440);   // WRONG — hardcoded bar height, per Conventions
```

Use `getVisualArea()` as `clock_app.cpp:55` does. The status bar height is a
stylesheet value, and 440 is a guess that goes stale silently.

### States, and none of them is a spinner

| Condition | Shows |
|---|---|
| Cache present, fresh | reading + "Updated N min ago" |
| Cache present, offline or fetch failed | same reading, age kept visible |
| No cache, no WiFi | empty state, pointing at WiFi |
| No location resolved | prompt, not a HK reading |

Exit criteria for the phase: correct conditions and icon with WiFi up; the
resolved location visible; a cached reading with a visible age when offline; and
no `lv_*` call anywhere in the fetch path.

## Phase 9.6 — Calculator port

**Implementation note (2026-09-07):** `components/calculator_app` now carries
the port described below. It uses a local app root sized from `getVisualArea()`,
stores the formula (including its NUL terminator) under the app's
`CrystalState`, and registers as the default-enabled launcher slot 4. The
procedural icon avoids the reference's 906KB image array. The ESP-IDF firmware
build passes. Hardware validation on 2026-09-07 confirmed arithmetic and all
calculator controls, and confirmed that an in-progress formula survives a card
switch. Phase 9.6 is closed.

The reference app is `reference/.../components/apps/calculator` (439 lines,
`Calculator.cpp` + `Calculator.hpp`). Three things in it do not survive the port,
and they are the whole work of the phase.

**It overrides the wrong methods.** The reference derives from
`ESP_Brookesia_PhoneApp` and overrides `run()`, `close()`, `back()`, and
`init()`. In `CrystalApp` all five of those are `final` (`crystal_app.hpp:44`) —
they are the adapter that dispatches the Android hooks. The port overrides
`onCreate()` / `onPause()` / `onResume()` / `onDestroy()` / `onBack()` instead,
exactly as `WeatherApp` does:

```cpp
class CalculatorApp final : public CrystalApp {
public:
    CalculatorApp();
    bool onCreate() override;
    bool onPause() override;
    bool onDestroy() override;
};
```

`run()` becomes `onCreate()`, `close()` becomes `onDestroy()`. Do not keep
`init()` — once-per-boot setup goes in `onCreate()` guarded by the app.

**It parents to the active screen.** Every widget in `Calculator::run()` is
created on `lv_scr_act()`:

```cpp
keyboard   = lv_btnmatrix_create(lv_scr_act());   // reference — do not copy
label_obj  = lv_obj_create(lv_scr_act());
```

That puts the button matrix outside the app's own root, so it survives nothing
and is positioned against the display rather than the app area. Build one root
sized from `getVisualArea()` and positioned at `(0,0)`, as `weather_app.cpp:144`
does, then parent everything to it:

```cpp
const lv_area_t area = getVisualArea();
root_ = lv_obj_create(lv_scr_act());
lv_obj_set_size(root_, lv_area_get_width(&area), lv_area_get_height(&area));
lv_obj_set_pos(root_, 0, 0);            // not area.x1/area.y1 — see below
```

The offset trap is documented under Phase 9.5: `getVisualArea()` returns display
coordinates, but the app root is already parented inside the app container, so
adding `area.x1/y1` double-counts the status bar and produces a page that
scrolls.

**Its state is in members, so a switch away erases it.** `formula_len`,
`formula_label`, and `history_label` are plain fields. Cards are destroyed on
switch, so the accumulator has to be in `CrystalState`. It is a string, so use
the byte API — `CrystalState` has `get`/`set`/`erase`/`get_u32`/`set_u32` and
nothing else:

```cpp
// onPause: the formula text is the state. Store the NUL as well.
const char *text = lv_label_get_text(formula_label_);
state().set("formula", text, strlen(text) + 1);

// onCreate: restore before the first paint, not after.
char formula[64] = {};
size_t len = sizeof(formula);
if (state().get("formula", formula, &len) && len > 0 && len <= sizeof(formula)) {
    formula[len - 1] = '\0';
} else {
    formula[0] = '0'; formula[1] = '\0';
}
```

Register it in `main.cpp` beside the others — a factory and a row in `kApps`
(`main/main.cpp:33`), which is what makes the registry's enabled/slot flags apply
to it:

```cpp
static CrystalApp *make_calculator_app() { return new CalculatorApp(); }
{"calculator", make_calculator_app, true, 4},
```

**The icon.** `img_app_calculator.c` is 906,288 bytes for a 2,951-byte PNG. Do
not take it. Follow `clock_icon.c` and `weather_icon.c`: a procedural
`lv_img_dsc_t` filled at boot by a `*_prepare()` function, ~1-5KB of code and one
64x64 buffer. See "Assets" below for why this, and not SPIFFS, is the pattern in
this tree today.

`calculate()` and the `isStartZero()` / `isStartNum()` / `isStartPercent()` /
`isLegalDot()` input guards port unchanged. That is the point of borrowing it.

Exit: arithmetic correct, and the in-progress formula survives a swipe away and
back.

## Phase 10 — keyboard overlay

Phase 10 replaces the WiFi dialog's private keyboard with the shell-owned
`crystal_keyboard_show()` overlay. Apps supply the focused `lv_textarea` and the
viewport whose bottom should bind to the keyboard:

```cpp
crystal_keyboard_show(input, form_viewport);
```

Implementation details that carry the design:

- The shell overlay calls `crystal_shell_set_keyboard_open()` for its entire
  lifetime, so the arbiter suppresses card switching until teardown.
- The keyboard derives its width from the display and publishes its top through
  `crystal_keyboard_top()`; callers do not duplicate a hardcoded dialog rect.
- Its 200px reserved band uses Calculator's 26px bottom safe inset, preserving
  the top position while keeping keys out of Brookesia's navigation gesture zone.
- Letter, shift, and symbol maps keep identical row geometry. LVGL still owns
  character insertion, cursor movement, deletion, and ready/cancel events.
- The WiFi password field and Dev Tester use the same overlay and both provide a
  reveal control for masked input.

The centering rule, which is the exit criterion:

```cpp
static void center_focused_field(lv_obj_t *field, lv_coord_t kb_top)
{
    lv_area_t f{};
    lv_obj_get_coords(field, &f);           // display coords, same as kb_top

    if (f.y2 < kb_top) return;              // already visible — do not move it

    const lv_area_t area = getVisualArea(); // app band bottom, not a constant
    const lv_coord_t target = (kb_top + area.y1) / 2;
    const lv_coord_t delta  = (f.y1 + (f.y2 - f.y1) / 2) - target;
    lv_obj_scroll_by(lv_obj_get_parent(field), 0, -delta, LV_ANIM_ON);
}
```

The early return *is* the requirement: a field that is already fully visible must
not move, even by a pixel, even when the keyboard is nowhere near it. Both
coordinate sources have to be in the same space — mixing `lv_obj_get_coords()`
(display) with an area-relative keyboard top is the same class of bug as the
Phase 9.5 offset trap.

Dismissal has three paths (return/done, tap outside a field, back gesture) and
all three must clear `s_keyboard_open`, or the pull-down stays dead for the rest
of the session. Clear it in one place — the overlay's own teardown — not at each
call site.

## Phase 11 — Settings and power

The authoritative implementation guide is `PHASE_11_SETTINGS.md`. It defines the
page stack, bottom-edge ownership, HAL additions, storage schema, timezone
catalog, power policy, build order, and validation criteria. Keep those details
there so interfaces and verified line references have one owner.

The phase is implemented, device-validated, and closed. The five resolved defects
and their passing regression checklist are retained in
`PHASE_11_BUG_FIXES_V3.md`.
Two things there are general traps worth knowing outside Phase 11: bind LVGL
handlers unconditionally and gate on state inside them rather than binding inside
an `if` on state that changes while the page is open, and remember that a textarea
keeps drawing its cursor until something sends it `LV_EVENT_DEFOCUSED` — LVGL 8.4's
`lv_textarea` has no handler for it and the theme draws the cursor from
`LV_STATE_FOCUSED`.

## Phase 12 — reliability

Three coredump options are in `sdkconfig.defaults` — `ENABLE_TO_FLASH`,
`DATA_FORMAT_ELF`, `CHECKSUM_CRC32`. `CONFIG_ESP_COREDUMP_CHECK_BOOT` is **not**
written there; it defaults to `y` in IDF, which is the behaviour wanted (a corrupt
image is rejected before you try to read it) but is worth pinning explicitly, since
an option you rely on and never state is an option that changes under you on an IDF
bump.

**One option is missing and it gates an exit criterion.**
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is absent. Without it the bootloader never
marks a freshly written image `PENDING_VERIFY`, so a bad OTA cannot roll back — it
just boots and stays broken. Add it in the same change as the OTA path, not after:

```
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

Everything else in this phase is new code. It has three parts — the coredump
report, the validity handshake, and the update path — and they are independent
enough to land in that order.

### Where the boot calls go

```cpp
extern "C" void app_main(void)
{
    // ... nvs_flash_init() ...

    ESP_LOGI(TAG, "reset reason: %s", crystal_reset_reason_name());
    crystal_coredump_check();     // safe here: see below

    crystal_time_init();
    ESP_ERROR_CHECK(bsp_spiffs_mount());
    // ... display, phone, core, registry, shell ...

    lv_refr_now(display);
    bsp_display_unlock();
}
```

`crystal_coredump_check()` writes through `hal().storage`, and it is legal this
early because `s_hal` is a statically initialised table of pointers to
statically constructed adapters (`crystal_hal.cpp:946`) — `crystal_hal_init()`
only *restores saved settings*, it does not construct the HAL. Do not read that as
licence to call any HAL method before `crystal_hal_init()`: brightness and volume
are un-restored at that point, and the display does not exist at all.

### The coredump report

Two records, written in one pass and read by two different surfaces. The toast
flag is consumed once; the detail row survives until the next crash.

```cpp
struct CrystalCrashRecord {          // 15-char NVS key limit: store one blob
    uint32_t pc;
    uint32_t reset_reason;           // esp_reset_reason_t at the crash boot
    int32_t  when;                   // time(nullptr), 0 if the RTC was invalid
    char     task[16];               // exc_task is already bounded
};

void crystal_coredump_check(void)
{
    esp_core_dump_summary_t summary = {};
    if (esp_core_dump_get_summary(&summary) != ESP_OK) return;   // ELF format only

    ESP_LOGE(TAG, "coredump: PC=0x%08" PRIx32 " task=%s",
             summary.exc_pc, summary.exc_task);

    CrystalCrashRecord record = {};
    record.pc           = summary.exc_pc;
    record.reset_reason = (uint32_t)esp_reset_reason();
    record.when         = (int32_t)time(nullptr);
    strlcpy(record.task, summary.exc_task, sizeof(record.task));

    const uint8_t pending = 1;
    if (hal().storage != nullptr) {
        (void)hal().storage->set("crash.last", &record, sizeof(record));
        (void)hal().storage->set("crash.new", &pending, sizeof(pending));
    }
    (void)esp_core_dump_image_erase();      // so it reports exactly once
}
```

`crystal_coredump_check()` runs before `crystal_time_init()`, so `time(nullptr)`
is whatever the last `settimeofday()` left — usually 0 on a cold boot. That is
correct rather than unfortunate: a crash timestamp the device cannot vouch for
should read "unknown", and the Device Status row formats `when == 0` that way
instead of printing 1 Jan 1970. Moving the check after `crystal_time_init()` to
"fix" this trades a truthful blank for a plausible lie about when the crash was.

There is no `crystal_nvs_*` API in this tree: shell state is `hal().storage`, app
state is `CrystalState`.

**The coredump partition is plaintext.** `NVS_ENCRYPTION` protects NVS, not the
`coredump` partition — a dump is a register and stack snapshot, so a WiFi password
still sitting in a stack buffer at crash time is readable with
`esptool read_flash`. That is acceptable for developer hardware and is a real
consideration before handing a crashed unit to anyone; erasing the image at boot,
which this function already does, is most of the mitigation.

### The quiet notice

Reported once, on the boot after the crash, then the flag clears. The toast layer
already exists, so the surfacing is one queue post — but not from `app_main`, which
runs before `crystal_core_init()` creates the queue. Post it from `service_task`'s
startup, in place of the current unconditional "Core services ready":

```cpp
// service_task, after the 1200 ms settle. The flag is cleared here rather than
// in crystal_coredump_check(), so a crash during boot still gets reported on the
// next successful boot instead of being swallowed by the one that crashed.
uint8_t pending = 0;
size_t  length  = sizeof(pending);
if (hal().storage->get("crash.new", &pending, &length) && pending == 1) {
    (void)hal().storage->erase("crash.new");
    static constexpr char kMessage[] = "Recovered from an error";
    (void)crystal_ui_post(UI_EVT_TOAST, kMessage, sizeof(kMessage) - 1);
}
```

Keep the message free of PC values and task names. The toast is a reassurance that
the device noticed; the detail belongs in Settings › System › Device Status, which
reads `crash.last` and formats a `Last Crash` row beside the existing `Last Reset`
one (`crystal_shell.cpp:2708`). One crash, two audiences.

`esp_reset_reason()` is worth logging on every boot, crash or not — it is the only
cheap way to separate a panic from a brownout or a watchdog reset, and brownouts on
this board look like random reboots. `ISystemInfo::reset_reason()`
(`crystal_hal.cpp:810`) already maps it to a string; use that rather than a second
switch.

Decode with `idf.py coredump-info` / `coredump-debug`. **This works only against
the exact ELF that produced the dump.** Archive `build/crystal_os.elf` with every
image handed to anyone; without that habit the dump is unreadable hex. The same
archive is what makes the OTA below auditable, so archive the `.bin` and the `.elf`
as a pair keyed by version string.

Watch `CONFIG_ESP_TASK_WDT_TIMEOUT_S` with panic disabled: the likeliest trip is
a slow `onResume()` holding the LVGL lock. Since `onResume()` runs on the LVGL
task, anything blocking there — a storage read loop, a fetch — is a watchdog
candidate. Post to the service task instead.

### Version strings need a source

`ISystemInfo::app_version()` returns `esp_app_get_description()->version`
(`crystal_hal.cpp:825`), which with no `PROJECT_VER` set is derived from
`git describe` — so it changes on every commit and is empty in a tarball build. An
update flow compares versions, so pin it in the root `CMakeLists.txt` *before*
`project()`:

```cmake
set(PROJECT_VER "1.0.0")
project(crystal_os)
```

Comparing them is string equality, not ordering. Semantic-version ordering on a
device invites a downgrade that the manifest did not intend; "the manifest offers
something other than what is running" is the whole predicate needed.

### The validity handshake

With rollback enabled, a new image boots as `PENDING_VERIFY` and the bootloader
reverts to the previous slot on the next reset unless something confirms it. What
counts as confirmation is a judgement call, and the wrong answer makes rollback
useless:

```cpp
// WRONG — app_main returning proves the linker worked, nothing more. A firmware
// that boots and then wedges the LVGL task confirms itself and cannot roll back.
esp_ota_mark_app_valid_cancel_rollback();
```

Confirm from `service_task`, after the display, the shell, and the first frame have
all survived a settling period. The service task's existing 1200 ms delay is
already the point at which the UI is known to be up:

```cpp
// service_task, once, after the crash notice above.
const esp_partition_t *running = esp_ota_get_running_partition();
esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
        ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_LOGI(TAG, "confirming new image after successful UI bring-up");
    (void)esp_ota_mark_app_valid_cancel_rollback();
}
```

A boot loop before that point is exactly what rollback is for, so nothing here
should try to be clever about retrying. If the new image cannot reach a first
frame, letting it revert is the correct outcome.

### The update path

Settings › System › Software Update, one page in `crystal_shell` via
`system_page_push()`. The image comes from an HTTPS manifest whose URL is
compiled in as a default and editable in that page — `esp_https_ota` with the
certificate bundle, which `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY`
already accommodates for rotating roots.

```json
{ "version": "1.0.1", "url": "https://.../crystal_os-1.0.1.bin", "notes": "..." }
```

Parse it with the `strstr` + `sscanf` pattern from the weather fetch against a
1 KiB static buffer, and honour the constraint stated there: **that parser must not
grow.** Three known scalar fields in a document you control is what it is for. Read
the body with `open`/`fetch_headers`/`read`/`close` — `esp_http_client_perform()`
drains the body and leaves you nothing, which is documented under Phase 9.5 and
bites identically here.

Both the check and the write run on `crystal_service`. Its stack is 8192 bytes and
already carries a TLS handshake for weather, so the handshake is affordable; the OTA
write is flash-bound rather than stack-bound. Nothing in this path calls `lv_*` —
progress reaches the UI as a queued event:

```cpp
enum crystal_evt_t : uint8_t { /* ... */ UI_EVT_OTA_PROGRESS, UI_EVT_OTA_RESULT };
```

`EventMessage` carries a fixed `kEventDataMax` payload (`crystal_core.cpp:39`), so
send a small POD — a percentage plus a state byte — and let the page format it. Do
not send strings through it.

Three things the page owns that the OTA library does not:

**Hold the screen on.** A ten-minute download under the 60-second screen-off
timeout looks exactly like a crash. Suppress auto-dim for the duration and restore
the user's setting afterwards, including on the failure path. `crystal_power_set_auto_dim()`
is the switch; the restore belongs in one place so an early return cannot skip it.

**Block navigation during the write.** `crystal_shell_set_modal_open(true)` for the
write phase only — the arbiter already treats a modal as blocking everything
(`DESIGN.md` §4 precedence 1). Leaving the page mid-write does not stop the flash
write, so the choice is between a UI that lies about what is happening and one that
does not offer the exit. Checking for an update is cancellable; writing is not.

**Report the reboot as deliberate.** After a successful write the device restarts
into `PENDING_VERIFY`. Say so before calling `esp_restart()`, or the user reads an
unannounced reboot as the crash the previous section apologises for.

### What this does not protect against

Stated because the gap is easy to mistake for a feature. TLS plus the certificate
bundle authenticates the *host*, so nobody on the network can substitute an image.
It does not authenticate the *image*: whoever controls the manifest host controls
what every device installs, and there is no signature check, because
`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` is not enabled and no key is provisioned.
For a self-hosted manifest that is a reasonable v1 position. If images are ever
served from infrastructure that is not yours, signed images are the fix — and
anti-rollback (`CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK`) is deliberately *not* enabled,
because it burns efuses and permanently forecloses installing an older image on that
unit.

The USB wrapper stays the recovery transport: same image, two paths. The dual 5M
slots have existed since Phase 0 precisely so this phase needs no repartition.

Exit: a deliberate crash produces a symbolised backtrace and a single quiet toast on
the next boot, with detail in Device Status; a good OTA installs, confirms itself,
and reports its new version; a bad OTA rolls back to the previous slot without
manual intervention.

## Phase 13 — app catalog

Everything needed is already in `crystal_registry.hpp` — this phase is the
user-facing face of it:

```cpp
bool     crystal_registry_enabled(const char *id, bool default_value);
uint16_t crystal_registry_slot(const char *id, uint16_t default_value);
bool     crystal_registry_set_enabled(const char *id, bool enabled);
bool     crystal_registry_set_slot(const char *id, uint16_t slot);
size_t   crystal_registry_installed_count();
const char *crystal_registry_installed_id(size_t index);
```

Install is `set_enabled(id, true)`; reorder is `set_slot`. Iterate with
`installed_count()` / `installed_id()` — do not keep a second list of app ids in
the catalog UI, or it drifts from `kApps` the first time an app is added.

**One thing does not exist yet: clear-data.** `CrystalState` exposes
`get`/`set`/`erase`/`get_u32`/`set_u32` and no `clear()`. Erasing one app's data
means either adding `CrystalState::clear()` (which knows its own `prefix_` and
can iterate that namespace) or an explicit key list per app. Prefer the former —
a per-app key list in the catalog is a list that goes stale silently. The design
doc's Manage Apps section refers to `CrystalState::clear()` as though it exists;
it is a to-build, not a call site.

**Disabled apps are never constructed**, so a hidden app costs flash and no RAM.
That also means toggling one on cannot instantiate it retroactively — decide
whether enabling takes effect at once (construct and install into the phone now)
or at next boot, and say which in the UI copy. Silently doing neither is the
failure mode.

The honest limit belongs in the UI, not just the docs: users install only what
shipped in the firmware, and the catalog grows when the OS updates.

Exit: a non-developer can install, remove, reorder, and clear app data without a
firmware change.

## Assets: never compile images in

The reference makes the cost concrete — same image, two forms:

```
img_app_calculator.png        2,951 bytes
img_app_calculator.c        906,288 bytes     # 307x larger
img_app_drawpanel.png        21,734 bytes
img_app_drawpanel.c        906,282 bytes
```

Two icons in C-array form would spend ~1.8MB of a 5M app slot.

**What this tree actually does: procedural icons.** Every app icon so far is drawn
into a static buffer at boot and exposed as an `lv_img_dsc_t` — `clock_icon.c`
(1,198 bytes), `hello_icon.c` (1,994), `weather_icon.c` (5,054). One 64x64 RGB565
buffer is 8KB of BSS, and the drawing code is a few dozen lines:

```cpp
// clock_icon.c — filled once, from main, before the icon is used
static lv_color_t clock_icon_map[ICON_SIZE * ICON_SIZE];
void clock_icon_prepare(void);
extern const lv_img_dsc_t clock_icon;
```

Keep to this for icons. It costs less flash than either alternative, needs no
filesystem, and cannot fail at runtime the way a missing file can.

**SPIFFS is mounted, but `assets/` is empty.** `bsp_spiffs_mount()` runs on the
boot path (`main/main.cpp:101`), so the partition is live. The retired Phase 7.5
preview files may remain on upgraded devices but are no longer read or written.
What does not exist is an asset *pipeline* —
nothing converts a PNG to an LVGL binary image and nothing ships one. So
`lv_img_set_src(icon, "S:/...")` needs an asset put there first, and the LVGL
filesystem drive letter has to be registered for that path form to resolve. SPIFFS
stays the right answer for genuine bitmap content (photos, multi-frame art,
anything a loop cannot draw), and the pipeline is part of the first such asset's
work:

```cpp
lv_img_set_src(icon, "S:/assets/foo/art.bin");   // requires the mount to exist
```

The rule that does not bend either way: no image is ever a compiled-in C array.

## Conventions

- Any `lv_*` call happens on the LVGL task. No exceptions. Post to the queue.
- Apps never read absolute screen coordinates; use `content()`.
- No app touches NVS directly; use `state()` so the namespace holds.
- Assets go to SPIFFS. Compiling images in is what forced the reference's 9MB
  partition.
- Every hardware access goes through `hal()`, which is what keeps the simulator
  viable.
- Gestures move pixels. Only a completed gesture changes app lifecycle, and it
  does so through Brookesia — `start_card()` is the single entry point, and it is
  never called from a drag in progress.
