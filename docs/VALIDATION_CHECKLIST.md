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

- [x] Two RGB framebuffers with avoid-tear direct mode remain selected.
- [x] The benchmark app can be restored or built when a performance regression
  needs investigation.
- [x] The corrected display path completes 8-10 refreshes/sec under sustained
  benchmark dragging.
- [x] Phase 6 continues to avoid a live full-screen tracking card, dynamic blur,
  and shadow during drag.
- [x] The switcher uses a short snapshot fade and has passed physical-panel
  measurement without obvious tearing.

Completion gate: the measured result, corrected display mode, and simplified
animation decision are recorded in `IMPLEMENTATION_PLAN.md`.

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

Brightness stability follow-up (2026-09-05): direct 0%, 1%, 3%, and 5%
backlight levels were stable during the 30-second hardware observations. The
underlying energy-saver low-level brightness behavior remains unresolved and is
deferred. The temporary boot brightness diagnostic has been removed; production
behavior continues to use 0% for the `Off` state, with no wake-path experiment
retained.

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
- [x] The serial log shows `onPause` then `onDestroy`, in that order, on every
  return to the launcher.
- [x] `onPause` appears exactly once per close — not twice.
- [x] Open Hello, then State Test, then Hello, then State Test: every launch logs
  `onCreate`, never `onResume`. Confirms `max_running_num = 1`.
- [ ] Steady-state PSRAM after ten alternating launches matches the free heap
  after the first, within noise. Nothing is accumulating.
- [x] Clock's boot reconciliation runs once on its first `onCreate`, and does
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

## Phase 6: card shell

- [x] Firmware builds with the `crystal_shell` component.
- [x] Boot opens the saved card by stable app ID, falling back to slot 0.
- [x] Reboot restores the last-viewed card.
- [x] Left/right card order follows launcher slots and does not wrap.
- [x] A card switch performs `onPause()` then `onDestroy()` before the next
  card's `onCreate()`.
- [x] Hardware log confirms `Hello -> State Test -> Clock -> State Test -> Hello`.
- [x] Destination snapshots are cropped to 90% about the centre **of the app area**
  and downsampled to half the app resolution per axis (240x220 on this panel, so
  the app's aspect ratio is preserved and one zoom factor serves both axes),
  then magnified to cover the app area and centred inside a container clipped to
  that area, so no rounding overshoot can paint over the status bar.
- [x] A swipe switches with no live drag tracking: the outgoing app does not
  follow the finger, and the transition is the snapshot fade after the switch
  completes.
- [x] Physical-panel measurement confirms the simplified app-area-sized
  snapshot fade transition.

These two items describe an **interim fallback**, not a design decision. The 50%
visual crossover is required by `DESIGN.md` §5; Phase 6.5 corrected the unintended
display mode, re-ran the gate, and landed below the continuation threshold, so
snap-and-fade stays only until the render cost is fixed. See
`PHASE_6_5_CROSSOVER.md` for the measurements and Phase 7.5 below for the items
that retire these two. Nothing in this checklist authorises keeping snap-and-fade
as the final interaction.

### Phase 6.5 re-gate

- [x] Single-buffer instrumentation recorded render and flush time separately.
  **Not reproducible from the tree:** the instrumentation was not committed. Nothing
  in `components/` or `main/` splits render from flush today — `perf_spike` only logs
  `lv_refr_get_fps_avg()`. Re-add the timing hooks before trusting or re-running the
  49-96 ms / 15-34 ms figures, and commit them this time.
- [x] Generated `sdkconfig` retains two RGB buffers, avoid-tear, and direct mode.
- [x] Direct mode reduces synchronous flush from 15-34 ms to 3-10 ms.
- [x] Sustained drag completes about 8-10 refresh cycles/sec with render time at
  91-106 ms, below the 12 FPS continuation gate.
- [x] Physical-panel testing confirms no more obvious snapshot tearing.
- [x] The 50% crossover is deferred to Phase 7.5 with a measured reason, and the
  snap/fade baseline is recorded as interim rather than final.

Completion gate for the current shell increment: direct card boot, deterministic
switching, and the app-area-sized snapshot transition pass on hardware. Full
gesture arbitration remains Phase 7.

## Phase 7: gesture arbiter and indicator bar

- [x] The firmware contains one explicit `NONE`/`APP_SWITCH`/`QUICK_SETTINGS`/`APP`
  owner, claimed at the 12px direction lock and reset only on release.
- [x] Brookesia's eager edge mask is disabled; Crystal raises the full-screen input
  mask only for an OS-owned gesture.
- [x] Left/right edge bands are 24px and the quick-settings arm band is 20px.
- [x] Quick-settings, keyboard, Settings, and modal lock APIs are present for their
  owning phases to drive.
- [x] The off-screen wake touch is masked until release.
- [x] The status path updates Wi-Fi, AXP2101 percentage/charging at no less than a
  30-second interval, and fixed-order page dots.
- [x] The legacy once-per-minute RTC diagnostic poll is removed.
- [x] ESP-IDF 6.1 compilation succeeds.
- [x] With RTC unset and Wi-Fi disconnected, start a countdown, switch away and
  back, and confirm the monotonic fallback keeps counting and expires normally.
- [x] On hardware, dragging horizontally in the middle of an app still reaches the
  app, while an outward edge drag does not leak a click or scroll event.
- [x] On hardware, a vertical drag in a scrolled app remains app-owned unless it
  starts in the top 20px band.
- [x] On hardware, first/last-card boundary swipes are swallowed without wrapping.
- [x] On hardware, the first touch after display-off wakes the panel but activates
  no app control.
- [x] With Wi-Fi disconnected and time unset, the status bar renders the
  disconnected state, battery icon/percentage, page dots, and `--:--` clock.
- [x] Page-dot selection follows card changes.
- [ ] With a battery connected, verify percentage accuracy and charging-state
  indication. No battery was available for the current hardware test.

Completion gate: passed on the available no-battery hardware. Battery percentage
accuracy and charging indication remain a deferred hardware follow-up.

## Phase 7.5: 50% visual crossover

- [x] The incoming preview follows the finger while the outgoing app is stationary.
- [x] The incoming card is created at direction lock, without a blank or blocked
  frame during the drag.
- [x] A first visit shows the destination identity card without constructing the
  target app during the drag.
- [x] Visited-app previews use a downscaled app-area image during motion and are
  enlarged to the card area without covering the status bar.
- [x] Crossing 10% has no lifecycle effect; release at or beyond 50% commits the
  destination live app behind the preview.
- [x] Releasing before 50% cancels without changing the active app.
- [x] Releasing after 50% completes the switch and destroys the outgoing app.
- [x] Touch transfers only after the destination is live; no event leaks across
  gesture owners.
- [x] App-area clipping keeps the preview below the status bar at every offset.
- [x] The physical panel shows no obvious tearing throughout drag and settle.

Persistent preview files are implemented under `/spiffs` and keyed by stable app
ID; persistence across reboot is validated for Hello, Clock, and State Test.

Persistence finding: SPIFFS allows a maximum 32-character object name. The
original State Test temporary filename exceeded that limit and caused
`result=open-failed`; short `/spiffs/.tmp_<stable-app-id>` names now avoid the
limit. The final preview path remains unchanged.

## Post-cleanup checklist (2026-09-05)

### Phase 7

- [x] Temporary brightness diagnostic code is removed from production startup.
- [x] Production `Off` brightness target remains 0%; no 5% floor was added.
- [x] The experimental wake-touch inactivity reset was reverted.
- [x] Existing gesture ownership, status-bar, timer, and wake-touch behavior is
  unchanged by the cleanup.
- [x] No stale diagnostic symbols or log messages remain.

### Phase 7.5

- [x] Preview/lifecycle state machine remains unchanged by the cleanup.
- [x] Dragging remains preview-only; no lifecycle callbacks occur during drag.
- [x] Release below 50% cancels; release at or above 50% commits after cover.
- [x] Persistent previews remain keyed by stable app ID under `/spiffs`.
- [x] No stale preview path references remain in the implementation documents.
- [x] Re-test card transitions and persistent preview loading after an app-only
  production flash.

Phase 7.5 completion: all visual, lifecycle, persistence, and no-tearing checks
passed on hardware; phase closed.

### Deferred

- [ ] Determine the root cause of any low-brightness energy-saver fluctuation if
  it reappears; do not change the production brightness floor without evidence.

## Regression command sequence

```bash
source /Users/szemy/.espressif/v6.1/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor -b 2000000
```

Use `idf.py fullclean` only after configuration or dependency changes, or when
investigating stale build output.
