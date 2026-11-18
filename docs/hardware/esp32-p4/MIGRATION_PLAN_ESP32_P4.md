# Crystal OS Migration Plan: ESP32-S3 → ESP32-P4

**Original plan date:** 2025-01-13; reconciled 2026-11-14 (Asia/Hong_Kong)  
**Target Hardware:** ESP32-P4-WIFI6-Touch-LCD-4B  
**Tested factory reference:** `/Users/szemy/Downloads/ESP32-P4-WIFI6-Touch-LCD-4B-main/examples/factory`  
**Status:** Crystal Phase 1 builds, flashes and boots under IDF v6.1 on rev1.3;
Phase 1.5 and P4.2 visual/interaction work are accepted and closed (2026-11-15).
Phase P4.3 Network Expansion and final status-bar/icon UI are also accepted and
closed (2026-11-15). Continue with v2 P4.4 validation and optimization; sustained
network stability, FPS and soak remain unqualified.

## Baseline reconciliation — 2026-11-14

Use **ESP-IDF v6.1 / ESP32-P4 rev1.3 / `rev1_x`** as the migration baseline.
Read the [summary and assumption corrections](../../reference/esp32-p4-factory/README.md),
[validation record](../../reference/esp32-p4-factory/factory-idf6-validation.md) and
[handoff](../../reference/esp32-p4-factory/CRYSTAL_OS_P4_SESSION_HANDOFF.md).
This section supersedes conflicting original assumptions below.

- Factory source built unchanged; six images flashed with hash verification.
  JD9365DA/720x720 display, touch, Calculator open/close, Wi-Fi scan/association/
  IPv4 and an SNTP message were recorded on rev1.3 with 32 MB flash.
- **Preserve the existing C6 firmware.** Version discovery times out, but Wi-Fi
  works. `coprocessor=0.0.0` does not prove incompatibility. P4 host versions
  1.6.5 (`esp_wifi_remote`) and 3.0.10 (`esp_hosted`) do not identify the C6 version.
- Reconnect, sustained traffic, TLS/application protocols, soak, peripherals and
  revision 3 remain unqualified. microSD timed out with card presence unknown;
  a legacy cached I2C handle diagnostic does not establish physical bus failure.
- No verified complete pre-flash backup exists. Partition table saved; NVS
  preserved by factory writes, while storage expanded and assets were replaced.
- Keep the tested Downloads workspace and `build/idf6-rev1_x/` artifacts separate;
  no exact upstream commit is established. Do not substitute the repo's vendor copy
  or import managed components, SDKs or generated build trees.

**Crystal results are now recorded separately in the
[Phase 1 validation/build guide](ESP32_P4_PHASE1_VALIDATION.md).** The signed
Crystal image passed flash hash verification and boots on rev1.3, initializes
JD9365DA/720x720 and GT911, mounts encrypted NVS/SPIFFS/LittleFS, initializes the
four-app card shell, associates through the unchanged C6, gets IPv4, synchronizes
SNTP/RTC and passes weather plus smoke-test HTTPS requests. Two startup connection
retries were observed before success; controlled reconnect is still unqualified.

The root S3 `sdkconfig` remains S3/16 MB, and a separate S3 IDF v6.1 build passes.
Crystal retains Brookesia 0.4.2, LVGL 8.4.0 and its card animations/gesture logic.
The user confirms the card interface displays and app taps/card swipes respond.
Phase 1.5 and P4.2 visual/interaction acceptance is complete; sustained animation
performance remains unqualified. The tested P4 uses a
360 MHz CPU and HEX PSRAM; 400 MHz/OCT and automatic PPA enablement below are
superseded assumptions. Hardware AES stays disabled.

**Next validation after P4.3 closure:** the 720px UI, network expansion and final
status-bar/rounded-icon fixes have user acceptance. Continue controlled radio
reconnect/timing, state persistence, packages, remaining control-panel operations,
OTA and sustained performance/soak qualification. A full current-factory P4 flash backup was captured before
Crystal was flashed; the older original-firmware backup limitation in the factory
record remains true. Keep that reference workspace separate and preserve the
existing C6 firmware.

---

## Executive Summary

The ESP32-P4 represents a generational leap over the S3:
- **Dual-core RISC-V; tested rev1.3 profile uses 360 MHz** (vs 240 MHz Xtensa)
- **32MB PSRAM** standard (vs 8MB OCT PSRAM on S3)
- **Hardware 2D GPU (PPA)** for accelerated blending/rotation/scaling
- **MIPI-DSI display interface** with DPI support (vs RGB parallel only)
- **WiFi 6** via companion ESP32-C6 (vs WiFi 4 on S3)
- **ESP-IDF v6.1** Crystal build/flash/boot and HTTPS smoke tests verified
- **Brookesia/LVGL:** Crystal retains 0.4.2 / 8.4.0; Phase 1.5/P4.2 visual and interaction checks pass; sustained FPS qualification remains open

**Migration approach:** preserve the Crystal app platform and audit its compatibility with IDF v6.1 and the tested P4 stack. Factory success establishes a hardware baseline, not a direct-port guarantee.

**Recommended approach:** Branch now, port incrementally, maintain S3 as v1 release branch while P4 becomes the v2 development target.

---

## 1. Hardware & Software Stack Comparison

| Dimension | ESP32-S3 (Current) | ESP32-P4 (Target) | Migration Impact |
|-----------|-------------------|-------------------|------------------|
| **CPU** | Dual Xtensa LX7 @ 240MHz | Dual RISC-V @ 360MHz on tested rev1.3 | PM uses valid P4 360/180 MHz points |
| **RAM** | 512KB SRAM | 768KB SRAM | 🟢 More breathing room |
| **PSRAM** | 8MB OCT @ 80MHz | 32MB HEX @ 200MHz in tested profile | Capacity verified; performance not benchmarked |
| **Display** | RGB parallel (16-bit) | MIPI-DSI + DPI | 🟡 BSP config changes |
| **GPU** | Software only | Hardware PPA (2D) | 🟢 LVGL can use it |
| **WiFi** | Integrated WiFi 4 | ESP32-C6 via SDIO (WiFi 6) | 🟡 Driver changes (esp_wifi_remote) |
| **Flash** | 16MB | 32MB standard | 🟢 More space for apps |
| **Camera** | No BSP support | OV5647 MIPI + ISP | 🟢 New capability |
| **IDF Version** | Current baseline 6.1; older plan used 5.3.x | v6.1 | Crystal P4 and S3 builds pass |
| **Brookesia** | 0.4.2 | 0.4.2 / LVGL 8.4.0 | Card display, app taps and swipes user-confirmed; detailed visual checks pending |
| **BSP** | waveshare/esp32_s3_touch_lcd_4b | Board Manager + local P4 BSP/bsp_extra | Audit initialization and APIs |

**Planning scope:** Wi-Fi and display are known porting areas; the dependency/API and security audit may identify additional work.

---

## 2. Intended Reuse (extent unverified)

### **Crystal OS Core Architecture** (preserve design; validate compatibility)
- `crystal_app.hpp/cpp`: Lifecycle, CrystalState, CrystalApp base class
- `crystal_shell.hpp/cpp`: Gesture handling, keyboard overlay, status bar updates
- `crystal_registry.hpp/cpp`: App installation registry (NVS-based)
- `crystal_package.hpp/cpp`: `.capp` format parser
- `crystal_signature.hpp/cpp`: RSA-3072 signature verification
- `crystal_declarative.hpp/cpp`: JSON UI runtime
- `crystal_control.hpp/cpp`: HTTP control panel
- `crystal_core.hpp`: Time, WiFi status, weather reading structs

**Why:** All built on esp-brookesia's `ESP_Brookesia_Phone` abstraction, which exists on P4.

### ✅ **App Platform v2 Components**
- Declarative runtime (Phase 14-16)
- Package manifest parsing
- Resource tracking and lifecycle
- NVS-based state persistence
- HTTP control panel
- Signature verification

**Why:** ESP32-P4 still runs FreeRTOS + LVGL + esp-brookesia. Your abstractions sit above the hardware.

### ✅ **Existing Apps** (With minor BSP updates)
- Clock app
- Weather app (HK transit API client)
- Calculator app
- Dev Tester app
- Declarative Dashboard example

**Why:** These use LVGL APIs and Crystal abstractions, not hardware-specific code.

---

## 3. Known Porting Areas (scope to confirm)

### 🟡 **3.1 WiFi Stack** (Medium effort)

**S3 approach:**
```cpp
// Direct esp_wifi APIs
esp_wifi_init()
esp_wifi_set_mode(WIFI_MODE_STA)
esp_wifi_connect()
```

**P4 starting point:** tested host versions below; preserve the existing C6. The legacy initialization sketch must be checked against actual tested factory code.
```yaml
# idf_component.yml
dependencies:
  espressif/esp_wifi_remote:
    version: "1.6.5"
  espressif/esp_hosted:
    version: "3.0.10"  # P4 host, not installed C6 version
```
```cpp
// WiFi runs on companion C6, accessed via SDIO
#include "esp_wifi_remote.h"
esp_wifi_remote_init()  // Unverified sketch: check actual hosted API/order
```

**Migration steps:**
1. Add `esp_wifi_remote` dependency to `main/idf_component.yml`
2. Audit actual hosted initialization and supported Wi-Fi APIs; do not mechanically replace includes
3. Update WiFi init sequence in `main.cpp`
4. Test reconnection logic (SDIO adds latency vs native WiFi)

**Estimated effort:** 1-2 days (mostly testing)

---

### 🟡 **3.2 Display BSP Configuration** (Low-medium effort)

**S3 config (RGB parallel):**
```c
CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2
CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y
```

**P4 config (MIPI-DSI/DPI):**
```c
CONFIG_IDF_TARGET="esp32p4"
CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2
CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y
```

**Changes needed:**
- Update `sdkconfig.defaults`: TARGET, flash size (32MB), SPIRAM speed (200MHz)
- Compare tested Board Manager/local BSP/bsp_extra initialization and headers
- Validate actual display APIs against Crystal Brookesia/LVGL dependencies
- Target the tested JD9365DA panel at 720x720

**Memory configuration:**
```c
// P4 has 32MB PSRAM @ 200MHz
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y  // Execute code from PSRAM
CONFIG_CACHE_L2_CACHE_256KB=y
CONFIG_CACHE_L2_CACHE_LINE_128B=y
```

**Estimated effort:** 1 day (config + verification)

---

### 🟢 **3.3 TLS/HTTPS Stack** (Already solved better)

**Your S3 workaround:**
```c
# Phase 9.7: PSRAM allocator for TLS buffers
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_MBEDTLS_HARDWARE_AES=n  # Workaround for PSRAM DMA issues
```

**P4 advantage:** 32MB PSRAM + faster CPU means TLS buffers fit comfortably in PSRAM without the hardware AES workaround. You may be able to **re-enable hardware AES** on P4.

**Action:** Test with `CONFIG_MBEDTLS_HARDWARE_AES=y` after porting. P4's memory architecture may eliminate the DMA race condition.

---

### 🟢 **3.4 Partition Table** (Expand)

**S3 layout (16MB flash):**
```
ota_0:    4M
ota_1:    4M
storage:  2M (SPIFFS)
apps:     5.5M (LittleFS for packages)
```

**Proposed P4 layout (32MB flash):**
```
ota_0:    8M   (2× for richer firmware)
ota_1:    8M
storage:  4M   (SPIFFS for assets)
apps:     10M  (LittleFS for more packages)
```

**Why:** P4 can support camera apps, video playback, and larger declarative UIs. Give them room.

---

### 🟢 **3.5 Performance Optimizations** (P4 unlocks)

**Remove S3 workarounds:**
1. **DFS (Dynamic Frequency Scaling):** S3 had `esp_pm min must equal max` memory rule because DFS breaks LVGL. P4's faster base clock (400MHz) may make DFS viable again.
2. **LVGL buffer sizes:** Increase `CONFIG_LV_LAYER_SIMPLE_BUF_SIZE` from current (unknown) to P4 example's 102,400 bytes.
3. **Font loading:** P4 example enables Montserrat 8-48px. You can afford richer typography.
4. **Animation frame budget:** 400MHz CPU + 2D GPU means smoother 60fps transitions.

---

## 4. New Capabilities on P4

### 📹 **4.1 Camera Integration**

P4 BSP includes OV5647 MIPI camera support. Reference example has:
- `components/apps/camera/Camera.cpp`
- Face detection (`human_face_detect`)
- Pedestrian detection (`pedestrian_detect`)
- ISP pipeline with autofocus

**Opportunity:** Add a camera app to Crystal OS. The P4 example uses ~720×720 resolution for AI workloads.

**Suggested phase:** Phase 18 (post Lua scripting)

---

### 🎥 **4.2 Video Playback**

P4 example includes H.264 hardware decode:
```yaml
# idf_component.yml
esp_video:
  version: "0.8.*"
espressif/esp_h264:
  version: "==1.0.3"
```

**Opportunity:** Video player app (MJPEG or H.264 from SD/SPIFFS).

**Suggested phase:** Phase 19 (media platform)

---

### 🎨 **4.3 Hardware-Accelerated UI**

P4's PPA (Pixel Processing Accelerator) can offload:
- Image rotation/scaling
- Alpha blending
- Color space conversion

**LVGL integration:** Likely auto-enabled via BSP config. Verify with benchmark app.

---

## 5. Migration Strategy

### **Phase 1: Branch & Bootstrap** (Week 1)
**Goal:** Get Crystal OS building for P4 target.

1. [x] Work on `feature/esp32-p4-port`.
2. [x] Add `sdkconfig.defaults.common` and `sdkconfig.defaults.esp32p4` and build in `build/p4-idf61-phase1c`; keep the root S3 `sdkconfig.defaults` and `sdkconfig` unchanged.
3. [x] Resolve IDF v6.1 P4 dependencies for LVGL 8/Brookesia 0.4.2, Board Manager, the Waveshare board and hosted Wi-Fi. Use the Board Manager display/touch path for the 720x720 panel.
4. [x] Add a 32MB partition layout with dual 8MB OTA slots, 4MB SPIFFS, 10MB LittleFS and Crystal's signing/security settings. OTA behavior is not runtime-validated.
5. [x] Keep Crystal's existing Wi-Fi API and link IDF v6.1's `esp_wifi_remote` wrapper; its `esp_wifi_init()` forwards to the remote implementation. Association/IPv4, SNTP and HTTPS passed with the existing C6.
6. [x] Build and sign with ESP-IDF v6.1; the image fits the 8MB OTA slot.
7. [x] Flash with hash verification; verify Crystal boot, panel/touch initialization, card-shell startup and first-frame milestone in the serial log.
8. [x] Build the isolated S3 target and run the host launcher/swipe regression.
9. [x] Confirm card interface visibility, app taps and between-card swipes on device (user-confirmed).
10. [ ] Inspect detailed layout/animation quality and exercise all apps/edge/interrupted gestures.

**Current checkpoint:** Implementation and boot/network smoke tests pass; card
display, app taps and card swipes are confirmed, while detailed UI and broader
feature qualification remain open. Follow the
[isolated build and validation guide](ESP32_P4_PHASE1_VALIDATION.md), not old root
`set-target` snippets. The C6 firmware remains unchanged. The Phase 2/3 tasks below
overlap this bootstrap; retain their unperformed acceptance checks rather than
repeating or replacing the working board/network implementation.

---

### **Phase 2: Display & Bootloader** (Week 1-2)
**Goal:** See Brookesia loading screen on P4 hardware.

**Closure reconciliation — 2026-11-15:** Phase 1 already met the display and
bootloader foundation. The corresponding Phase P4.2 visual work is now closed
after device acceptance of the Crystal logo/theme/pill, immediate Settings,
Wi-Fi scan/password layouts, enlarged keyboard and compact Wi-Fi status. Final
host regressions pass. See the
[closure checkpoint](ESP32_P4_PHASE2_VALIDATION.md#closure-checkpoint--2026-11-15).
Sustained FPS and memory/soak qualification continue under v2 Phase P4.4 and this
plan's performance/stability checklist. No 60fps achievement is claimed; preserve
the existing C6 firmware and separate tested factory workspace.

**Reconciled checkpoint — 2026-11-14:** Phase 1 already establishes display,
touch, the card shell and working C6 Wi-Fi; Phase 1.5 layouts/typography have
user acceptance. Continue with **Phase P4.2 Visual Enhancements** in the v2 plan,
rather than replacing the existing board path or deferring working networking.
The staged Crystal boot UI, reusable dark stylesheet, Settings transitions,
thin home pill and Wi-Fi view polish now build and flash under IDF v6.1, and
startup/SNTP/Weather/HTTPS plus host regressions pass. See
[Phase P4.2 validation](ESP32_P4_PHASE2_VALIDATION.md). Physical acceptance was
open at that checkpoint and is now complete. Sustained profiling continues in
P4.4; 60fps is not established with the current 30ms redraw period. No C6
firmware update is part of this work.

**Settings design update — 2026-11-15:** The user replaces the slow Settings fade
with immediate entry/category/Back transitions, as on S3, and skips the Settings
app backdrop snapshot to avoid extra image RAM. This supersedes animated Settings
proposals while retaining the boot and existing shell animations. The user
confirms immediate Settings entry, categories, repeated Back and app return
work correctly (2026-11-15), without launcher flashes or stuck controls. This
acceptance is not a claim of 60fps animation performance.

1. Port `main.cpp` initialization:
   - Update BSP display config for DPI interface
   - Adapt Brookesia Phone initialization after dependency/API audit
2. Flash & verify boot sequence
3. Confirm LVGL renders (loading animation, status bar)
4. Qualify LVGL performance in P4.4; 60fps remains an unverified target, not an assumption
5. **Checkpoint:** Brookesia home screen appears, touch works

**Deliverable:** Interactive Crystal UI with staged loading and coherent motion;
preserve the already working Wi-Fi baseline and existing C6 firmware.

---

### **Phase 3: WiFi & Networking** (Week 2)
**Current scope — 2026-11-15:** Wi-Fi restoration already passed in Phase 1,
and its Settings/scanning UX passed in P4.2. The v2 plan's P4.3 Network Expansion
is now closed for its accepted scope. Continue with P4.4 validation and
optimization, preserving the working C6 firmware. See
[the Phase P4.3 validation record](ESP32_P4_PHASE3_VALIDATION.md).

The implementation adds HAL-owned IP101 Ethernet (IDF v6.1, espressif/ip101
1.1.0), explicit Ethernet-first IPv4 routing, per-interface DHCP DNS, Wi-Fi
fallback, coherent Network/Ethernet Settings and a P4 Ethernet diagnostic card.
Weather/SNTP/HTTPS and opt-in control-panel readiness accept either interface.
P4 retains its SDK ADC1 entropy source for Ethernet-only crypto; C6 Wi-Fi is
not a local P4 entropy source.

Verified: 100 Mbps link, DHCP 192.168.1.233 / 255.255.255.0, gateway
192.168.1.1; Ethernet selected as default while Wi-Fi also obtains an address;
SNTP synchronization, live Weather and HTTPS 200. The user confirms cable
removal switches Settings to WiFi and shows Ethernet disconnected; reconnecting
restores Ethernet as active with an assigned address. The updated build passes
Wi-Fi-off Weather, SNTP and HTTPS, followed by three user-confirmed manual Weather
refreshes. An earlier Weather timeout and its unresolved precise cause are
retained in the validation record. The Ethernet-only local panel serves its
browser pairing page, pairs successfully and loads the authenticated installed-app
list (user-confirmed, Wi-Fi off). The user accepts Ethernet Settings/app details,
scrolling and return navigation. A fresh Weather refresh succeeds after enabling
Wi-Fi and unplugging Ethernet (user-confirmed). The user confirms the final
Ethernet/Wi-Fi indicator, 2px downward status-bar offset, rounded launcher icons
while pressed and uncropped rounded switcher artwork all pass on device.
This closes P4.3 acceptance on 2026-11-15; no new flash-verification or serial
evidence is implied by this user confirmation. The device pairing dialog has
P4-specific typography and proportional sizing, with passing P4/S3 host layout checks. The user confirms
readable dialog text with no clipping or overlap; readability acceptance passes.
This is not full network qualification or a sustained radio/traffic test.

**Original Wi-Fi checklist (historical scope):**

1. Implement `esp_wifi_remote` initialization
2. Port WiFi credential storage (NVS remains same)
3. Test connection, reconnection, AP scanning
4. Verify HTTPS client works (weather API test)
5. Test control panel (HTTP server + mDNS)
6. **Checkpoint:** Weather app fetches live data

**Deliverable:** P4.3 Ethernet/Wi-Fi network expansion accepted; sustained
network qualification continues in v2 P4.4.

---

### **Phase 4: App Platform Validation** (Week 3)
**Goal:** All Crystal OS features working.

1. Test each bundled app:
   - Clock (should just work)
   - Weather (needs WiFi from Phase 3)
   - Calculator (should just work)
   - Dev Tester (verify gesture/lifecycle)
2. Test declarative runtime:
   - Load Declarative Dashboard from `examples/`
   - Verify dynamic lists, charts, timers
3. Test control panel:
   - Log streaming
   - App installation (upload `.capp`)
   - WiFi configuration
4. Test package installation flow (Phase 14)
5. **Checkpoint:** Feature parity with S3 version

**Deliverable:** All v1 features operational on P4

---

### **Phase 5: P4-Specific Optimizations** (Week 4)
**Goal:** Leverage P4 advantages.

1. **Performance:**
   - Enable hardware AES for TLS (test stability)
   - Increase LVGL buffer sizes
   - Test DFS (may still need lock for LVGL)
2. **Memory:**
   - Profile PSRAM usage (should have headroom)
   - Consider larger HTTP response buffers (>64KB)
3. **UI polish:**
   - Test richer animations (GPU acceleration)
   - Add more Montserrat font weights
4. **Partition expansion:**
   - Update OTA partitions to 8MB each
   - Expand apps partition to 10MB
5. **Checkpoint:** Measurably smoother/faster than S3

**Deliverable:** Optimized P4 build

---

### **Phase 6: New Capabilities** (Future)
**Goal:** Add P4-exclusive features.

1. **Camera app** (Phase 18):
   - Integrate OV5647 BSP APIs
   - Build viewfinder app
   - Optional: face detection overlay
2. **Video player** (Phase 19):
   - Integrate `esp_video` + H.264 decode
   - Load from SPIFFS/SD
3. **Enhanced diagnostics:**
   - Use extra RAM for richer logs
   - Performance monitor overlay

**Deliverable:** Features impossible on S3

---

## 6. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| **WiFi remote driver instability** | Medium | High | Keep S3 branch as fallback; test thoroughly |
| **Display BSP API changes** | Low | Medium | P4 BSP is mature; check espressif docs |
| **TLS buffer allocation on P4** | Low | Medium | Test early; may be non-issue with 32MB PSRAM |
| **Brookesia API breakage (0.4.x)** | Low | Low | Minor version should be compatible |
| **IDF v6.1 compatibility** | Unassessed | High | Factory passed; audit/build/test Crystal separately |
| **Hardware availability** | Low | High | You already have the board |
| **Maintenance burden (2 platforms)** | High | Medium | Accept S3 as frozen v1; P4 is v2+ |

---

## 7. Testing Checklist

### **Boot & Display**
- [ ] Firmware boots to Brookesia loading screen
- [ ] Touch input works (calibration if needed)
- [x] Status bar shows time/battery/network icons; Ethernet/Wi-Fi indicator and 2px offset accepted in P4.3
- [ ] Home screen gesture (swipe from bottom) works
- [ ] App launcher grid appears

### **Networking**
- [ ] WiFi connects to known AP
- [ ] WiFi reconnects after router reboot
- [ ] mDNS announces `crystal-os.local`
- [ ] HTTPS client fetches weather data
- [ ] HTTP server serves control panel
- [ ] TLS session reuse works (no handshake spam)

### **App Platform**
- [ ] Clock app launches, shows time
- [ ] Weather app fetches HK transit data
- [ ] Calculator app computes correctly
- [ ] Dev Tester app shows lifecycle events
- [ ] Declarative Dashboard loads from JSON
- [ ] Control panel uploads `.capp` package
- [ ] Package signature verification works
- [ ] App registry survives reboot

### **Lifecycle**
- [ ] onCreate/onStart/onResume fire in order
- [ ] onPause persists state before switch
- [ ] onDestroy cleans up resources
- [ ] Keyboard overlay appears/dismisses
- [ ] Quick Settings panel works
- [ ] Back gesture peels navigation stack

### **Performance**
- [ ] LVGL maintains 60fps (benchmark app)
- [ ] No screen tearing (direct mode + double buffer)
- [ ] Animations are smooth (page transitions)
- [ ] Weather fetch doesn't block UI
- [ ] OTA update doesn't freeze display

### **Stability**
- [ ] 24-hour soak test (no crashes)
- [ ] Memory usage stable (no leaks)
- [ ] WiFi stays connected overnight
- [ ] NVS encryption works (no corruption)
- [ ] Coredump to flash on panic

---

## 8. Code Structure Recommendations

### **Maintain S3 as v1 Release Branch**
```
main              → ESP32-S3 (Crystal OS v1)
feature/esp32-p4  → ESP32-P4 (Crystal OS v2 development)
```

**Rationale:** P4 is the future, but S3 users need a stable branch for bug fixes. Don't merge P4 changes back to main until P4 is the primary target.

### **Conditional Compilation (Minimize)**
Avoid `#ifdef CONFIG_IDF_TARGET_ESP32P4` everywhere. Instead:

1. **Platform abstraction layer** (`crystal_hal.hpp`):
   ```cpp
   // crystal_hal.hpp
   bool crystal_wifi_init();        // S3: esp_wifi, P4: esp_wifi_remote
   bool crystal_display_init();     // S3: RGB, P4: DPI
   ```

2. **BSP wrapper:**
   ```cpp
   // Already exists; keep using bsp/esp-bsp.h
   // BSP handles S3 vs P4 differences
   ```

3. **Build-time selection:**
   - `sdkconfig.defaults.esp32s3`
   - `sdkconfig.defaults.esp32p4`
   - `idf.py -DIDF_TARGET=esp32p4 build`

---

## 9. Documentation Updates

After successful port:

1. **README.md:**
   - Add "Supported Hardware" section
   - Link to S3 vs P4 comparison
2. **BUILD.md:**
   - Document P4-specific build flags
   - List P4 BSP requirements
3. **Memory records:**
   - Update PSRAM budgets (32MB)
   - Update partition layout
4. **Phase 14-17 docs:**
   - Note P4 as primary target for v2+
   - Update resource quotas (higher on P4)

---

## 10. Timeline Estimate

| Phase | Duration | Dependencies | Risk |
|-------|----------|--------------|------|
| 1. Branch & Bootstrap | 2-3 days | None | Low |
| 2. Display & Bootloader | 3-5 days | Phase 1 | Low |
| 3. WiFi & Networking | 3-5 days | Phase 2 | Medium |
| 4. App Platform Validation | 5-7 days | Phase 3 | Low |
| 5. P4 Optimizations | 3-5 days | Phase 4 | Low |
| **Total (Phases 1-5)** | **3-4 weeks** | - | - |
| 6. New Capabilities | 2-4 weeks | Phase 5 | Medium |

**Critical path:** WiFi remote driver (Phase 3). If that's unstable, the port stalls.

**Best case:** 3 weeks to feature parity  
**Likely case:** 4 weeks to feature parity  
**Worst case:** 6 weeks if WiFi driver needs debugging

---

## 11. Decision Point: When to Switch Primary Development

**Option A: Immediate switch (Recommended)**
- Branch now, port in 4 weeks, make P4 the v2 target
- S3 becomes maintenance-only (bug fixes, no new features)
- **Pros:** Clean break, focus energy on better hardware
- **Cons:** S3 users stuck on v1 forever

**Option B: Parallel development**
- Maintain feature parity across S3 and P4
- **Pros:** Both platforms get new features
- **Cons:** 2× maintenance burden, slower progress

**Option C: Delayed switch**
- Finish Phase 17 (Lua runtime) on S3 first, then port everything
- **Pros:** Less code to port (all phases done once)
- **Cons:** Delays leveraging P4's advantages by months

**Recommendation:** **Option A**. The P4 is objectively superior, and your architecture ports cleanly. Cut over now before investing more in S3-specific optimizations.

---

## 12. Next Steps

1. **Read P4 example code** (1 day):
   - Study tested factory main, local BSP/bsp_extra, profile defaults and generated Board Manager configuration
   - Compare app structure to your Crystal apps
   - Note any Brookesia API differences

2. **Create migration branch:**
   ```bash
   cd "/Users/szemy/Workspace/ESP32 Crystal OS"
   git checkout -b feature/esp32-p4-port
   ```

3. **Update build config** (Phase 1):
   - Establish isolated P4 sdkconfig/build paths; selectively adapt tested rev1_x settings
   - Merge your TLS/NVS/security settings
   - Update `idf_component.yml`

4. **First build attempt:**
   ```bash
   idf.py set-target esp32p4
   idf.py build
   ```

5. **Document build errors** → iterative fixing

---

## Conclusion

**Migration feasibility: ✅ HIGH**

Preserve Crystal OS architecture and reuse its app platform. The earlier 80% unchanged / 20% porting estimate is unverified; factory v6.1 success does not establish Crystal API compatibility or implementation effort.

**The P4 is your future.** It removes S3's memory constraints, TLS allocation hacks, and performance ceilings. Camera and video capabilities open new app categories. WiFi 6 improves reliability.

**Recommendation:** Continue from the existing Phase 1 P4 build, then verify boot, display, touch, home screen and hosted Wi-Fi on hardware before moving on. Keep S3 as the v1 maintenance target.

The architecture work you've done (lifecycle, packages, signatures, declarative runtime) **pays off here**—it ports to new hardware with minimal friction. This is exactly what platform abstraction is for.
