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

The temporary `Phase 1 Perf` app is intentionally isolated from the production
shell. It creates a dense static backdrop and a draggable live card, then uses
`lv_refr_get_fps_avg()` plus the built-in LVGL performance monitor to report the
single-buffer result. The app should be removed after the G1 decision is written
and before the Phase 6 switcher is implemented.

The hardware run on 2026-09-02 showed approximately 7--8 visible FPS while
dragging with one RGB buffer. LVGL reported 19--30 FPS, which is useful
diagnostic data but does not override the visible panel measurement. Repeating
the run with `CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2` remained at approximately 7--8
visible FPS, so double buffering is not the Phase 6 solution. The switcher must
use the documented simplified animation fallback: no live full-screen tracking
card, blur, or shadow during the drag; use a short cross-fade or snap instead.

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

`run()` and `close()` are sealed `final` so no app can bypass the lifecycle. The
reference's apps call `getVisualArea()` themselves and one of them hardcodes a
40px status-bar offset (`Drawpanel::touch_event_cb`); the base class removes the
need by handing over a pre-translated content root.

```cpp
// crystal_app/include/crystal_app.hpp
class CrystalApp : public ESP_Brookesia_PhoneApp {
public:
    CrystalApp(const char *name, const lv_img_dsc_t *icon)
        : ESP_Brookesia_PhoneApp(name, icon, true) {}

protected:
    virtual bool onCreate()  { return true; }   // once, at install
    virtual bool onResume() = 0;                // build UI from state()
    virtual void onPause()   {}                 // serialize state out
    virtual void onDestroy() {}                 // release non-UI resources

    lv_obj_t     *content();    // sized to the visual area, origin already at (0,0)
    CrystalState &state();

private:
    bool init()  override final { return onCreate(); }
    bool run()   override final;
    bool close() override final;
    bool back()  override final { notifyCoreClosed(); return true; }

    lv_obj_t *_content = nullptr;
};
```

```cpp
bool CrystalApp::run(void)
{
    uint32_t t0 = lv_tick_get();

    lv_area_t a = getVisualArea();
    _content = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_content, a.x2 - a.x1, a.y2 - a.y1);
    lv_obj_align(_content, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_pad_all(_content, 0, 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    bool ok = onResume();

    uint32_t dt = lv_tick_elaps(t0);
    if (dt > 80) {
        ESP_LOGW(TAG, "%s onResume took %lums (budget 80ms)", getName(), dt);
    }
    return ok;
}

bool CrystalApp::close(void)
{
    onPause();          // write state before anything is torn down
    _state.flush();
    onDestroy();
    _content = nullptr; // Brookesia destroys the screen tree itself
    return true;
}
```

Rebuild speed is why destroy-on-switch works. An `onResume()` that cannot hold
80ms is an app that needs its heavy work moved off the UI path — and the
`ESP_TASK_WDT_TIMEOUT_S=5` watchdog is the failure mode if it is ignored, since
`run()` holds the LVGL lock.

## Phase 4 — CrystalState

Per-app namespace, hard cap. Scroll offsets and draft strings, never pixels.

```cpp
class CrystalState {
public:
    static constexpr size_t kMaxBytes = 2048;

    void        set_i32(const char *k, int32_t v);
    int32_t     get_i32(const char *k, int32_t def = 0);
    void        set_str(const char *k, const char *v);
    const char *get_str(const char *k, const char *def = "");
    void        clear();          // backs "clear app data" in Phase 13
    void        flush();
private:
    char _ns[16];                 // "app.<name>" — the sandbox boundary
};
```

The namespace is not cosmetic. It is what makes a future script sandbox
possible: an app that can read arbitrary NVS can read WiFi credentials.

A converted app then reads:

```cpp
bool Notes::onResume()
{
    _ta = lv_textarea_create(content());
    lv_textarea_set_text(_ta, state().get_str("draft"));
    lv_obj_scroll_to_y(content(), state().get_i32("scroll"), LV_ANIM_OFF);
    return true;
}

void Notes::onPause()
{
    state().set_str("draft", lv_textarea_get_text(_ta));
    state().set_i32("scroll", lv_obj_get_scroll_y(content()));
}
```

## Phase 5 — registry

One table of everything compiled in; NVS decides what installs.

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

One owner, claimed once per touch, released on lift.

```cpp
enum GestureOwner { OWNER_NONE, OWNER_APP_SWITCH, OWNER_QUICK_SETTINGS, OWNER_APP };

static constexpr int kLockThreshold = 12;    // px of travel before direction locks
static constexpr int kEdgeBand      = 24;    // px from left/right edge
static constexpr int kTopBand       = 20;    // px from top for quick settings

static GestureOwner s_owner = OWNER_NONE;
```

```cpp
static void on_pressing(lv_point_t start, lv_point_t now)
{
    if (s_owner != OWNER_NONE) return;                 // claimed once, then stable

    int dx = now.x - start.x, dy = now.y - start.y;
    if (abs(dx) < kLockThreshold && abs(dy) < kLockThreshold) return;

    if (abs(dx) > abs(dy)) {
        if (start.x <= kEdgeBand && dx > 0)             s_owner = OWNER_APP_SWITCH; // prev
        else if (start.x >= SCREEN_W - kEdgeBand && dx < 0) s_owner = OWNER_APP_SWITCH; // next
    } else if (dy > 0 && start.y <= kTopBand && !quick_settings_open()) {
        s_owner = OWNER_QUICK_SETTINGS;
    }

    if (s_owner == OWNER_NONE) s_owner = OWNER_APP;
}

bool crystal_gesture_should_pass_to_app(void)
{
    return s_owner == OWNER_APP;      // OS ownership blocks lv_indev delivery
}
```

The `start.y <= kTopBand` condition is the one that matters. Without it, a
swipe-down inside a scrolled app view opens quick settings when the user meant
to scroll up — the only case where OS-over-app priority feels like a bug.
Quick-settings-open also suppresses app switching, as decided.

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
