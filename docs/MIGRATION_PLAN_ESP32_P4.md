# Crystal OS Migration Plan: ESP32-S3 → ESP32-P4

**Original plan date:** 2025-01-13; reconciled 2026-11-14 (Asia/Hong_Kong)  
**Target Hardware:** ESP32-P4-WIFI6-Touch-LCD-4B  
**Tested factory reference:** `/Users/szemy/Downloads/ESP32-P4-WIFI6-Touch-LCD-4B-main/examples/factory`  
**Status:** Factory v6.1 smoke test passed on rev1.3; Crystal migration planning

## Baseline reconciliation — 2026-11-14

Use **ESP-IDF v6.1 / ESP32-P4 rev1.3 / `rev1_x`** as the migration baseline.
Read the [summary and assumption corrections](reference/esp32-p4-factory/README.md),
[validation record](reference/esp32-p4-factory/factory-idf6-validation.md) and
[handoff](reference/esp32-p4-factory/CRYSTAL_OS_P4_SESSION_HANDOFF.md).
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

Factory testing is complete; **Crystal OS implementation and tests remain pending**.
Current Crystal inputs still select S3/16 MB flash, Brookesia 0.4.2 and LVGL 8.4.0.
Audit Board Manager/local BSP/bsp_extra, hosted initialization and dependency APIs.
Retained code/configuration snippets, performance claims and estimates below are
proposals, not validated v6.1 implementations. Do not assume unchanged APIs or
copy factory partitions over Crystal OTA/security/persistence requirements.

**Next implementation step:** preserve current work and audit dependencies/security;
establish separate P4 build/sdkconfig paths, port minimal board/display/touch/PSRAM,
then hosted Wi-Fi with the working C6. Validate Crystal lifecycle, persistence,
networking, packages and OTA before optional peripherals and UI enhancements.
This reconciliation changes documentation only; no implementation or flashing occurred.

---

## Executive Summary

The ESP32-P4 represents a generational leap over the S3:
- **Dual-core 400MHz RISC-V** (vs 240MHz Xtensa dual-core)
- **32MB PSRAM** standard (vs 8MB OCT PSRAM on S3)
- **Hardware 2D GPU (PPA)** for accelerated blending/rotation/scaling
- **MIPI-DSI display interface** with DPI support (vs RGB parallel only)
- **WiFi 6** via companion ESP32-C6 (vs WiFi 4 on S3)
- **ESP-IDF v6.1** factory baseline verified; Crystal build/port pending
- **Brookesia/LVGL:** audit tested reference; Crystal currently pins 0.4.2 / 8.4.0

**Migration approach:** preserve the Crystal app platform and audit its compatibility with IDF v6.1 and the tested P4 stack. Factory success establishes a hardware baseline, not a direct-port guarantee.

**Recommended approach:** Branch now, port incrementally, maintain S3 as v1 release branch while P4 becomes the v2 development target.

---

## 1. Hardware & Software Stack Comparison

| Dimension | ESP32-S3 (Current) | ESP32-P4 (Target) | Migration Impact |
|-----------|-------------------|-------------------|------------------|
| **CPU** | Dual Xtensa LX7 @ 240MHz | Dual RISC-V @ 400MHz | 🟢 Faster, no code changes |
| **RAM** | 512KB SRAM | 768KB SRAM | 🟢 More breathing room |
| **PSRAM** | 8MB OCT @ 80MHz | 32MB OCT @ 200MHz | 🟢 4× capacity, 2.5× speed |
| **Display** | RGB parallel (16-bit) | MIPI-DSI + DPI | 🟡 BSP config changes |
| **GPU** | Software only | Hardware PPA (2D) | 🟢 LVGL can use it |
| **WiFi** | Integrated WiFi 4 | ESP32-C6 via SDIO (WiFi 6) | 🟡 Driver changes (esp_wifi_remote) |
| **Flash** | 16MB | 32MB standard | 🟢 More space for apps |
| **Camera** | No BSP support | OV5647 MIPI + ISP | 🟢 New capability |
| **IDF Version** | Historical 5.3.x baseline | v6.1 | Factory passed; Crystal audit pending |
| **Brookesia** | 0.4.2 | Pending dependency/API audit | Compatibility unverified |
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

1. Create branch: `git checkout -b feature/esp32-p4-port main`
2. Update `sdkconfig.defaults`:
   - `CONFIG_IDF_TARGET="esp32p4"`
   - SPIRAM/cache settings from P4 example
   - Update partition flash size to 32MB
3. Update `main/idf_component.yml`:
   - Add `espressif/esp_wifi_remote`
   - Integrate tested Board Manager/local P4 BSP in isolated P4 configuration
4. Review a 32MB P4 layout preserving Crystal OTA/security/persistence requirements
5. Stub out WiFi init with `esp_wifi_remote` (can be non-functional)
6. Build firmware: `idf.py build`
7. **Checkpoint:** Firmware builds without errors

**Deliverable:** Compiling firmware (may not boot yet)

---

### **Phase 2: Display & Bootloader** (Week 1-2)
**Goal:** See Brookesia loading screen on P4 hardware.

1. Port `main.cpp` initialization:
   - Update BSP display config for DPI interface
   - Adapt Brookesia Phone initialization after dependency/API audit
2. Flash & verify boot sequence
3. Confirm LVGL renders (loading animation, status bar)
4. Benchmark LVGL performance (should see ~60fps easily)
5. **Checkpoint:** Brookesia home screen appears, touch works

**Deliverable:** Interactive UI (no WiFi yet)

---

### **Phase 3: WiFi & Networking** (Week 2)
**Goal:** Restore WiFi connectivity.

1. Implement `esp_wifi_remote` initialization
2. Port WiFi credential storage (NVS remains same)
3. Test connection, reconnection, AP scanning
4. Verify HTTPS client works (weather API test)
5. Test control panel (HTTP server + mDNS)
6. **Checkpoint:** Weather app fetches live data

**Deliverable:** Full network stack operational

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
- [ ] Status bar shows time/battery/WiFi icons
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

**Recommendation:** Branch now (Phase 1), get it booting (Phase 2), validate WiFi (Phase 3), and declare P4 the Crystal OS v2 platform within 4 weeks. Keep S3 as the v1 maintenance branch but stop adding features to it.

The architecture work you've done (lifecycle, packages, signatures, declarative runtime) **pays off here**—it ports to new hardware with minimal friction. This is exactly what platform abstraction is for.
