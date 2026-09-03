# Crystal OS Validation Checklist

Use this checklist after a clean build, after changing hardware services, and
before marking a phase complete.

## Build and flash

- [ ] `idf.py build` completes without errors.
- [ ] Firmware size fits the smallest OTA application partition.
- [ ] Flash write and hash verification complete successfully.
- [ ] The monitor uses the correct USB port and `2000000` baud.
- [ ] The application starts without a panic, watchdog reset, or reboot loop.
- [ ] The PSRAM memory test passes.

The existing `Incorrect size of core dump image` message is caused by stale or
uninitialized coredump partition contents. It is not a crash unless a panic or
reset follows it.

## Phase 0: bring-up

- [ ] The 480x480 panel initializes and displays the launcher.
- [ ] GT911 is detected and reports its ID and configuration version.
- [ ] Touch coordinates correspond to visible controls.
- [ ] The Hello app displays a readable `HELLO WORLD` icon.
- [ ] Tapping the Hello app opens it.
- [ ] `Return to launcher` returns to the launcher.
- [ ] First-frame time remains close to the recorded 1.9-second baseline.

Completion gate: basic display, touch, app launch, and app return all work on
physical hardware.

## Phase 1: performance gate

- [ ] `CONFIG_BSP_LCD_RGB_BUFFER_NUMS=1` remains selected.
- [ ] The benchmark app can be restored or built when a performance regression
  needs investigation.
- [ ] Visible drag performance remains consistent with the recorded 7-8 FPS.
- [ ] Phase 6 continues to avoid a live full-screen tracking card, dynamic blur,
  and shadow during drag.
- [ ] Future switcher transitions use a short cross-fade or snap and receive a
  new physical-panel measurement.

Completion gate: the measured result and single-buffer animation decision are
recorded in `IMPLEMENTATION_PLAN.md`.

## Phase 2: HAL

- [ ] `hal().brightness` changes and reports backlight brightness.
- [ ] Brightness is clamped to the supported maximum of 95 percent.
- [ ] `hal().storage` can write, read, and erase a test blob in NVS.
- [ ] `hal().wifi` starts in station mode.
- [ ] Saved Wi-Fi configuration reconnects without credentials in source code.
- [ ] Wi-Fi obtains an IP address.
- [ ] PCF85063 responds on the shared I2C bus at address `0x51`.
- [ ] A valid PCF85063 time can be read.
- [ ] Time can be written to PCF85063 and read back.
- [ ] `hal().touch_raw` returns coordinates and pressed/released state.
- [ ] The host mock compiles:

```bash
clang++ -std=c++17 \
  -Icomponents/crystal_hal/include \
  -fsyntax-only sim/crystal_hal_mock.cpp
```

- [ ] A full LVGL SDL window builds and runs on macOS.

Completion gate: the same Crystal-owned UI code runs with the device HAL and
desktop mock. Until the SDL item passes, Phase 2 remains technically open.

## Phase 3: core services

- [ ] `Core services ready` appears after boot and fades out.
- [ ] The toast does not intercept touch.
- [ ] The log contains `toast displayed: Core services ready`.
- [ ] An unset, invalid, or stopped RTC displays `--:--`.
- [ ] Saved valid RTC time appears before network synchronization.
- [ ] Wi-Fi/SNTP synchronization produces the `Time synchronized` toast.
- [ ] Synchronized time appears in the status bar.
- [ ] Displayed time uses Hong Kong time (`HKT-8`, UTC+08:00).
- [ ] After SNTP, reboot without Wi-Fi and confirm the RTC supplies time.
- [ ] Leave the device untouched for 30 seconds and confirm a gradual dim to 20
  percent.
- [ ] Continue to 60 seconds and confirm the backlight turns off.
- [ ] Touch the dark screen and confirm brightness returns to 95 percent.
- [ ] The launcher and Hello app remain responsive after waking.
- [ ] No queue, LVGL, I2C, watchdog, or task errors appear during the test.

Completion gate: offline RTC boot, queued toast, and dim/off/wake behavior all
pass on physical hardware. Wake-touch suppression remains a Phase 6 gesture
ownership requirement.

## Phase 4: app framework checkpoint

- [x] The launcher shows both `Hello` and `State Test` apps.
- [x] Hello opens and returns to the launcher using the Crystal lifecycle.
- [x] Open State Test, tap `Increment`, and confirm the saved counter changes.
- [x] Return to the launcher, reopen State Test, and confirm the counter remains.
- [x] Reboot, reopen State Test, and confirm the counter survives the reboot.
- [x] Switching apps produces no LVGL or NVS errors.
- [x] `onResume()` logs a warning when it exceeds the 80 ms budget.

Completion gate: both apps use `CrystalApp`; State Test state survives
switch-away/switch-back and reboot, and resume-time diagnostics are active.

## Phase 4.5: lifecycle correctness checkpoint

State Test keeps its counter in memory and writes it to NVS only from
`onPause()`, so the following sequence exercises the documented save path.

- [ ] Open State Test, tap `Increment` three times, return to the launcher,
  reopen: the counter reads 3. Proves `onPause()` ran on the close path.
- [ ] Increment again, return to the launcher, then reboot: the saved counter
  remains. A hard reset while the app is open does not invoke `onPause()`.
- [ ] The serial log shows `onPause` then `onDestroy`, in that order, on every
  return to the launcher.
- [ ] `onPause` appears exactly once per close — not twice.
- [ ] Open Hello, then State Test, then Hello, then State Test: every launch logs
  `onCreate`, never `onResume`. Confirms `max_running_num = 1`.
- [ ] Steady-state PSRAM after ten alternating launches matches the free heap
  after the first, within noise. Nothing is accumulating.
- [ ] Clock's boot reconciliation runs once on its first `onCreate`, and does
  not clear timer state on later launches in the same boot.
- [ ] An app whose `onPause()` deliberately returns `false` is **not** killed:
  the warning is logged and teardown continues normally.
- [ ] `onStart`/`onStop` compile and are overridable, and no path fires them yet.
- [ ] No LVGL, NVS, or watchdog errors across the whole sequence.

Completion gate: the documented save path (`onPause()`) is the one actually
demonstrated, every launch takes the same `onCreate()` route, and a failed
`onPause()` no longer destroys the app.

## Phase 5: registry and launcher checkpoint

- [x] Boot logs show `hello` and `state_test` installed in slots 0 and 1.
- [x] In State Test, choose `Disable Hello next boot`, then return to the
  launcher and reboot.
- [x] Hello is absent after reboot and the log reports `disabled: hello`.
- [x] Choose `Enable Hello next boot`, return to the launcher, and reboot.
- [x] Hello returns after reboot.
- [x] Choose `State Test first next boot`, return to the launcher, and reboot.
- [x] State Test precedes Hello in launcher order and the install logs show its
  lower saved slot.
- [x] Restore `Hello first next boot` and reboot to return to the default order.
- [x] No disabled app is constructed and no NVS or launcher errors occur.

Checkpoint gate: enabled state and launcher order both persist across reboot.
The final Manage Apps interface and immediate launcher updates remain Phase 13.

## Phase 5.5: Clock app

- [ ] Clock shows local time, date, and Hong Kong timezone.
- [ ] Timer presets select 30 seconds through 30 minutes and update the ring.
- [ ] Start, pause, resume, and reset update the timer state correctly.
- [ ] Start a 3-minute timer, leave Clock, reopen it, and confirm the remaining
  time continues from the saved absolute end instant.
- [ ] Leave Clock entirely and confirm expiry produces the `Timer finished` toast
  and the timer indicator clears.
- [ ] Confirm timer expiry also plays the short three-tone alert through the board
  speaker.
- [ ] Stopwatch start/pause/resume/reset work and laps are listed newest first.
- [ ] Stopwatch elapsed time and laps survive leaving and reopening Clock.
- [ ] Reboot clears a running timer, as defined for v1.

Checkpoint gate: timer expiry is service-owned and visible outside Clock; Clock
state survives app destruction without requiring a resident app.

## Regression command sequence

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor -b 2000000
```

Use `idf.py fullclean` only after configuration or dependency changes, or when
investigating stale build output.
