# Crystal OS — Implementation Plan (v1)

Companion document: `CODE_GUIDE.md`, which holds the skeletons referenced by each
phase below.

## 1. Target

| Item | v1 choice |
| --- | --- |
| Board | Waveshare `esp32_s3_touch_lcd_4b` |
| SoC | ESP32-S3, dual core, 240MHz, octal PSRAM @80MHz |
| Panel | RGB parallel, 480x480, ~86mm square (**not** MIPI DSI) |
| Touch | GT911 via I2C (`CONFIG_BSP_I2C_NUM=1`) |
| RTC | PCF85063 on the same I2C bus |
| Flash | 16MB |
| UI base | `esp-brookesia` 0.4.2 on LVGL 8.4 |

Crystal OS is a shell and app framework layered on `ESP_Brookesia_Phone`. It does
not reimplement the status bar, launcher, gesture recognition, per-app visual
area, or app lifecycle core — those are Brookesia's, and are extended rather
than replaced.

**Attribution requirement:** a `NOTICE` file at the repo root and an attribution
row in System settings crediting `esp-brookesia` and ESP-IDF to Espressif.
Brookesia's license text has not been read yet (`managed_components/` was never
populated in the reference); confirm it after the first successful build and make
`NOTICE` match what it actually says.

## 2. Settled decisions

These are closed. They are restated here so no phase re-opens them.

- **Destroy-on-switch, not resident apps.** Brookesia's `close()`/`run()` already
  destroy and rebuild an app's UI. Continuity comes from a snapshot plus a small
  state store, not from keeping object trees alive.
- **Single gesture owner.** One arbiter, claimed on direction-lock, OS priority
  over apps, quick-settings armed only from the top ~20px band.
- **LVGL is single-threaded.** Core 1 owns every `lv_*` call. Everything else
  posts to a queue.
- **All apps ship in firmware.** An NVS registry decides which are installed;
  app code lives in the OTA slot, app assets live in SPIFFS.
- **RTC first, SNTP second.** Time is correct before the first frame.
- **Dual OTA slots from day one**, even before OTA is wired up.
- **English only.** Three Montserrat sizes plus one for the indicator bar.

## 3. Measurement gates

Four unknowns remain. None change the architecture; each fills a blank. Two are
sequenced as gates because later work depends on the answer.

| # | Question | Answered by | Blocks |
| --- | --- | --- | --- |
| G1 | Full-screen animation FPS with `BSP_LCD_RGB_BUFFER_NUMS=1` | Phase 1 spike | Phase 6 (card switcher) |
| G2 | Does Brookesia compile against LVGL's SDL port? | Phase 2 | How much of Phase 5-10 can be built off-device |
| G3 | Actual panel size — `bsp_extra` describes itself as "AMOLED-1.8" but depends on `esp32_s3_touch_lcd_4b`, and the active stylesheet is `480_480` | Inspect hardware | Stylesheet choice, asset dimensions |
| G4 | Is there a usable battery sense pin? | Board schematic | Whether the battery icon ships at all |

G1 is the important one. If a full-screen drag with a blurred backdrop lands
under ~20 FPS, the choices are: enable double buffering (+460KB PSRAM and more
bandwidth on an already-loaded bus), simplify the effect, or move the animation
work to a later P4 target. Do not build Phase 6 before knowing.

## 4. Phases

Each phase lists its exit criteria. A phase is not done until they hold on real
hardware.

### Phase 0 — Skeleton and boot baseline (complete)

Create the project, get a blank Brookesia phone booting with one trivial app.

- Root `CMakeLists.txt`, `main/`, `components/`, `idf_component.yml` pinning
  `idf >=5.3.0`, `espressif/esp-brookesia 0.4.2`, `lvgl/lvgl 8.4.0`,
  `espressif/esp_lvgl_port ^2`, `waveshare/esp32_s3_touch_lcd_4b`
- `partitions.csv` per §5
- `sdkconfig.defaults` per §6
- Port `my_rounder_cb` verbatim from the reference — the panel requires
  even-aligned flush areas
- Register `bsp_display_lock` / `bsp_display_unlock` as Brookesia's LV lock
  callbacks

Exit: boots, one app launches and returns, `NOTICE` file present, first-frame
time logged as the baseline for the 1.2-2s budget.

**Completion record (2026-09-02):** Verified on the Waveshare ESP32-S3-Touch-LCD-4B.
The project builds and flashes successfully; the Brookesia launcher, GT911 touch,
Hello app, and Return to launcher path all work on hardware. The measured
first-frame baseline is **1933 ms**, within the Phase 0 target budget. The
launcher tile's visual affordance and full-tile hit area remain UI polish for a
later phase and do not block this bring-up milestone.

### Phase 1 — Performance gate (G1) (complete)

A throwaway spike, not production code. Full-screen card drag over a pre-blurred
static backdrop, `CONFIG_LV_USE_PERF_MONITOR=y` (already on in the reference).

Exit: a recorded FPS number and a written decision on double buffering.

**Spike preparation (2026-09-02):** Added the temporary `Phase 1 Perf` launcher
app. It renders a dense static 8x8 backdrop and a 300px live card that follows
touch 1:1, matching the workload described by the future switcher. LVGL's
performance monitor remains enabled and the app logs
`drag FPS avg=<n> (single-buffer gate)` once per second while the card is held.
Run `idf.py flash`, open `Phase 1 Perf`, and drag continuously for at least ten
seconds. Record the sustained FPS from the serial log, then decide: keep the
single RGB buffer at >=20 FPS; below 20 FPS, evaluate double buffering before
starting Phase 6.

**Hardware result (2026-09-02):** On the Waveshare ESP32-S3-Touch-LCD-4B with
`BSP_LCD_RGB_BUFFER_NUMS=1`, the visible card motion measured approximately
**7--8 FPS** during sustained dragging. The serial `lv_refr_get_fps_avg()` values
were 19--30 FPS, but that is the LVGL refresh average rather than the panel's
visible present rate; the on-screen result is the binding measurement for G1.
The single-buffer configuration therefore fails the 20 FPS gate. A second run
with `BSP_LCD_RGB_BUFFER_NUMS=2` still measured approximately **7--8 visible
FPS** (serial average decayed to 12 FPS), so double buffering does not solve the
panel-bound workload. **Decision: do not depend on double buffering for Phase 6.**
Use the planned fallback: a simplified switcher animation, with no live
full-screen tracking card, no blur/shadow during the drag, and a short
cross-fade or snap transition. Keep the benchmark app until that simpler
animation has its own measurement.

### Phase 2 — HAL boundary and simulator (G2)

Every hardware touch goes behind an interface: `IBrightness`, `IWifi`, `IRtc`,
`IStorage`, `ITouchRaw`. Device implementations wrap the BSP; desktop mocks back
an LVGL SDL build.

**Progress (2026-09-02):** Added the device brightness adapter and an NVS-backed
`IStorage` implementation and a lazy Wi-Fi station adapter in `crystal_hal`.
The Wi-Fi adapter was verified on hardware by connecting to a WPA2 network and
obtaining an IP address. The PCF85063 adapter now uses the board's shared I2C
bus at address `0x51`, and raw touch is exposed through the bound LVGL input
device. A standalone mock backend is available under `sim/` for host tests.
The full SDL UI target remains a follow-up.

Worth the effort because the hardest remaining work — card drag feel, pull-down
feel, keyboard field-centering — needs dozens of tuning passes. On hardware
that is a week; on desktop, an afternoon.

Caveats to accept up front: the simulator will not reproduce PSRAM limits, frame
rate, or single-buffer tearing. Those stay device-only questions. If Brookesia
resists porting (it calls into `esp_log` and `heap_caps` at minimum), simulate
only Crystal's own layers and leave Brookesia on-device.

Exit: the same UI code builds and runs on macOS and on the board.

### Phase 3 — Core services

**Progress (2026-09-03):** Added `crystal_core`, including a bounded
cross-task UI event queue, an LVGL-timer drain point, and a low-priority service
task pinned to core 0. A non-interactive toast overlay now handles queued toast
events, with a one-time core-0 startup toast serving as the hardware proof.
System time is loaded from the PCF85063 before display startup, the Brookesia
clock is refreshed every second, and SNTP corrections are written back to the
RTC. An unset or invalid RTC displays `--:--` until RTC or SNTP establishes a
valid time. `crystal_time_set()` provides the shared manual/system-to-RTC write path. The
service also implements a 30-second dim and 60-second backlight-off
policy with ramped brightness and touch activity restoration. Swallowing the
first wake touch remains assigned to the Phase 6 gesture arbiter.

- UI event queue: `crystal_ui_post()` from any task, drained by an `lv_timer` on
  the LVGL task so handlers already hold the lock
- `crystal_service` task, low priority, core 0: RTC reads, brightness ramp,
  screen-timeout counter
- Toast layer on `lv_layer_top()`, `LV_OBJ_FLAG_CLICKABLE` cleared so it can
  never steal a gesture. Built before WiFi because WiFi outcomes are toasts.
- Time service: PCF85063 → `settimeofday()` at boot, SNTP correction later,
  corrected time written back to the RTC, POSIX TZ string in NVS

Exit: time is right before the first frame with no network; a toast fires from a
core-0 task without touching LVGL directly.

### Phase 4 — App framework

**Progress (2026-09-03):** Added `CrystalApp`, a Crystal-owned lifecycle
wrapper over `ESP_Brookesia_PhoneApp`, with sealed `run()`/`pause()`/`resume()`/
`close()`/`back()` entry points and `onCreate()`/`onPause()`/`onResume()`/
`onDestroy()`/`onBack()` hooks, including an 80 ms `onResume()` diagnostic.
Added `CrystalState`, a bounded (2 KiB per
value) NVS-backed store with app-specific key prefixes. Hello now uses the
new lifecycle, and the State Test app demonstrates a counter surviving an app
switch and reboot. The Phase 4 hardware checkpoint passed; the framework is
ready for Phase 5 registry work.

`CrystalApp` over `ESP_Brookesia_PhoneApp`, with `run()`/`close()` sealed
`final` and redirected to the lifecycle hooks. `CrystalState` as a per-app
namespaced NVS-backed store, hard-capped ~2KB.

Namespacing matters beyond tidiness: it is the boundary that makes a future
script sandbox possible. An app that can read arbitrary NVS can read WiFi
credentials.

Exit: two apps convert to the lifecycle and survive switch-away/switch-back with
scroll position and draft text intact. `onResume()` under 80ms, warned in debug
builds when exceeded.

### Phase 4.5 — Lifecycle correctness

A review of the Phase 4 hooks against Android's six-callback model
(`onCreate`/`onStart`/`onResume`/`onPause`/`onStop`/`onDestroy`) found four gaps.
Android's callbacks are three nested pairs — create/destroy bounds the object
lifetime, start/stop the visible lifetime, resume/pause the foreground lifetime.
Crystal's five hooks do not nest, and two of them fire on events other than the
ones their names imply. This phase closes the gaps that matter before any real
app is built on the framework. It is small and entirely inside
`components/crystal_app` plus one stylesheet value. The install hooks,
pause-before-destroy ordering, advisory teardown results, and one-resident-app
policy land here. `onStart()`/`onStop()` are defined as dormant API hooks; their
dispatch belongs to Phase 7 when the gesture arbiter owns occluding overlays.

Keep the five active app hooks. Importing all six Android callbacks adds ceremony a 480px
single-window device does not need — there is no multi-window mode, so
"visible but not foreground" only ever means "occluded by an OS overlay".

**1. `onPause()` before `onDestroy()`.** The shipped `close()` calls
`onDestroy()` alone, so the documented contract "serialize state out in
`onPause()`" silently loses data on the most common path — return to launcher,
and destroy-on-switch. Android guarantees `onPause` precedes `onDestroy`; Crystal
must too. Guard with a lifecycle state enum so an app Brookesia already paused
does not receive `onPause()` twice.

Brookesia's `processClose()` calls `close()` *before* `enableAutoClean()` and
`cleanResource()`, so the LVGL tree is still alive inside `onPause()`/
`onDestroy()`. Widget state can be read there. That is a real difference from
Android, where the view hierarchy is gone by `onDestroy`, and it is what makes
this fix a two-line change rather than a redesign. Document it — an app author
will otherwise assume the Android rule and cache values defensively.

**2. Seal `init()`/`deinit()` as `onInstall()`/`onUninstall()`.** `onCreate()`
fires on every launch, so there is currently no once-ever hook, and
`ESP_Brookesia_CoreApp::init()`/`deinit()` sit unsealed and unused — an app can
override them today and bypass the framework. Phase 5's registry and Phase 13's
clear-data both need an install-time hook, and Clock's service-owned timer needs
somewhere to register that does not depend on the app being open. Note that
`CODE_GUIDE.md` already described `onCreate()` as mapping to `init()` "once, at
install"; the code never did that. This phase makes one of the two true.

**3. `onStart()`/`onStop()` for occlusion.** Quick settings (Phase 8) and the
keyboard overlay (Phase 10) cover the app opaquely with no callback at all, so a
1s `lv_timer` keeps redrawing behind a fully occluding panel. On a single-buffer
RGB panel that already measured 7-8 FPS at gate G1, that is wasted bandwidth in
exactly the moment an animation needs it. `onStop()` is the hook Clock, Weather,
and the Phase 1 benchmark all want. Fired by the Phase 7 arbiter, which is the
only component that knows what is covering what — so the hooks land here and the
call sites land in Phase 7. Until then they are defined and never fired.

**4. Decide `max_running_num`.** §2 calls destroy-on-switch settled and
`DESIGN.md` §5 justifies the snapshot design by stating resident apps are not
paid for, but the active `ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET()` ships
`max_running_num = 3` with `enable_app_save_snapshot = 1`. Apps 1-3 therefore
stay resident and paused; nothing is destroyed until a fourth launches. An app
cannot predict whether it gets `onCreate()` on a fresh tree or `onResume()` on a
live one, which is Android's "configuration change versus process death" trap and
fails on the fourth app rather than the first.

**Decision: override to `max_running_num = 1`.** The memory argument in
`DESIGN.md` §5 holds, Phase 6's 50% crossover already assumes a rebuild behind
the card, and one resident app makes the lifecycle deterministic — every launch
is `onCreate()`, every switch away is `onPause()` then `onDestroy()`. Keep
`enable_app_save_snapshot = 1`; Phase 6 needs the snapshot. Set it in the
stylesheet override in `main.cpp` rather than editing `managed_components/`.

**5. Ignore `onPause()`'s return value.** Brookesia treats `false` from `pause()`
as a failure and force-closes the app (`core_manager.cpp:279`). Android's
callbacks are `void`. An app that returns `false` to signal "state did not save"
currently gets killed for it, which is the opposite of useful. `CrystalApp::pause()`
logs a warning and returns `true` unconditionally. The same argument applies to
`onDestroy()` — there is nothing productive Brookesia can do with a failed
teardown.

A `LifecycleState` enum member (`INSTALLED`/`CREATED`/`STARTED`/`RESUMED`/
`PAUSED`/`DESTROYED`) backs items 1 and 5, asserts legal transitions in debug
builds, and covers Brookesia's error paths, which call `processClose()` from
inside a failed `pause()` and would otherwise re-enter the hooks.

**Progress (2026-09-03):** Implemented the lifecycle state guard, ordered
pause/destroy teardown, install/uninstall hooks, advisory teardown returns,
80 ms `onCreate()` timing, and `max_running_num = 1`. State Test now writes its
counter only in `onPause()`. `onStart()`/`onStop()` remain dormant until Phase 7.

Exit: an app whose only state write is in `onPause()` survives return-to-launcher
and reopen. Opening a fourth app produces the same `onCreate()` path as the
first. `onInstall()` fires once per boot regardless of how many times the app is
opened. Illegal transitions assert in debug builds. `CODE_GUIDE.md` §"Phase 4 —
CrystalApp" matches the shipped code.

### Phase 5 — Registry and launcher

NVS registry (conceptually `app.<name>.enabled` and `app.<name>.slot`). The
device backend hashes the stable app ID into keys such as `r1234abcd.e` and
`r1234abcd.s`, because ESP-IDF NVS keys are limited to 15 characters. `main.cpp`
walks the compiled-in table and installs only enabled apps, in slot order.

**Progress (2026-09-03):** Added `crystal_registry`, a compiled app table in
`main.cpp`, persistent enabled/slot accessors, stable slot sorting, and boot logs
for installed and disabled apps. State Test provides temporary Phase 5 hardware
controls to toggle Hello and swap the two launcher slots for the next boot.
Hardware validation confirmed that disabling and restoring Hello persists across
reboots and that saved slot changes reorder the launcher. Phase 5 is complete;
immediate launcher mutation remains Phase 13 UI work.

Exit: disabling an app removes it from the launcher across a reboot; reordering
persists.

### Phase 5.5 — Clock app

**Progress (2026-09-03):** Added the Clock app with Clock, Timer, and
Stopwatch tabs. Timer countdown ownership lives in `crystal_core`, using an
absolute end instant with pause/resume/reset controls, expiry toast, and a
status indicator. Stopwatch state and up to 50 laps persist through the app
lifecycle. The app is registered at launcher slot 2. Timer expiry plays a short
three-tone chime through the board's ES8311 speaker and shows a visual toast.

The first real app, and deliberately placed here: it is the strongest available
test of the Phase 4 lifecycle, because a running timer must survive the app being
destroyed.

Three tabs (Clock, Timer, Stopwatch) per `DESIGN.md` §9.5. Timer presets 30s
through 30m plus custom; stopwatch with laps.

The architectural work is not the UI, it is the ownership split:

- Absolute end time (`time_t`) in `CrystalState`, never a remaining-seconds
  counter — so it stays correct across destruction *and* across an SNTP
  correction that moves the clock.
- Expiry owned by `crystal_service`, not the app. Fires the toast and chime
  wherever the user is.
- Indicator-bar glyph while something is counting. The one addition to the bar in
  v1, justified because an invisible running timer reads as a bug.

Needs no network, so it can be built before Phase 9.

Exit: start a 3-minute timer, switch to another app, switch back — the ring is
mid-flight and correct. Start a timer, leave Clock entirely, and the expiry toast
still fires. Reboot mid-timer and the behaviour is defined (v1: cleared, and the
user is not lied to about it).

### Phase 6 — App switcher

Gated on G1. 1/8-downscale snapshot (480x480 RGB565 is 460KB; a 60x60 buffer is
~7KB), box blur on the small buffer, LVGL upscales on draw — visually
equivalent at this size and roughly 60x cheaper.

Card overlay follows the drag; past 50% the incoming app renders and receives
touch. Left edge dragged right = previous, right edge dragged left = next.

Exit: switching feels continuous, steady-state PSRAM does not grow with the
number of apps ever opened.

### Phase 7 — Gesture arbiter and indicator bar

One `gesture_owner` (`NONE`/`APP_SWITCH`/`QUICK_SETTINGS`/`APP`), claimed on
direction-lock after ~12px, released on touch-up. While the OS owns a gesture,
`lv_indev` events do not reach the app.

Quick-settings arms only from the top ~20px band. Without that, a swipe-down in
a scrolled app view opens settings when the user meant to scroll up — the one
case where OS priority feels broken.

Indicator bar: logo, time (no date), WiFi state, battery (G4). Icon colour black
or white chosen from the foreground app's background; bar background inherits
it.

Exit: no gesture reaches an app while the OS owns it; scrolled app views still
scroll up.

### Phase 8 — Quick settings

Pull-down over a snapshot of the foreground app. Brightness and volume as
filled bars wired to `bsp_extra`. **Clamp brightness to
`BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX` = 95, not 100**, or the top of the slider
does nothing. Gear opens the settings app. Bluetooth button reserved, visibly
disabled. Power-saving toggle per Phase 11.

Exit: released past half-open completes the animation; releasing short of it
returns; app switching is locked while the panel is active.

### Phase 9 — WiFi

Non-blocking, off the boot path. `esp_wifi_start()` plus auto-connect from NVS
returns immediately; the bar shows disconnected until `IP_EVENT_STA_GOT_IP`
arrives on the UI queue.

Long-press the WiFi button expands SSIDs sorted by RSSI, connected one pinned
first. Pick → credential dialog → on success dismiss the whole stack to the app
and toast; on failure, a dialog. Enable `CONFIG_NVS_ENCRYPTION` with the
`nvs_keys` partition — credentials are otherwise plaintext.

Exit: first frame is not delayed by WiFi; credentials survive reboot; WPA3 works
(`ESP_WIFI_ENABLE_WPA3_SAE` is already on).

### Phase 9.5 — Weather app

First app with a network dependency, so it lands immediately after WiFi.

**Open-Meteo, not weather.com.** Not a cost decision: any API key compiled into
firmware is extractable with `esptool read_flash`, so a keyed provider means
either treating the key as public or running a proxy. Open-Meteo needs no key,
returns compact JSON, and its WMO codes map to a small icon set.

```
GET https://api.open-meteo.com/v1/forecast?latitude=..&longitude=..
      &current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m
```

- Lat/long entered in Settings › General — no GPS on this board.
- Fetch runs on `crystal_service`, never the LVGL task; results via the UI queue.
- Cache the last reading and its timestamp in `CrystalState`, and surface age as
  "Updated N min ago" so staleness is visible rather than hidden.
- Refresh on open if older than 15 minutes, plus every 30 minutes while WiFi is
  up. Not on a tight timer.
- TLS costs ~30-45KB heap per handshake plus the cert bundle in flash. Budget it.

**Check the schematic first.** If the board has an I2C temp/humidity part, indoor
readings from the sensor alongside outdoor from the API is a better product than
either alone. Unverified here.

Exit: correct conditions shown with WiFi up; cached reading with a visible age
when offline; no LVGL call from the fetch path.

### Phase 9.6 — Calculator port

Borrow `components/apps/calculator` from the reference (439 lines, self-contained)
and port it to `CrystalApp`. Third lifecycle conversion test, no network.

Take the class, not the assets: `img_app_calculator.c` is 906,288 bytes for an
image whose PNG is 2,951 bytes. Convert to an LVGL binary image in SPIFFS.

Exit: arithmetic correct, state survives a switch away and back.

### Phase 10 — Keyboard overlay

App viewport bottom binds to the keyboard top. If the focused field is already
visible, leave it; if the keyboard would cover it, animate it to the midpoint
between keyboard top and indicator bar.

Exit: a field near the bottom of a scrolling form stays visible and does not
jump when already visible.

### Phase 11 — Settings and power

Categories: Network (WiFi, DHCP vs static, IP/gateway/netmask/DNS), System
(hardware version, OS version, company info, IP, attribution), Power (screen dim
timeout, screen off timeout, dim level, power saving), General (timezone).

Timezone is not optional — without it SNTP yields UTC and the bar shows the
wrong hour. The first-boot default is Hong Kong (`HKT-8`, UTC+08:00); Phase 11
will expose this as a General setting stored as a POSIX timezone string.

`CONFIG_PM_ENABLE=y` with DFS 240/80MHz. **Do not enable automatic light
sleep in v1**: the RGB panel is a continuous DMA scan-out and will blank or
tear. Power saving is one NVS flag with several effects — CPU capped at 80MHz,
`WIFI_PS_MAX_MODEM`, lower brightness ceiling, shorter timeouts.

Exit: static IP survives reboot; power saving measurably lowers current draw.

### Phase 12 — Reliability

- `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` + ELF format + CRC32. Check
  `esp_core_dump_image_get()` at boot, log it, surface a quiet "recovered from an
  error", then erase. Log `esp_reset_reason()` to separate panics from brownouts
  and watchdog resets.
- **Archive `build/crystal_os.elf` for every image given to anyone.** Coredumps
  decode only against the exact ELF that produced them; without this habit they
  are unreadable hex.
- OTA over WiFi, plus the USB wrapper as the recovery transport. Same image, two
  paths.

Watch `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` (panic disabled): a slow `onResume()`
holding the LVGL lock is the likeliest way to trip it.

Exit: a deliberate crash produces a symbolised backtrace; a bad OTA rolls back.

### Phase 13 — App catalog

The user-facing face of Phase 5: browse all bundled apps, install (enable),
uninstall (disable, optionally wiping that app's state namespace), reorder.

Exit: a non-developer can install, remove, reorder, and clear app data without a
firmware change.

## 5. Partition table

16MB flash. App code in the OTA slots, app assets in SPIFFS.

```
# Name,     Type, SubType,  Offset, Size
nvs,        data, nvs,      ,       0x6000
nvs_keys,   data, nvs_keys, ,       0x1000
otadata,    data, ota,      ,       0x2000
phy_init,   data, phy,      ,       0x1000
coredump,   data, coredump, ,       0x10000
ota_0,      app,  ota_0,    ,       5M
ota_1,      app,  ota_1,    ,       5M
storage,    data, spiffs,   ,       4M
```

Two things this encodes. Dual slots exist from Phase 0 even though OTA lands in
Phase 12 — repartitioning after devices ship means a serial reflash of every
unit. And 5M slots only stay comfortable if assets stay out of the binary: the
reference needed 9M almost entirely because of ~48MB of compiled-in image C
arrays in the music demo (`img_lv_demo_music_cover_*_large.c` alone is 9.4MB
each). Crystal core should land near 2.5-3MB, leaving room for 15-25 apps of
logic.

Discard from the reference: all of `music_player/gui_music`, and the 3MB of demo
MP3s in `spiffs/`.

## 6. sdkconfig deltas

Start from the reference `sdkconfig.defaults`, then:

```
CONFIG_PM_ENABLE=y                        # absent in reference
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y     # reference is _TO_NONE
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
CONFIG_ESP_COREDUMP_CHECKSUM_CRC32=y
CONFIG_NVS_ENCRYPTION=y
CONFIG_BSP_DISPLAY_LVGL_TASK_STACK_SIZE_KB=10   # was 6
```

Keep as-is: `SPIRAM_MODE_OCT`, `SPIRAM_SPEED_80M`, `SPIRAM_FETCH_INSTRUCTIONS`,
`SPIRAM_RODATA`, `COMPILER_OPTIMIZATION_PERF`, `FREERTOS_HZ=1000`,
`ESP_BROOKESIA_MEMORY_USE_CUSTOM` with the PSRAM allocator override in
`main/CMakeLists.txt`, `LV_COLOR_DEPTH=16`, `LV_COLOR_16_SWAP=n`,
`ESP_WIFI_TASK_PINNED_TO_CORE_0`.

Trim: the ~20 compiled Montserrat sizes down to four (16 small, 20 medium, 28
large, plus the indicator bar size). Each unused size is dead flash. Drop
`LV_USE_DEMO_BENCHMARK` after Phase 1.

## 7. Sequencing notes

Phases 0-3 are prerequisites for everything. 4-5 unlock app work. 4.5 should land
before 5.5, since Clock is the first app to depend on `onPause()` actually being
called and on an install-time hook. 6 waits on G1. 7 should precede 8 and 10,
since both depend on the arbiter existing, and it is where Phase 4.5's
`onStart()`/`onStop()` get their call sites. 12 can
start any time after 0 and should not be left to the end — coredumps are most
valuable while the system is least stable.
