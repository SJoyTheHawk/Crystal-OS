# Crystal OS v2 - ESP32-P4 Migration Plan (Option A)

**Original plan date:** 2025-01-13; reconciled 2026-11-14 (Asia/Hong_Kong)  
**Strategy:** Keep Crystal OS architecture, port to P4, cherry-pick visual/UX enhancements  
**Target Hardware:** ESP32-P4-WIFI6-Touch-LCD-4B  
**Target IDF:** v6.1 (Crystal builds, flashes and boots on P4 rev1.3)
**Target Brookesia:** existing Crystal 0.4.2 / LVGL 8.4.0; retained for Phase P4.1

## Baseline reconciliation — 2026-11-14

Read the [verified baseline and assumption corrections](reference/esp32-p4-factory/README.md),
[validation record](reference/esp32-p4-factory/factory-idf6-validation.md) and
[session handoff](reference/esp32-p4-factory/CRYSTAL_OS_P4_SESSION_HANDOFF.md).
This section supersedes conflicting assumptions in the original plan below.

- Factory source built unchanged under IDF v6.1; all six flashed images passed
  hash verification on ESP32-P4 rev1.3, 32 MB flash, profile `rev1_x`.
- JD9365DA/720x720 boot/display, touch, Calculator open/close, Wi-Fi scan,
  association and IPv4 passed; an SNTP message was observed.
- **Preserve the existing C6 firmware.** Its version query times out and reports
  `coprocessor=0.0.0`, but basic Wi-Fi works. Tested P4 host dependencies are
  `esp_wifi_remote` 1.6.5 and `esp_hosted` 3.0.10; the installed C6 version is unknown.
- Reconnect, sustained traffic, TLS/application protocols, soak, peripherals and
  rev3 runtime are not qualified. microSD timed out with card presence unknown.
  The legacy I2C diagnostic is not evidence of physical bus failure.
- There is no verified full original-flash backup. The partition table was saved;
  factory writes preserved NVS but expanded storage and replaced its assets.
- Keep the tested Downloads workspace separate. Its project is `examples/factory/`
  and artifacts are `build/idf6-rev1_x/`. Do not substitute this repo's vendor copy,
  import generated build trees, or claim an established upstream commit.

**Phase P4.1 implementation and device results — 2026-11-14:** See the
[Crystal Phase 1 validation and build guide](ESP32_P4_PHASE1_VALIDATION.md).
Crystal's signed IDF v6.1 image now builds, flashes with hash verification and
boots on rev1.3. JD9365DA/720x720 and GT911 initialization, Crystal's four-app
registry/card shell, encrypted NVS, both filesystems, Wi-Fi association/IPv4,
SNTP/RTC synchronization, weather HTTPS and the HTTPS smoke test passed in serial
evidence. The final capture includes two startup connection retries before
success; this is not a controlled reconnect qualification. The C6 was not flashed.

The root S3 configuration remains S3/16 MB, and an isolated S3 IDF v6.1 regression
build also passes. Crystal's existing card transitions and gesture logic are
retained, with a square 720px dark stylesheet. The user confirms the card
interface is visible and app taps and between-card swipes work. Detailed visual
polish and animation quality still need device acceptance. The host launcher/swipe
regression also passes.

**Remaining content is proposed guidance where not explicitly checked off.**
The tested profile uses 360 MHz CPU and HEX PSRAM, not the generic 400 MHz/OCT
assumptions below. Hardware AES and PPA acceleration are not enabled for Phase 1.
Use the isolated build guide instead of the old root `set-target` instructions
or proposed API snippets. Preserve Crystal OTA/security and persistence
requirements; package/OTA regressions, soak and performance claims remain open.

**Next Phase P4.1 validation:** physically verify the Crystal card interface,
touch/edge gestures, app open/return and animation continuity; repeat a Crystal
Wi-Fi scan and controlled reconnect. Then validate state persistence, packages,
control panel and OTA incrementally. The factory evidence above remains a
separate historical baseline. A complete 32 MB backup of the current factory P4
flash was made before the Crystal flash; it is not the missing original-factory
backup described in that record.

---

## Executive Summary

**Option A** preserves your sophisticated Crystal OS app platform while porting to ESP32-P4 hardware and cherry-picking the compelling visual/UX features from the P4 reference firmware:

✅ **Keep (Your Architecture is Better):**
- App lifecycle system (onCreate/onPause/onResume/onDestroy)
- Package system (.capp format, signatures, registry)
- Declarative runtime (JSON → LVGL)
- Control panel (HTTP-based management)
- CrystalState (NVS persistence)
- All Phase 14-16 work

✅ **Add (Cherry-picked from P4 Example):**
- **New home pill design** (Brookesia 0.4.x navigation bar styling)
- **OS initialization/loading screen** (smooth boot animation)
- **Enhanced animations** (leveraging P4's 400MHz + GPU)
- **Ethernet support** (via BSP, alongside WiFi)
- **Stylesheet system** (dark theme, easier customization)
- **Settings app WiFi UI** (polished scan/connect flow)
- **Camera app foundation** (Phase 18+)

✅ **Gain from P4 Hardware:**
- 4× memory (32MB PSRAM vs 8MB)
- 1.67× CPU speed (400MHz vs 240MHz)
- Hardware 2D GPU (PPA) for blending/rotation
- WiFi 6 (vs WiFi 4)
- Remove S3 workarounds (TLS allocation hacks, DFS locks)

---

## Phase Breakdown

### **Phase P4.1: Hardware Port Foundation** (Week 1, 5-7 days)
Bootstrap Crystal OS on P4 hardware.

### **Phase P4.2: Visual Enhancements** (Week 2, 5-7 days)
Add loading screen, animations, home pill redesign.

### **Phase P4.3: Network Expansion** (Week 2-3, 3-5 days)
Add ethernet, upgrade WiFi patterns.

### **Phase P4.4: Validation & Optimization** (Week 3-4, 5-7 days)
Test, benchmark, optimize for P4 capabilities.

### **Phase P4.5: New Capabilities** (Week 4+, ongoing)
Camera, video, advanced features.

**Total timeline:** 3-4 weeks to feature parity + visual enhancements, then ongoing expansion.

---

## Phase P4.1: Hardware Port Foundation

**Goal:** Get Crystal OS building and running on P4 hardware with existing features working.

**Duration:** 5-7 days  
**Risk:** Medium (WiFi remote driver is new)

### P4.1.1: Build System Updates

**Implemented path:** `sdkconfig.defaults.common`, `sdkconfig.defaults.esp32p4`,
`partitions.esp32p4.csv` and target-gated components. Root S3 defaults are retained.
The diff below is historical planning material, not the applied configuration.

**Files to modify:**
- `sdkconfig.defaults`
- `main/idf_component.yml`
- `partitions.csv`
- `CMakeLists.txt` (if needed)

#### sdkconfig.defaults Changes

```diff
-CONFIG_IDF_TARGET="esp32s3"
+CONFIG_IDF_TARGET="esp32p4"

-CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
+CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y

-CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
+# ESP32-P4 runs at 400MHz by default

-CONFIG_SPIRAM_MODE_OCT=y
-CONFIG_SPIRAM_SPEED_80M=y
+CONFIG_SPIRAM_SPEED_200M=y
+CONFIG_SPIRAM_XIP_FROM_PSRAM=y

+# P4-specific cache config
+CONFIG_CACHE_L2_CACHE_256KB=y
+CONFIG_CACHE_L2_CACHE_LINE_128B=y

-CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2
+CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2
 CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
 CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y

+# Larger LVGL buffers (P4 has 32MB PSRAM)
+CONFIG_LV_LAYER_SIMPLE_BUF_SIZE=102400
+CONFIG_LV_IMG_CACHE_DEF_SIZE=20
+CONFIG_LV_GRAD_CACHE_DEF_SIZE=10240

+# Enable more Montserrat fonts for richer typography
+CONFIG_LV_FONT_MONTSERRAT_10=y
+CONFIG_LV_FONT_MONTSERRAT_12=y
+CONFIG_LV_FONT_MONTSERRAT_18=y
+CONFIG_LV_FONT_MONTSERRAT_22=y
+CONFIG_LV_FONT_MONTSERRAT_24=y
+CONFIG_LV_FONT_MONTSERRAT_26=y
+CONFIG_LV_FONT_MONTSERRAT_30=y
+CONFIG_LV_FONT_MONTSERRAT_32=y

# Test re-enabling hardware AES (P4's better memory may avoid S3 DMA race)
+CONFIG_MBEDTLS_HARDWARE_AES=y
 CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
```

#### main/idf_component.yml Changes

```yaml
dependencies:
  idf: ">=6.1,<6.2"                       # Proposed port constraint, not yet applied
  waveshare/waveshare_boards:
    version: ">=0.3.1"                    # Unified P4 BSP (replaces separate display handling)
  espressif/esp_wifi_remote:
    version: "1.6.5"                      # Tested factory P4 host dependency
  espressif/esp_hosted:
    version: "3.0.10"                     # Host only; retain existing C6 firmware
  esphome/libsodium: "^1.10021.12"
  espressif/mdns: "^1"
```

**Status:** Proposed manifest fragment, not the full tested resolution. Audit the factory dependency snapshot, Board Manager configuration, local BSP and `bsp_extra`; retain Crystal dependencies as needed. Factory success does not prove unchanged Crystal initialization.

#### partitions.csv Changes

```csv
# Name,     Type, SubType,  Offset, Size
nvs,        data, nvs,      ,       0x6000
nvs_keys,   data, nvs_keys, ,       0x1000
otadata,    data, ota,      ,       0x2000
phy_init,   data, phy,      ,       0x1000
coredump,   data, coredump, ,       0x10000
ota_0,      app,  ota_0,    ,       8M      # Doubled from 4M
ota_1,      app,  ota_1,    ,       8M      # Doubled from 4M
storage,    data, spiffs,   ,       4M      # Doubled from 2M
apps,       data, littlefs, ,       10M     # Expanded from 5.5M
```

**Rationale:** P4 has 32MB flash; give apps room to grow (camera, video assets).

### P4.1.2: WiFi Remote Driver Integration

**Implemented correction:** existing `esp_wifi_*` calls use the IDF v6.1 remote
wrapper. No separate `crystal_wifi_p4.cpp` or `esp_wifi_remote_init()` call is
needed. Crystal association, IPv4 and two HTTPS requests passed with the unchanged
C6. The proposed code below is superseded by the implemented HAL path.

**Create:** `components/crystal_core/src/crystal_wifi_p4.cpp`

```cpp
// P4 uses ESP32-C6 companion for WiFi via SDIO
#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "esp_wifi_remote.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "crystal_wifi_p4";

bool crystal_wifi_init_platform()
{
    // Initialize WiFi remote (C6 companion via SDIO)
    esp_err_t ret = esp_wifi_remote_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_remote_init failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Rest of WiFi init is same as S3 (esp_wifi APIs are identical)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "WiFi remote initialized (C6 companion)");
    return true;
}

#endif // CONFIG_IDF_TARGET_ESP32P4
```

**Modify:** `components/crystal_core/src/crystal_core.cpp`

```cpp
bool crystal_wifi_init()
{
#ifdef CONFIG_IDF_TARGET_ESP32P4
    return crystal_wifi_init_platform();
#else
    // S3 native WiFi path (existing code)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&cfg) == ESP_OK;
#endif
}
```

**Rationale:** Isolate platform-specific WiFi initialization; rest of WiFi stack (scan, connect, events) uses same esp_wifi APIs.

### P4.1.3: Display BSP Porting

**Implemented path:** `components/crystal_board_p4` provides Crystal's BSP facade
using Board Manager and `esp_lvgl_adapter` with LVGL 8.4.0. It retains the
adapter's P4 flush alignment and maps Crystal's zero-timeout lock contract to
the adapter's indefinite wait. The user confirms the card screen, app taps and
card swipes work; detailed visual and animation acceptance remains open.

**Modify:** `main/main.cpp` display initialization

```cpp
// S3 uses bsp_display_start() with RGB config
// P4 uses same API but BSP internally uses DPI/MIPI-DSI

lv_display_t *display = bsp_display_start();
require_boot_step(display != nullptr, "Failed to start display");

// P4 BSP may have different resolution - query it
lv_coord_t h_res = lv_display_get_horizontal_resolution(display);
lv_coord_t v_res = lv_display_get_vertical_resolution(display);
ESP_LOGI(TAG, "Display: %dx%d", h_res, v_res);

// Backlight control same across S3/P4
(void)bsp_display_brightness_set(0);
```

**Compatibility audit required:** factory JD9365DA/720x720 display and touch passed; validate Crystal initialization and Brookesia/LVGL APIs separately.

### P4.1.4: Build & Flash Test

**Superseded commands:** follow [the tested isolated-build procedure](ESP32_P4_PHASE1_VALIDATION.md).
The original commands below would change the root S3 target configuration.

```bash
cd "/Users/szemy/Workspace/ESP32 Crystal OS"
git checkout -b feature/esp32-p4-port
idf.py set-target esp32p4
idf.py build
idf.py flash monitor
```

**Success criteria:**
- Firmware builds without errors
- Boots to Brookesia loading animation
- Touch input works
- Home screen appears (even if WiFi doesn't work yet)

**Estimated effort:** 3 days (config, build, initial debugging)

---

## Phase P4.2: Visual Enhancements

**Goal:** Add loading screen, smooth animations, new home pill design.

**Duration:** 5-7 days  
**Risk:** Low (all LVGL/Brookesia features)

### P4.2.1: Brookesia Stylesheet System

The P4 example uses a **stylesheet** for consistent theming. Adopt this pattern.

**Create:** `components/crystal_shell/include/crystal_stylesheet.hpp`

```cpp
#pragma once
#include "esp_brookesia.hpp"

// Crystal OS Dark Theme (based on P4 example's dark stylesheet)
struct CrystalStylesheet {
    ESP_Brookesia_PhoneStylesheet_t phone_stylesheet;
    
    // Navigation bar (home pill)
    ESP_Brookesia_NavigationBarStylesheet_t nav_bar;
    
    // Status bar
    ESP_Brookesia_StatusBarStylesheet_t status_bar;
    
    // App launcher
    ESP_Brookesia_AppLauncherStylesheet_t launcher;
    
    // Gesture overlay
    ESP_Brookesia_GestureStylesheet_t gesture;
    
    // Recents (app switcher)
    ESP_Brookesia_RecentsScreenStylesheet_t recents;
};

const CrystalStylesheet* crystal_stylesheet_dark();
const CrystalStylesheet* crystal_stylesheet_light();
```

**Implementation:** `components/crystal_shell/src/crystal_stylesheet.cpp`

```cpp
#include "crystal_stylesheet.hpp"

// Crystal OS Dark Theme
static const CrystalStylesheet kDarkStylesheet = {
    .phone_stylesheet = {
        // Copy structure from P4 example's dark/stylesheet.h
        // This defines colors, fonts, spacing for entire UI
    },
    .nav_bar = {
        // Home pill colors and size
        .main = {
            .bg_color = 0x1A1A1A,
            .indicator_color = 0xFFFFFF,
            .indicator_width_percent = 30,
            .indicator_height = 4,
            .text_color = 0xCCCCCC,
        }
    },
    // ... status bar, launcher, etc.
};

const CrystalStylesheet* crystal_stylesheet_dark()
{
    return &kDarkStylesheet;
}
```

**Rationale:** Centralized styling makes theme changes easy. P4 example proves this pattern scales.

### P4.2.2: OS Loading Screen

The P4 firmware shows a smooth loading animation during boot. Add this to Crystal OS.

**Create:** `components/crystal_shell/src/crystal_boot_screen.cpp`

```cpp
#include "esp_brookesia.hpp"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "boot_screen";
static lv_obj_t *boot_screen = nullptr;
static lv_obj_t *spinner = nullptr;
static lv_obj_t *label = nullptr;

void crystal_boot_screen_show(const char *message)
{
    if (boot_screen != nullptr) return; // Already showing
    
    boot_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(boot_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(boot_screen, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_border_width(boot_screen, 0, 0);
    lv_obj_clear_flag(boot_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Crystal OS logo/title
    lv_obj_t *title = lv_label_create(boot_screen);
    lv_label_set_text(title, "Crystal OS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -60);
    
    // Spinner (loading animation)
    spinner = lv_spinner_create(boot_screen, 1000, 60);
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x53C8B6), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
    
    // Status message
    label = lv_label_create(boot_screen);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 100);
    
    lv_refr_now(NULL); // Force immediate render
}

void crystal_boot_screen_update(const char *message)
{
    if (label != nullptr) {
        lv_label_set_text(label, message);
    }
}

void crystal_boot_screen_hide()
{
    if (boot_screen != nullptr) {
        // Fade out animation
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, boot_screen);
        lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&anim, 500);
        lv_anim_set_exec_cb(&anim, [](void *obj, int32_t v) {
            lv_obj_set_style_opa((lv_obj_t*)obj, v, 0);
        });
        lv_anim_set_deleted_cb(&anim, [](lv_anim_t *a) {
            lv_obj_del((lv_obj_t*)lv_anim_get_user_data(a));
        });
        lv_anim_set_user_data(&anim, boot_screen);
        lv_anim_start(&anim);
        
        boot_screen = nullptr;
        spinner = nullptr;
        label = nullptr;
    }
}
```

**Integrate into main.cpp:**

```cpp
// Early in app_main, after display init:
bsp_display_lock(0);
crystal_boot_screen_show("Initializing...");
bsp_display_unlock();

// After crystal_hal_init:
crystal_boot_screen_update("Loading packages...");

// After packages mount:
crystal_boot_screen_update("Starting system...");

// After phone->begin():
crystal_boot_screen_update("Ready");
vTaskDelay(pdMS_TO_TICKS(500));
crystal_boot_screen_hide();
```

**Why:** Professional boot experience. P4's speed makes animations smooth.

### P4.2.2b: Advanced Boot Loading UI (Factory Pattern)

The **P4 factory firmware** demonstrates a more sophisticated boot UI with staged progress, atomic updates, and error handling. This is production-ready and improves on the simple pattern above.

**Reference:** Factory firmware `/main/main.cpp` lines 95–244, `BootLoadingUi` class.

**Upgrade:** Replace simple `crystal_boot_screen.cpp` with this advanced pattern:

```cpp
// components/crystal_shell/src/crystal_boot_ui.cpp
#include <atomic>
#include "esp_brookesia.hpp"
#include "lvgl.h"

class BootLoadingUi {
public:
    static constexpr uint32_t STATUS_UPDATE_PERIOD_MS = 30;   // Refresh at ~33fps
    static constexpr uint32_t READY_HOLD_MS = 300;            // Hold "Ready" for 300ms
    static constexpr uint32_t FADE_OUT_TIME_MS = 240;         // Fade-out duration
    
    bool create() {
        root_ = lv_obj_create(lv_layer_top());
        if (!root_) return false;
        
        spinner_ = lv_spinner_create(root_);
        title_ = lv_label_create(root_);
        phase_ = lv_label_create(root_);   // "LOADING" / "READY" / "STARTUP FAILED"
        status_ = lv_label_create(root_);  // Dynamic status text
        progress_ = lv_bar_create(root_);
        
        if (!spinner_ || !title_ || !phase_ || !status_ || !progress_) {
            destroy();
            return false;
        }
        
        // Root: full-screen dark background
        lv_obj_remove_style_all(root_);
        lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(root_, lv_color_hex(0x101417), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE); // Block user interaction during boot
        
        // Spinner: 64×64 teal, centered top
        lv_obj_set_size(spinner_, 64, 64);
        lv_obj_align(spinner_, LV_ALIGN_CENTER, 0, -110);
        lv_obj_set_style_arc_width(spinner_, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner_, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(spinner_, lv_color_hex(0x313B42), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner_, lv_color_hex(0x27C1A8), LV_PART_INDICATOR); // Teal
        
        // Title: "Crystal OS" 32px white
        lv_label_set_text(title_, "Crystal OS");
        lv_obj_set_style_text_font(title_, &lv_font_montserrat_32, LV_PART_MAIN);
        lv_obj_set_style_text_color(title_, lv_color_hex(0xF7F9FA), LV_PART_MAIN);
        lv_obj_align(title_, LV_ALIGN_CENTER, 0, -36);
        
        // Phase: "LOADING" 16px yellow
        lv_label_set_text(phase_, "LOADING");
        lv_obj_set_style_text_font(phase_, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(phase_, lv_color_hex(0xF2B84B), LV_PART_MAIN); // Yellow
        lv_obj_align(phase_, LV_ALIGN_CENTER, 0, 4);
        
        // Status: 18px gray, scrolling text with ellipsis
        lv_obj_set_width(status_, lv_pct(84));
        lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(status_, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_color(status_, lv_color_hex(0xA8B1B8), LV_PART_MAIN); // Light gray
        lv_obj_align(status_, LV_ALIGN_CENTER, 0, 44);
        
        // Progress bar: 52% wide, 7px tall, teal indicator
        lv_obj_set_size(progress_, lv_pct(52), 7);
        lv_obj_align(progress_, LV_ALIGN_CENTER, 0, 82);
        lv_bar_set_range(progress_, 0, 100);
        lv_obj_set_style_radius(progress_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(progress_, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(progress_, lv_color_hex(0x2A343B), LV_PART_MAIN);      // Dark gray
        lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(progress_, lv_color_hex(0x27C1A8), LV_PART_INDICATOR); // Teal
        lv_obj_set_style_bg_opa(progress_, LV_OPA_COVER, LV_PART_INDICATOR);
        
        // Update timer: runs every 30ms to check for pending progress updates
        update_timer_ = lv_timer_create(onUpdateTimer, STATUS_UPDATE_PERIOD_MS, this);
        if (!update_timer_) {
            destroy();
            return false;
        }
        
        setStageNow("Preparing display...", 8);
        return true;
    }
    
    // Thread-safe: call from any FreeRTOS task to queue a status update
    void queueStage(const char *status, int progress) {
        if (!status) return;
        progress = (progress < 0) ? 0 : (progress > 100) ? 100 : progress;
        
        pending_status_.store(status, std::memory_order_relaxed);
        pending_progress_.store(progress, std::memory_order_relaxed);
        pending_revision_.fetch_add(1, std::memory_order_release);
    }
    
    // Show error state and stop animation
    void showError(const char *status) {
        if (!root_) return;
        stopUpdateTimer();
        lv_obj_add_flag(spinner_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(phase_, "STARTUP FAILED");
        lv_obj_set_style_text_color(phase_, lv_color_hex(0xF06A6A), LV_PART_MAIN); // Red
        lv_label_set_text(status_, status);
        lv_obj_set_style_text_color(status_, lv_color_hex(0xF7F9FA), LV_PART_MAIN);
        lv_bar_set_value(progress_, 100, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(progress_, lv_color_hex(0xF06A6A), LV_PART_INDICATOR); // Red bar
        lv_obj_move_foreground(root_);
    }
    
    // Finish boot and fade out
    void finish() {
        if (!root_) return;
        stopUpdateTimer();
        setStageNow("Ready", 100);
        
        // Opacity fade-out with delay
        lv_anim_t fade;
        lv_anim_init(&fade);
        lv_anim_set_var(&fade, root_);
        lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&fade, FADE_OUT_TIME_MS);
        lv_anim_set_delay(&fade, READY_HOLD_MS);
        lv_anim_set_exec_cb(&fade, [](void *obj, int32_t opacity) {
            lv_obj_set_style_opa((lv_obj_t *)obj, opacity, LV_PART_MAIN);
        });
        lv_anim_set_completed_cb(&fade, lv_obj_delete_anim_completed_cb);
        lv_anim_start(&fade);
        
        clearObjectPointers(); // Animation owns root_ now
    }
    
    void destroy() {
        stopUpdateTimer();
        if (root_) lv_obj_del(root_);
        clearObjectPointers();
    }
    
private:
    lv_obj_t *root_ = nullptr;
    lv_obj_t *spinner_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *phase_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *progress_ = nullptr;
    lv_timer_t *update_timer_ = nullptr;
    
    // Atomic cross-task communication
    std::atomic<const char *> pending_status_ = nullptr;
    std::atomic<int> pending_progress_ = {0};
    std::atomic<int> pending_revision_ = {0};
    int applied_revision_ = -1;
    
    void clearObjectPointers() {
        root_ = spinner_ = title_ = phase_ = status_ = progress_ = nullptr;
        update_timer_ = nullptr;
    }
    
    void stopUpdateTimer() {
        if (update_timer_) {
            lv_timer_del(update_timer_);
            update_timer_ = nullptr;
        }
    }
    
    void setStageNow(const char *status, int progress) {
        if (status_ && phase_) {
            lv_label_set_text(status_, status);
            lv_bar_set_value(progress_, progress, LV_ANIM_OFF);
        }
    }
    
    static void onUpdateTimer(lv_timer_t *timer) {
        BootLoadingUi *ui = (BootLoadingUi *)lv_timer_get_user_data(timer);
        int rev = ui->pending_revision_.load(std::memory_order_acquire);
        if (rev != ui->applied_revision_) {
            const char *status = ui->pending_status_.load(std::memory_order_relaxed);
            int progress = ui->pending_progress_.load(std::memory_order_relaxed);
            ui->setStageNow(status, progress);
            ui->applied_revision_ = rev;
        }
    }
};
```

**Integration in main.cpp:**

```cpp
static BootLoadingUi *g_boot_ui = nullptr;

extern "C" void app_main(void) {
    // ... early initialization ...
    
    bsp_display_lock(0);
    g_boot_ui = new BootLoadingUi();
    g_boot_ui->create();
    bsp_display_unlock();
    
    // Boot stages with atomic progress updates
    g_boot_ui->queueStage("Initializing NVS...", 8);
    nvs_flash_init(); // Your code
    
    g_boot_ui->queueStage("Mounting SPIFFS...", 15);
    bsp_spiffs_mount(); // Your code
    
    g_boot_ui->queueStage("Starting display...", 25);
    lv_display_t *disp = bsp_display_start(); // Your code
    
    g_boot_ui->queueStage("Loading Crystal OS...", 50);
    ESP_Brookesia_Phone *phone = new ESP_Brookesia_Phone(); // Your code
    
    g_boot_ui->queueStage("Starting apps...", 75);
    phone->begin(); // Your code
    
    g_boot_ui->queueStage("Installing packages...", 90);
    // Install apps from registry
    
    g_boot_ui->queueStage("Ready", 100);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    bsp_display_lock(0);
    g_boot_ui->finish(); // Fade out and delete
    delete g_boot_ui;
    g_boot_ui = nullptr;
    bsp_display_unlock();
    
    // Normal operation continues...
}
```

**Why this pattern:**
- **Atomic updates:** `std::atomic` allows task-safe progress updates from any FreeRTOS task
- **Responsive:** 30ms timer ensures smooth animation (60fps at LVGL refresh rate)
- **Error handling:** `showError()` for graceful startup failures
- **Professional polish:** Fade-out animation, color-coded phases (yellow loading, red error)
- **Porting reference:** compare tested source and Crystal APIs before adapting; this is not a verified drop-in implementation

**Estimated effort:** 2 days (implementation + testing on hardware)

### P4.2.3: Enhanced Home Pill (Navigation Bar)

Brookesia 0.4.x has improved navigation bar styling. Update to match P4 example's design.

**Modify:** `components/crystal_shell/src/crystal_shell.cpp`

```cpp
// After phone initialization, apply stylesheet:
auto *stylesheet = crystal_stylesheet_dark();
phone->addStylesheet(stylesheet->phone_stylesheet);
phone->activateStylesheet(stylesheet->phone_stylesheet);

// Navigation bar gets modern home pill design automatically
// via stylesheet (no code changes needed)
```

**Visual changes:**
- Pill is centered, thinner, more modern
- Color matches theme (white on dark)
- Smoother animations when swiping up

**Rationale:** Free upgrade from Brookesia 0.4.x stylesheet system.

### P4.2.4: Smooth Page Transitions

P4's GPU and CPU enable richer animations. Increase transition budgets.

**Modify:** `components/crystal_shell/src/crystal_shell.cpp`

```cpp
// When switching apps, use slide animation (not just delete/create)
void crystal_shell_switch_app(CrystalApp *next_app)
{
    if (current_app == next_app) return;
    
    // Slide-out current app
    if (current_app != nullptr) {
        lv_obj_t *old_root = current_app->root();
        if (old_root != nullptr) {
            lv_anim_t anim_out;
            lv_anim_init(&anim_out);
            lv_anim_set_var(&anim_out, old_root);
            lv_anim_set_values(&anim_out, 0, -lv_obj_get_width(old_root));
            lv_anim_set_time(&anim_out, 300); // Fast on P4
            lv_anim_set_exec_cb(&anim_out, [](void *obj, int32_t v) {
                lv_obj_set_x((lv_obj_t*)obj, v);
            });
            lv_anim_set_path_cb(&anim_out, lv_anim_path_ease_in_out);
            lv_anim_start(&anim_out);
        }
        current_app->pause();
        current_app->destroy();
    }
    
    // Slide-in next app
    current_app = next_app;
    if (current_app != nullptr) {
        current_app->create();
        current_app->start();
        current_app->resume();
        
        lv_obj_t *new_root = current_app->root();
        if (new_root != nullptr) {
            lv_obj_set_x(new_root, lv_obj_get_width(new_root)); // Start off-screen
            lv_anim_t anim_in;
            lv_anim_init(&anim_in);
            lv_anim_set_var(&anim_in, new_root);
            lv_anim_set_values(&anim_in, lv_obj_get_width(new_root), 0);
            lv_anim_set_time(&anim_in, 300);
            lv_anim_set_exec_cb(&anim_in, [](void *obj, int32_t v) {
                lv_obj_set_x((lv_obj_t*)obj, v);
            });
            lv_anim_set_path_cb(&anim_in, lv_anim_path_ease_in_out);
            lv_anim_start(&anim_in);
        }
    }
}
```

**Rationale:** P4's 400MHz CPU handles 300ms slide animations at 60fps. Feels premium.

### P4.2.5: Settings App WiFi UI (Cherry-pick)

The P4 Settings app has a polished WiFi scan/connect UI. Adopt it.

**Option 1: Port entire Settings app**
- Copy `components/apps/setting/` from P4 example
- Integrate with Crystal lifecycle (wrap in CrystalApp)
- Adapt to Crystal's WiFi event system

**Option 2: Cherry-pick WiFi UI patterns**
- Keep current Settings overlay architecture
- Adopt P4's WiFi list rendering (RSSI icons, spinner during scan)
- Adopt P4's password keyboard layout

**Recommendation:** Option 2 (less disruptive, keeps your overlay model).

**Files to modify:**
- `components/crystal_shell/src/crystal_settings.cpp` (WiFi scan UI)
- Add RSSI signal strength icons (weak/moderate/good)
- Add spinner overlay during scan
- Improve password keyboard layout

**Estimated effort for Phase P4.2:** 7 days (stylesheet 1d, basic boot screen 1d, advanced boot UI 2d, animations 1.5d, WiFi UI 1.5d)

---

## Phase P4.3: Network Expansion

**Goal:** Add ethernet support, test WiFi 6 stability, improve network reliability.

**Duration:** 3-5 days  
**Risk:** Low (P4 BSP has ethernet driver)

### P4.3.1: Ethernet Support

ESP32-P4 board has ethernet via onboard PHY. Add support alongside WiFi.

**Create:** `components/crystal_core/include/crystal_ethernet.hpp`

```cpp
#pragma once
#include <functional>

enum class CrystalNetworkType {
    None,
    WiFi,
    Ethernet,
};

using crystal_network_callback_t = std::function<void(CrystalNetworkType, bool connected)>;

bool crystal_ethernet_init();
bool crystal_ethernet_start();
void crystal_ethernet_stop();
bool crystal_ethernet_is_connected();
void crystal_network_set_callback(crystal_network_callback_t cb);
CrystalNetworkType crystal_network_active_type();
```

**Implementation:** `components/crystal_core/src/crystal_ethernet.cpp`

```cpp
#include "crystal_ethernet.hpp"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "crystal_eth";
static esp_eth_handle_t s_eth_handle = nullptr;
static esp_netif_t *s_eth_netif = nullptr;
static bool s_eth_connected = false;
static crystal_network_callback_t s_network_cb = nullptr;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet link up");
            s_eth_connected = false; // Wait for IP
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet link down");
            s_eth_connected = false;
            if (s_network_cb) s_network_cb(CrystalNetworkType::Ethernet, false);
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet stopped");
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_ETH_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Ethernet got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            s_eth_connected = true;
            if (s_network_cb) s_network_cb(CrystalNetworkType::Ethernet, true);
        }
    }
}

bool crystal_ethernet_init()
{
    // Initialize TCP/IP stack (if not already done by WiFi)
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop (shared with WiFi)
    // ESP_ERROR_CHECK(esp_event_loop_create_default()); // Already done
    
    // Create eth netif
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    
    // Initialize Ethernet driver (P4 BSP provides eth_init)
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    
    // P4 board-specific PHY (check BSP docs for correct PHY type)
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config, nullptr);
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config); // Or correct PHY
    
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &s_eth_handle));
    
    // Attach driver to netif
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle)));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, eth_event_handler, nullptr));
    
    ESP_LOGI(TAG, "Ethernet initialized");
    return true;
}

bool crystal_ethernet_start()
{
    if (s_eth_handle == nullptr) return false;
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
    return true;
}

void crystal_ethernet_stop()
{
    if (s_eth_handle != nullptr) {
        esp_eth_stop(s_eth_handle);
    }
}

bool crystal_ethernet_is_connected()
{
    return s_eth_connected;
}

void crystal_network_set_callback(crystal_network_callback_t cb)
{
    s_network_cb = cb;
}

CrystalNetworkType crystal_network_active_type()
{
    if (s_eth_connected) return CrystalNetworkType::Ethernet;
    if (crystal_wifi_is_connected()) return CrystalNetworkType::WiFi;
    return CrystalNetworkType::None;
}
```

**Add to main.cpp:**

```cpp
// After WiFi init:
if (crystal_ethernet_init()) {
    crystal_ethernet_start();
    ESP_LOGI(TAG, "Ethernet enabled alongside WiFi");
}

// Set network callback for status bar updates:
crystal_network_set_callback([](CrystalNetworkType type, bool connected) {
    // Update status bar icon (WiFi or Ethernet symbol)
    if (type == CrystalNetworkType::Ethernet && connected) {
        // Show ethernet icon in status bar
    }
});
```

**Settings UI:**
- Add "Network" section with tabs: WiFi | Ethernet
- Ethernet tab shows: Link Status, IP Address, Speed (auto/100M/1G)
- No password needed for ethernet (just plug in cable)

**Rationale:** P4 has ethernet PHY onboard. Professional setups prefer wired.

### P4.3.2: EthernetInfo App (Factory Pattern)

The **P4 factory firmware** includes a production-ready `EthernetInfo` app that displays real-time ethernet stats. Use this as a reference implementation to validate your ethernet driver and show network status.

**Reference:** Factory firmware `/components/HardwareApps/EthernetInfo.cpp` and `.hpp`.

**Pattern: Info Row Helper**

The factory uses a reusable helper for name/value pairs:

```cpp
// components/crystal_app/src/crystal_ethernet_info.cpp
#include "crystal_app.hpp"
#include "esp_netif_ip_addr.h"
#include "esp_eth.h"

namespace {

// Reusable info row: name on left, value on right
lv_obj_t* addInfoRow(lv_obj_t *parent, const char *name, lv_obj_t **value_out) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 72);
    lv_obj_set_style_pad_hor(row, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Name label (left)
    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    
    // Value label (right), with ellipsis if long
    lv_obj_t *value_label = lv_label_create(row);
    lv_obj_set_width(value_label, lv_pct(64));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xCCCCCC), LV_PART_MAIN); // Dimmed
    lv_label_set_text(value_label, "--");
    
    *value_out = value_label;
    return row;
}

} // namespace

class EthernetInfoApp : public CrystalApp {
public:
    EthernetInfoApp() : CrystalApp("Ethernet Info") {}
    
    void onCreate() override {
        root_ = lv_obj_create(NULL);
        lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(root_, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
        
        // Scrollable container for info rows
        scroll_ = lv_obj_create(root_);
        lv_obj_set_size(scroll_, lv_pct(100), lv_pct(100));
        lv_obj_set_flex_flow(scroll_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(scroll_, LV_DIR_VER);
        
        // Create info rows
        addInfoRow(scroll_, "MAC Address", &mac_label_);
        addInfoRow(scroll_, "IPv4 Address", &ipv4_label_);
        addInfoRow(scroll_, "IPv6 Address", &ipv6_label_);
        addInfoRow(scroll_, "RX Packets", &rx_pkt_label_);
        addInfoRow(scroll_, "TX Packets", &tx_pkt_label_);
        addInfoRow(scroll_, "RX Bytes", &rx_bytes_label_);
        addInfoRow(scroll_, "TX Bytes", &tx_bytes_label_);
        addInfoRow(scroll_, "Link Status", &link_status_label_);
        
        updateStats(); // Initial read
        
        // Periodic update timer (1 second)
        lv_timer_create([](lv_timer_t *t) {
            ((EthernetInfoApp *)lv_timer_get_user_data(t))->updateStats();
        }, 1000, this);
    }
    
    void onPause() override {
        // Stats are read-only, no state to save
    }
    
    void onResume() override {
        updateStats();
    }
    
    void onDestroy() override {
        // LVGL handles cleanup
    }
    
private:
    lv_obj_t *root_ = nullptr;
    lv_obj_t *scroll_ = nullptr;
    lv_obj_t *mac_label_ = nullptr;
    lv_obj_t *ipv4_label_ = nullptr;
    lv_obj_t *ipv6_label_ = nullptr;
    lv_obj_t *rx_pkt_label_ = nullptr;
    lv_obj_t *tx_pkt_label_ = nullptr;
    lv_obj_t *rx_bytes_label_ = nullptr;
    lv_obj_t *tx_bytes_label_ = nullptr;
    lv_obj_t *link_status_label_ = nullptr;
    
    void updateStats() {
        // Query ethernet stats via esp_netif API
        esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (!eth_netif) {
            lv_label_set_text(link_status_label_, "Not initialized");
            return;
        }
        
        // MAC address
        uint8_t mac[6];
        esp_netif_get_mac(eth_netif, mac);
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        setLabelIfChanged(mac_label_, mac_str);
        
        // IPv4
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(eth_netif, &ip_info) == ESP_OK) {
            char ip_str[16];
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            setLabelIfChanged(ipv4_label_, ip_info.ip.addr != 0 ? ip_str : "Not assigned");
        }
        
        // Link status
        if (esp_netif_is_netif_up(eth_netif)) {
            setLabelIfChanged(link_status_label_, "Connected");
        } else {
            setLabelIfChanged(link_status_label_, "Disconnected");
        }
        
        // RX/TX stats from esp_eth API (if available)
        // Note: May need to add stat counters to crystal_ethernet.cpp
        setLabelIfChanged(rx_bytes_label_, "--");  // Placeholder
        setLabelIfChanged(tx_bytes_label_, "--");  // Placeholder
    }
    
    void setLabelIfChanged(lv_obj_t *label, const char *text) {
        if (label && strcmp(lv_label_get_text(label), text) != 0) {
            lv_label_set_text(label, text);
        }
    }
};
```

**Integration:**

Add to your app registry (Phase 14+ package system):

```cpp
// In crystal_registry init or app installation
EthernetInfoApp *eth_app = new EthernetInfoApp();
crystal_registry_install_app(eth_app, "EthernetInfo", CRYSTAL_ORIGIN_BUILTIN);
```

**Why:**
- **Reference implementation:** Proves ethernet driver is working
- **Real-time monitoring:** Shows MAC, IP, link status, packet counts
- **Reusable pattern:** `addInfoRow()` helper applies to any settings/info screen
- **Zero dependencies:** Uses only standard esp_netif APIs

**Estimated effort:** 1 day (implement + test on hardware)

### P4.3.3: Network Preference System

**Create:** `components/crystal_core/src/crystal_network_policy.cpp`

```cpp
// Policy: prefer ethernet over WiFi when both connected
// (lower latency, more stable)

static void enforce_network_policy()
{
    bool eth_up = crystal_ethernet_is_connected();
    bool wifi_up = crystal_wifi_is_connected();
    
    if (eth_up && wifi_up) {
        // Both connected: prefer ethernet, set WiFi to background
        ESP_LOGI(TAG, "Ethernet active, WiFi in standby");
        // esp_wifi_set_ps(WIFI_PS_MAX_MODEM); // Power-save WiFi
    } else if (!eth_up && wifi_up) {
        ESP_LOGI(TAG, "WiFi active");
    } else if (eth_up && !wifi_up) {
        ESP_LOGI(TAG, "Ethernet active");
    } else {
        ESP_LOGW(TAG, "No network connection");
    }
}
```

**Rationale:** Transparent failover between ethernet and WiFi.

**Estimated effort for Phase P4.3:** 5 days (ethernet driver 2d, EthernetInfo app 1d, network policy 1d, testing 1d)

---

## Phase P4.4: Validation & Optimization

**Goal:** Verify all Crystal OS features work on P4, optimize for P4 capabilities.

**Duration:** 5-7 days  
**Risk:** Low (mostly testing)

### P4.4.1: Feature Validation Checklist

Test every Crystal OS feature on P4:

#### Boot & Display
- [ ] Loading screen appears with spinner
- [ ] Smooth fade-in to home screen
- [ ] Touch input works (calibration if needed)
- [ ] Status bar shows time/battery/network
- [ ] Home pill appears at bottom (new design)

#### Networking
- [ ] WiFi connects to known AP
- [ ] WiFi reconnects after router reboot
- [ ] Ethernet auto-detects when cable plugged
- [ ] Ethernet has priority over WiFi
- [ ] mDNS announces on active interface
- [ ] HTTPS client works (weather API)
- [ ] TLS session reuse works

#### App Platform
- [ ] All bundled apps launch (Clock, Weather, Calculator)
- [ ] Declarative Dashboard loads from JSON
- [ ] Lifecycle hooks fire (onCreate/onPause/onResume/onDestroy)
- [ ] State persists across launches (NVS)
- [ ] Package installation works (upload .capp)
- [ ] Signature verification works

#### Control Panel
- [ ] HTTP server starts on port 8080
- [ ] Web UI loads in browser
- [ ] Log streaming works
- [ ] App upload works
- [ ] WiFi config works from panel

#### Gestures & UI
- [ ] Swipe up from bottom opens app launcher
- [ ] Swipe down from top opens quick settings
- [ ] Swipe left/right switches apps (if >1 running)
- [ ] Back gesture works
- [ ] Keyboard appears/dismisses smoothly
- [ ] Animations are smooth (60fps)

#### Performance
- [ ] LVGL benchmark shows 60fps+ (run lv_demo_benchmark)
- [ ] Weather fetch doesn't block UI
- [ ] OTA update streams without freezing
- [ ] Memory usage stable (no leaks over 24h)

#### Stability
- [ ] 24-hour soak test (no crashes)
- [ ] NVS encryption works (no corruption)
- [ ] Coredump to flash on panic
- [ ] Watchdog doesn't fire during normal use

### P4.4.2: Performance Optimization

**Goal:** Leverage P4's superior hardware.

#### Test Hardware AES Re-enablement

```bash
# In sdkconfig.defaults, we set:
CONFIG_MBEDTLS_HARDWARE_AES=y

# Test with repeated HTTPS requests:
# If stable, this speeds up TLS by ~30%
```

**If unstable:** Revert to `CONFIG_MBEDTLS_HARDWARE_AES=n` (S3 workaround).

#### Benchmark LVGL Performance

```cpp
// Add to Dev Tester app:
void benchmark_lvgl()
{
    lv_demo_benchmark();
    // Expect: 60fps on all tests (P4's 400MHz + GPU)
    // S3 was ~45fps on complex scenes
}
```

**Expected results:**
- S3: ~45fps on complex gradients/blends
- P4: 60fps consistently (hardware PPA acceleration)

#### Increase Animation Budgets

```cpp
// In crystal_shell.cpp, increase transition times:
// S3 used 200ms (to stay within 16ms frames)
// P4 can do 300ms smoothly

#define CRYSTAL_ANIM_APP_SWITCH_MS   300  // Was 200
#define CRYSTAL_ANIM_FADE_MS         400  // Was 250
#define CRYSTAL_ANIM_SLIDE_MS        350  // Was 200
```

**Why:** Slower animations feel more premium when they're perfectly smooth.

#### Profile Memory Usage

```cpp
// After 1 hour of use, log heap status:
size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

ESP_LOGI(TAG, "Memory: Internal=%zu KB, PSRAM=%zu/%zu KB",
         free_internal / 1024, free_psram / 1024, total_psram / 1024);

// P4 should have 24MB+ PSRAM free after apps loaded
// (vs S3 which had <2MB free)
```

### P4.4.3: Regression Testing

Run existing test suite (if any):

```bash
cd tools
python test_app_platform_v2.py    # Phase 14-16 tests
python test_control_panel.mjs      # HTTP control tests
python test_crystal_declarative.py # JSON runtime tests
```

**All tests must pass** before declaring P4 port complete.

**Estimated effort for Phase P4.4:** 6 days (testing 4d, optimization 2d)

---

## Phase P4.5: New Capabilities (Post-Parity)

**Goal:** Add features impossible/impractical on S3.

**Duration:** Ongoing (2-4 weeks per feature)  
**Risk:** Medium (new domains)

### P4.5.1: Camera App (Phase 18)

P4 BSP includes OV5647 MIPI camera with ISP pipeline.

**Features:**
- Live viewfinder (720x720 @ 50fps)
- Photo capture (save to SPIFFS)
- Face detection overlay (using `components/human_face_detect`)
- Settings: brightness, contrast, saturation

**Reference:** P4 example's `components/apps/camera/` (527 lines).

**Integration:** Wrap in CrystalApp, add to launcher.

**Estimated effort:** 1 week

### P4.5.2: Video Player (Phase 19)

P4 has hardware H.264 decoder.

**Features:**
- Play `.mp4` files from SPIFFS/SD card
- Hardware decode @ 720p/30fps
- Playback controls (play/pause/seek)

**Dependencies:**
```yaml
# In main/idf_component.yml:
esp_video:
  version: "0.8.*"
espressif/esp_h264:
  version: "==1.0.3"
```

**Reference:** P4 example's `components/apps/video_player/`.

**Estimated effort:** 1 week

### P4.5.3: Advanced Diagnostics

P4's extra memory enables richer developer tools.

**Features:**
- **Memory profiler:** Live heap graph (internal + PSRAM)
- **CPU profiler:** Task utilization chart
- **Network monitor:** Bandwidth graph (WiFi vs Ethernet)
- **Frame time graph:** LVGL render performance

**Implementation:** Extend Dev Tester app with charts (LVGL `lv_chart`).

**Estimated effort:** 3-5 days

### P4.5.4: Larger Declarative UIs

P4's 32MB PSRAM allows bigger JSON UIs.

**Current limit:** ~64KB JSON (S3 parser buffer limit)  
**New limit:** 256KB JSON (4× larger)

**Use cases:**
- Richer dashboards (more widgets, charts)
- Settings pages with many options
- Data tables (transit schedules, logs)

**Change:**
```cpp
// In components/crystal_declarative/src/crystal_declarative.cpp:
-#define MAX_JSON_SIZE (64 * 1024)
+#define MAX_JSON_SIZE (256 * 1024)
```

**Estimated effort:** 1 day (testing only, no code changes)

---

## Design Guide: P4-Specific Patterns

### DG.1: Animation Design for P4

**Principle:** P4's 400MHz CPU + GPU enable richer animations without sacrificing responsiveness.

#### Timing Guidelines

| Animation Type | S3 Duration | P4 Duration | Notes |
|----------------|-------------|-------------|-------|
| Page transition | 200ms | 300ms | Smoother on P4 |
| Fade in/out | 250ms | 400ms | More gradual |
| Button press | 100ms | 100ms | Keep snappy |
| Spinner rotation | 1000ms | 1000ms | No change |
| Slide drawer | 200ms | 350ms | Can be slower |

**Rationale:** Longer animations feel premium when they maintain 60fps. P4 can do this; S3 struggled.

#### Easing Functions

Use **ease-in-out** for page transitions:

```cpp
lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
```

Avoid **linear** (feels robotic) and **bounce** (gimmicky).

#### GPU Acceleration

LVGL automatically uses P4's PPA (Pixel Processing Accelerator) for:
- Image rotation/scaling
- Alpha blending
- Color space conversion

**No code changes needed** - just enable in sdkconfig:

```
CONFIG_LV_USE_DRAW_PPA=y  # Enable hardware acceleration
```

### DG.2: Memory Management for P4

**Principle:** P4 has 4× the PSRAM (32MB vs 8MB). Use it wisely.

#### Allocation Strategy

| Resource | S3 Strategy | P4 Strategy | Why |
|----------|-------------|-------------|-----|
| LVGL draw buffers | 2× 60KB | 2× 120KB | Bigger screens need larger buffers |
| HTTP response buffer | 64KB | 128KB | Larger API responses fit |
| Image cache | 5 images | 20 images | More icons/assets in RAM |
| Declarative JSON | 64KB | 256KB | Richer UIs |
| TLS buffers | PSRAM (16KB) | PSRAM (16KB) | No change |

**Don't over-allocate:** 32MB sounds like a lot, but camera buffers (720×720×2 = 1MB) and video decode (2MB+) consume it fast.

#### Heap Monitoring

```cpp
void crystal_memory_report()
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    
    ESP_LOGI(TAG, "PSRAM: %zu KB free / %zu KB total (%.1f%% used)",
             info.total_free_bytes / 1024,
             info.total_allocated_bytes / 1024,
             100.0 * info.total_allocated_bytes / 
                     (info.total_allocated_bytes + info.total_free_bytes));
}
```

Call this every 10 seconds in Dev Tester to spot leaks.

### DG.3: Network Redundancy (WiFi + Ethernet)

**Principle:** P4 supports WiFi and Ethernet simultaneously. Design for transparent failover.

#### Connection Policy

1. **Both connected:** Prefer ethernet (lower latency)
2. **Ethernet only:** Use ethernet
3. **WiFi only:** Use WiFi
4. **Neither:** Show error, retry both

#### HTTP Client Updates

```cpp
// In crystal_http.cpp, bind to active interface:
esp_err_t crystal_http_get(const char *url, crystal_http_response_t *response)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        // Bind to active interface (eth0 or wlan0)
        .if_name = crystal_network_active_interface(),
    };
    
    // Rest of HTTP client code...
}
```

#### Status Bar Display

```cpp
// Show different icons for WiFi vs Ethernet:
if (crystal_network_active_type() == CrystalNetworkType::Ethernet) {
    status_bar->setNetworkIcon(ICON_ETHERNET_CONNECTED);
} else if (crystal_network_active_type() == CrystalNetworkType::WiFi) {
    status_bar->setWifiIconState(signal_strength);
} else {
    status_bar->setNetworkIcon(ICON_NETWORK_DISCONNECTED);
}
```

### DG.4: Boot Sequence Design

**Principle:** Show progress, hide delays.

#### Loading Screen Stages

```cpp
// Stage 1: Hardware init (500ms)
crystal_boot_screen_show("Initializing hardware...");
bsp_display_start();
crystal_hal_init();

// Stage 2: Storage (300ms)
crystal_boot_screen_update("Loading packages...");
crystal_packages_mount();

// Stage 3: Network (2s if WiFi connects)
crystal_boot_screen_update("Connecting to network...");
crystal_wifi_init();
// Don't block on connection - continue boot

// Stage 4: System ready (100ms)
crystal_boot_screen_update("Starting system...");
phone->begin();

// Stage 5: Fade out (500ms)
crystal_boot_screen_update("Ready");
vTaskDelay(pdMS_TO_TICKS(500));
crystal_boot_screen_hide();
```

**Total perceived boot time:** ~3.5s (P4 is fast).

**S3 comparison:** ~4.5s (slower CPU, slower PSRAM).

---

## Code Guide: P4-Specific Patterns

### CG.1: WiFi Remote Initialization

```cpp
// components/crystal_core/src/crystal_wifi_p4.cpp

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "esp_wifi_remote.h"

bool crystal_wifi_init()
{
    // Step 1: Initialize remote driver (C6 companion via SDIO)
    esp_err_t ret = esp_wifi_remote_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi remote init failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Step 2: Standard WiFi init (same as S3 after this point)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set mode failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "WiFi initialized (remote via C6)");
    return true;
}

#endif // CONFIG_IDF_TARGET_ESP32P4
```

**Unverified legacy sketch:** derive initialization and transport settings from tested factory source; do not assume remaining S3 APIs and ordering are identical under v6.1.

### CG.2: Loading Screen Integration

```cpp
// main/main.cpp

extern "C" void app_main()
{
    // ... early init ...
    
    lv_display_t *display = bsp_display_start();
    (void)bsp_display_brightness_set(0); // Dark until first frame ready
    
    bsp_display_lock(0);
    
    // Show loading screen immediately
    crystal_boot_screen_show("Initializing...");
    (void)bsp_display_brightness_set(100); // Fade in to loading screen
    
    bsp_display_unlock();
    
    // Stage-by-stage updates:
    crystal_boot_screen_update("Loading packages...");
    crystal_packages_init(...);
    
    crystal_boot_screen_update("Connecting to network...");
    crystal_wifi_init();
    
    crystal_boot_screen_update("Starting system...");
    auto *phone = new ESP_Brookesia_Phone(display);
    phone->begin();
    
    // All apps installed, ready to show UI
    crystal_boot_screen_update("Ready");
    vTaskDelay(pdMS_TO_TICKS(500));
    crystal_boot_screen_hide(); // Fade out
    
    // Home screen is now visible
}
```

### CG.3: Stylesheet Application

```cpp
// components/crystal_shell/src/crystal_shell.cpp

bool crystal_shell_init(ESP_Brookesia_Phone *phone)
{
    // Apply dark theme stylesheet
    const CrystalStylesheet *theme = crystal_stylesheet_dark();
    
    if (!phone->addStylesheet(theme->phone_stylesheet)) {
        ESP_LOGE(TAG, "Failed to add stylesheet");
        return false;
    }
    
    if (!phone->activateStylesheet(theme->phone_stylesheet)) {
        ESP_LOGE(TAG, "Failed to activate stylesheet");
        return false;
    }
    
    ESP_LOGI(TAG, "Crystal dark theme applied");
    
    // Home pill, status bar, launcher now use themed colors/fonts
    return true;
}
```

### CG.4: Ethernet Initialization

```cpp
// main/main.cpp, after WiFi init:

#ifdef CONFIG_EXAMPLE_USE_ETHERNET
    if (crystal_ethernet_init()) {
        crystal_ethernet_start();
        ESP_LOGI(TAG, "Ethernet started alongside WiFi");
    } else {
        ESP_LOGW(TAG, "Ethernet init failed (no cable?)");
    }
#endif

// Set callback for network state changes:
crystal_network_set_callback([](CrystalNetworkType type, bool connected) {
    const char *type_str = (type == CrystalNetworkType::WiFi) ? "WiFi" :
                           (type == CrystalNetworkType::Ethernet) ? "Ethernet" : "None";
    ESP_LOGI(TAG, "%s %s", type_str, connected ? "connected" : "disconnected");
    
    // Update status bar icon on LVGL task
    crystal_shell_network_event(type, connected);
});
```

### CG.5: Animation Helper

```cpp
// components/crystal_shell/include/crystal_animation.hpp

void crystal_anim_slide_x(lv_obj_t *obj, lv_coord_t start_x, lv_coord_t end_x,
                          uint32_t duration_ms, lv_anim_ready_cb_t ready_cb = nullptr)
{
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_values(&anim, start_x, end_x);
    lv_anim_set_time(&anim, duration_ms);
    lv_anim_set_exec_cb(&anim, [](void *var, int32_t v) {
        lv_obj_set_x((lv_obj_t*)var, v);
    });
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    if (ready_cb) lv_anim_set_ready_cb(&anim, ready_cb);
    lv_anim_start(&anim);
}

// Usage:
lv_obj_t *panel = lv_obj_create(lv_scr_act());
crystal_anim_slide_x(panel, 480, 0, 300); // Slide in from right, 300ms
```

### CG.6: Memory-Aware Image Loading

```cpp
// P4 can cache more images in PSRAM

#ifdef CONFIG_IDF_TARGET_ESP32P4
    #define CRYSTAL_IMAGE_CACHE_SIZE 20  // vs 5 on S3
#else
    #define CRYSTAL_IMAGE_CACHE_SIZE 5
#endif

void crystal_image_cache_init()
{
    lv_img_cache_set_size(CRYSTAL_IMAGE_CACHE_SIZE);
    ESP_LOGI(TAG, "Image cache: %d slots", CRYSTAL_IMAGE_CACHE_SIZE);
}
```

---

## Testing Guide

### TG.1: Boot Screen Test

**Manual test:**
1. Flash firmware
2. Power cycle device
3. Observe:
   - [ ] Screen starts dark
   - [ ] "Crystal OS" title appears with spinner
   - [ ] Status messages update (4-5 stages)
   - [ ] Smooth fade to home screen
   - [ ] Total boot time <4s

**Automated test:**
```python
# tools/test_boot_screen.py
def test_boot_sequence():
    # Flash firmware, monitor serial output
    boot_log = monitor_serial(timeout=10)
    
    # Check stages appear in order
    assert "Initializing hardware" in boot_log
    assert "Loading packages" in boot_log
    assert "Connecting to network" in boot_log
    assert "Starting system" in boot_log
    assert "Ready" in boot_log
    
    # Check timing
    boot_time = extract_boot_time(boot_log)
    assert boot_time < 4.5, f"Boot took {boot_time}s (too slow)"
```

### TG.2: Animation Performance Test

**Benchmark:**
```cpp
// In Dev Tester app, add "Animation Benchmark" button:

void benchmark_animations()
{
    lv_obj_t *panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(panel, 400, 400);
    
    // Slide animation
    uint32_t start_ms = lv_tick_get();
    for (int i = 0; i < 10; i++) {
        crystal_anim_slide_x(panel, 0, 480, 300);
        vTaskDelay(pdMS_TO_TICKS(300));
        crystal_anim_slide_x(panel, 480, 0, 300);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    uint32_t elapsed_ms = lv_tick_get() - start_ms;
    
    // Should be ~6000ms (10 cycles × 600ms)
    float fps = (10 * 300 * 60) / elapsed_ms;
    ESP_LOGI(TAG, "Animation FPS: %.1f (target: 60)", fps);
    
    // Pass if >=55fps (some jitter allowed)
    assert(fps >= 55.0);
}
```

### TG.3: Network Failover Test

**Manual test:**
1. Connect WiFi only → verify weather app works
2. Plug in ethernet cable → verify switches to ethernet
3. Unplug ethernet → verify falls back to WiFi
4. Disconnect WiFi → verify shows "no connection"
5. Reconnect WiFi → verify auto-reconnects

**Automated test:**
```python
# tools/test_network_failover.py
def test_ethernet_priority():
    # Start with WiFi only
    device.connect_wifi("TestAP", "password")
    assert device.get_network_type() == "WiFi"
    
    # Plug ethernet (simulated via relay or manual)
    device.enable_ethernet()
    time.sleep(5)
    assert device.get_network_type() == "Ethernet"
    
    # Unplug ethernet
    device.disable_ethernet()
    time.sleep(5)
    assert device.get_network_type() == "WiFi"
```

### TG.4: Regression Tests (Existing Features)

All Phase 14-16 tests must pass:

```bash
cd tools
python test_app_platform_v2.py       # Pass
python test_control_panel.mjs         # Pass
python test_crystal_declarative.py    # Pass
python test_crystal_signatures.py     # Pass
```

If any fail, root cause before declaring port complete.

---

## Migration Checklist

### Pre-Migration (Week 0)
- [ ] Back up S3 firmware (tag as `v1.0-s3-final`)
- [ ] Review P4 example code (1 day)
- [x] P4 board available: rev1.3 tested in the factory session
- [x] Factory IDF v6.1 build/flash/basic-device prerequisite passed on rev1.3
- [x] Establish isolated Crystal P4 build/sdkconfig paths under IDF v6.1
- [x] Audit the tested P4 dependency/BSP/API path and preserve Crystal security and storage defaults; keep existing C6 firmware unchanged

### Phase P4.1: Hardware Port (Week 1)
- [x] Create branch `feature/esp32-p4-port`
- [x] Add isolated common/P4 defaults and partition profile; leave the S3 defaults intact
- [x] Resolve P4 dependencies after audit (Brookesia/LVGL, Board Manager, display adapter and hosted Wi-Fi)
- [x] Review a 32MB P4 layout preserving Crystal OTA/security/persistence requirements; runtime OTA remains untested
- [x] Route existing Crystal Wi-Fi calls through IDF v6.1's `esp_wifi_remote` wrapper; association/IPv4, SNTP and HTTPS passed with unchanged C6 firmware
- [x] Build and sign firmware successfully; image fits the 8MB OTA slot
- [x] Flash with hash verification and verify Crystal boot, first-frame milestone and four-app shell initialization by serial log
- [ ] Inspect loading/first-frame visual quality and animation continuity
- [x] Verify touch input: user confirms app taps and card swipes work
- [x] Verify Crystal card/home interface appears: user confirms it is visible

### Phase P4.2: Visual Enhancements (Week 2)
- [ ] Implement stylesheet system (`crystal_stylesheet.cpp`)
- [ ] Apply dark theme to Brookesia
- [ ] Implement boot screen (`crystal_boot_screen.cpp`)
- [ ] Integrate boot screen into main.cpp
- [ ] Add page transition animations
- [ ] Cherry-pick WiFi scan UI from P4 example
- [ ] Test all animations at 60fps

### Phase P4.3: Network Expansion (Week 2-3)
- [ ] Implement ethernet driver (`crystal_ethernet.cpp`)
- [ ] Add ethernet to main.cpp init sequence
- [ ] Implement network failover policy
- [ ] Update status bar for ethernet icon
- [ ] Add ethernet tab to Settings overlay
- [ ] Test WiFi ↔ Ethernet failover

### Phase P4.4: Validation (Week 3-4)
- [ ] Run full feature validation checklist
- [ ] Run LVGL performance benchmark
- [ ] Test hardware AES stability
- [ ] Profile memory usage (24h soak test)
- [ ] Run all regression tests
- [ ] Fix any discovered issues

### Phase P4.5: New Capabilities (Week 4+)
- [ ] Port camera app from P4 example
- [ ] Port video player from P4 example
- [ ] Add advanced diagnostics to Dev Tester
- [ ] Increase declarative JSON limit to 256KB
- [ ] Document P4-specific features

### Documentation (Ongoing)
- [ ] Update README.md (add P4 support)
- [ ] Update BUILD.md (P4 build instructions)
- [ ] Update APP_PLATFORM_V2 docs (note P4 as primary target)
- [ ] Write MIGRATION.md (S3 → P4 guide for users)
- [ ] Update memory records (32MB budgets)

### Release (Week 5)
- [ ] Tag `v2.0-p4-beta`
- [ ] Create release notes
- [ ] Test OTA update from v1 (S3) to v2 (P4) — N/A, different hardware
- [ ] Announce Crystal OS v2 on P4

---

## Risk Mitigation

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **WiFi remote driver unstable** | Medium | High | Keep S3 branch as fallback; test thoroughly with 24h soak |
| **Brookesia 0.4.x API changes** | Low | Medium | Check changelog, adapt as needed |
| **P4 BSP display issues** | Low | Medium | P4 BSP is mature; follow Espressif examples |
| **Ethernet PHY not detected** | Low | Low | Check board jumpers/schematics; fallback to WiFi-only |
| **TLS still fails with HW AES** | Medium | Low | Revert to SW AES (known workaround) |
| **Animations choppy despite P4 speed** | Low | Medium | Profile with LVGL perf monitor; tune buffer sizes |
| **Regression in existing features** | Medium | High | Run full test suite; fix before release |
| **Timeline overrun (>5 weeks)** | Medium | Low | Phases P4.5 are optional; can defer camera/video to v2.1 |

---

## Success Criteria

Crystal OS v2 on P4 is **ready for release** when:

1. ✅ All Phase 14-16 features work (lifecycle, packages, declarative, control panel)
2. ✅ Boot screen and animations are smooth (60fps confirmed)
3. ✅ Both WiFi and Ethernet work with automatic failover
4. ✅ All regression tests pass
5. ✅ 24-hour soak test shows no crashes or memory leaks
6. ✅ Performance is measurably better than S3 (LVGL benchmark, boot time)
7. ✅ Documentation is updated for P4

**Optional for v2.0 (can defer to v2.1):**
- Camera app
- Video player
- Advanced diagnostics

---

## Comparison: What Changed from S3

| Feature | S3 (v1) | P4 (v2) | Notes |
|---------|---------|---------|-------|
| **Hardware** |
| CPU | 240MHz Xtensa | 400MHz RISC-V | 1.67× faster |
| PSRAM | 8MB @ 80MHz | 32MB @ 200MHz | 4× capacity, 2.5× bandwidth |
| Flash | 16MB | 32MB | 2× capacity |
| Display | RGB parallel | MIPI-DSI/DPI | Hardware acceleration |
| WiFi | WiFi 4 (native) | WiFi 6 (via C6) | Better reliability, faster |
| Ethernet | No | Yes | Wired networking |
| Camera | No | OV5647 MIPI | New capability |
| GPU | No | PPA (2D) | Hardware blending/rotation |
| **Software** |
| IDF | Historical S3 5.3.x baseline | v6.1 | Factory passed; Crystal pending |
| Brookesia | 0.4.2 | Pending dependency/API audit | No assumed drop-in upgrade |
| Boot screen | No | Yes (animated) | Professional UX |
| Home pill | Basic | Redesigned (themed) | Modern aesthetic |
| Animations | 200ms (choppy) | 300ms (smooth) | Leverages P4 speed |
| WiFi UI | Basic | Polished (RSSI icons) | From P4 example |
| Network | WiFi only | WiFi + Ethernet | Redundancy |
| TLS | SW AES (HW broken) | HW AES (test) | May be fixed on P4 |
| Image cache | 5 slots | 20 slots | More memory |
| JSON limit | 64KB | 256KB | Richer UIs |
| **Performance** |
| Boot time | ~4.5s | ~3.5s | Faster CPU |
| LVGL FPS | ~45fps | 60fps | GPU + CPU |
| Weather fetch | Blocks 2s | Non-blocking | Better async |
| OTA speed | ~5 MB/min | ~8 MB/min | Faster flash |

---

## Appendix A: P4 Hardware Specifications

**ESP32-P4-WIFI6-Touch-LCD-4B Board**

| Component | Specification |
|-----------|---------------|
| MCU | ESP32-P4 (Dual RISC-V @ 400MHz) |
| SRAM | 768KB internal |
| PSRAM | 32MB OCT (200MHz) |
| Flash | 32MB QSPI |
| Display | 4" 480×480 MIPI-DSI IPS (capacitive touch) |
| WiFi | ESP32-C6 companion (WiFi 6, 802.11ax) |
| Ethernet | 10/100Mbps PHY (onboard) |
| Camera | OV5647 MIPI CSI-2 (5MP) |
| Audio | ES8311 codec (I2S) |
| USB | USB-C (serial + JTAG) |
| SD Card | MicroSD slot |
| Buttons | Boot, Reset |
| LEDs | Power, Status |
| GPU | PPA (Pixel Processing Accelerator) for 2D ops |

**Power:**
- 5V/2A via USB-C
- Battery connector (optional)

**Dimensions:**
- PCB: ~100mm × 65mm
- Display: 4" diagonal

---

## Appendix B: File Structure After Migration

```
Crystal OS (P4 branch)
├── components/
│   ├── crystal_app/          # No changes (architecture ports directly)
│   ├── crystal_shell/
│   │   ├── src/
│   │   │   ├── crystal_boot_screen.cpp     # NEW: loading screen
│   │   │   ├── crystal_stylesheet.cpp      # NEW: theme system
│   │   │   └── crystal_shell.cpp          # MODIFIED: animations
│   │   └── include/
│   │       ├── crystal_boot_screen.hpp     # NEW
│   │       └── crystal_stylesheet.hpp      # NEW
│   ├── crystal_core/
│   │   ├── src/
│   │   │   ├── crystal_wifi_p4.cpp        # NEW: WiFi remote
│   │   │   ├── crystal_ethernet.cpp       # NEW: ethernet driver
│   │   │   └── crystal_network_policy.cpp # NEW: failover logic
│   │   └── include/
│   │       ├── crystal_ethernet.hpp        # NEW
│   │       └── crystal_network.hpp         # MODIFIED
│   ├── crystal_declarative/  # MODIFIED: increase JSON limit
│   ├── crystal_control/      # No changes
│   ├── crystal_registry/     # No changes
│   ├── crystal_package/      # No changes
│   ├── crystal_signature/    # No changes
│   ├── camera_app/           # NEW (Phase P4.5)
│   ├── video_app/            # NEW (Phase P4.5)
│   ├── calculator_app/       # No changes
│   ├── clock_app/            # No changes
│   ├── weather_app/          # No changes
│   └── dev_tester/           # MODIFIED: add diagnostics
├── main/
│   └── main.cpp              # MODIFIED: boot screen, ethernet init
├── sdkconfig.defaults        # MODIFIED: P4 target, expanded resources
├── partitions.csv            # MODIFIED: 32MB layout
└── docs/
    ├── MIGRATION_S3_TO_P4.md # NEW
    └── APP_PLATFORM_V2_*.md  # UPDATED: note P4 as primary target
```

**Legend:**
- NEW: Created for P4 port
- MODIFIED: Updated for P4 capabilities
- No changes: Works as-is on P4

---

## Appendix C: Resources & References

### Official Documentation
Historical links below are background references; they do not establish the v6.1 dependency set or identify this board's tested BSP. Use the copied validation record and tested workspace for that baseline.
- [ESP32-P4 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-p4_technical_reference_manual_en.pdf)
- [ESP-IDF 5.5 Migration Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/migration-guides/index.html)
- [ESP-Brookesia Documentation](https://github.com/espressif/esp-brookesia)
- [ESP32-P4 BSP](https://components.espressif.com/components/espressif/esp32-p4-function-ev-board)

### Example Code
- Tested factory: `/Users/szemy/Downloads/ESP32-P4-WIFI6-Touch-LCD-4B-main/examples/factory`
- Ethernet: select a reference during peripheral qualification; traffic not validated

### Community
- [ESP32 Forum - P4 Discussions](https://esp32.com/)
- [Espressif GitHub Issues](https://github.com/espressif/esp-idf/issues)

### Tools
- `idf.py` (ESP-IDF build system)
- `esptool.py` (flashing)
- LVGL Simulator (for UI prototyping)

---

## Next Steps

1. **Review this plan** with stakeholders
2. **Audit and isolate the Crystal P4 environment** under IDF v6.1; factory prerequisite passed
3. **Create migration branch**: `git checkout -b feature/esp32-p4-port`
4. **Start Phase P4.1** (Hardware Port Foundation)

**Estimated timeline:** 3-4 weeks to feature parity + visual enhancements, then ongoing expansion.

**Questions? Review sections:**
- Phase breakdown (timeline concerns)
- Design Guide (architecture questions)
- Code Guide (implementation patterns)
- Risk Mitigation (contingency planning)

---

**End of Crystal OS v2 - ESP32-P4 Migration Plan**
