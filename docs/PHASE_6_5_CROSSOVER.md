# Phase 6.5 — Finger-tracked 50% crossover

## Status — closed (measurement and display-path correction)

Implementation guide and completion record. Steps 0 and 1 ran on hardware on
2026-09-04. Direct mode removed the obvious snapshot tearing, but sustained drag
completed only 8-10 refreshes per second with 91-106 ms render time, below the
12 FPS continuation gate. Steps 2-4 are therefore deferred to Phase 7.5 and the
existing snap transition stays as the working baseline.

Written for whoever picks this up next. Read the whole thing before editing code;
the ordering matters, and Steps 0-1 can invalidate Steps 2-4.

## 1. Why this is being revisited

`DESIGN.md` §5 specifies a 50% live crossover. Phase 6 deferred it and shipped a
snapshot-and-snap transition instead, on the strength of gate G1
(`IMPLEMENTATION_PLAN.md:113-127`): approximately 7-8 visible FPS during a
sustained full-screen drag.

That number was measured in a display mode nobody intended. `sdkconfig.defaults:33`
sets `CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y`, but it is **absent from the generated
`sdkconfig`** — Kconfig dropped it silently because of an unmet dependency chain:

```
BSP_DISPLAY_LVGL_DIRECT_MODE  depends on  BSP_DISPLAY_LVGL_AVOID_TEAR
BSP_DISPLAY_LVGL_AVOID_TEAR   depends on  BSP_LCD_RGB_BUFFER_NUMS > 1
                                          ← is 1, and AVOID_TEAR defaults to "n"
```

Verify for yourself before doing anything else:

```
grep -n "DIRECT_MODE\|AVOID_TEAR\|FULL_REFRESH" sdkconfig   # expect: nothing
```

So what runs is `esp32_s3_touch_lcd_4b.c:557` — a **480x100 partial draw buffer**
in internal RAM (`MALLOC_CAP_DEFAULT`, since both `buff_dma` and `buff_spiram` are
false at `esp32_s3_touch_lcd_4b.c:580-582`). `esp_lvgl_port_disp.c:544` then copies
each rendered band straight into the single live PSRAM scanout framebuffer.

Two consequences:

- **The visible banding is explained.** A 480x456 app area is 5 bands, each landing
  mid-scan with nothing to hide it. This is a buffering artifact, not image loading:
  the snapshot is already raw RGB565 in PSRAM and `downscale_crop` runs once, before
  the image is ever attached. Pre-caching removes work *upstream* of the banding.
- **The G1 retest at `BSP_LCD_RGB_BUFFER_NUMS=2` was a no-op.** Without
  `AVOID_TEAR=y`, `esp_lvgl_port_disp.c:294` never takes the
  `esp_lcd_rgb_panel_get_frame_buffer` branch. The second framebuffer was allocated
  and never used by LVGL. Same band-copy path, hence the same number.

## 2. What the frame budget does and does not explain

Be honest about this, because it decides how much confidence Step 1 deserves.

7-8 FPS is **125-143 ms per frame**. Accounted for:

| Cost | Estimate | Source |
| --- | --- | --- |
| Flush: 5 bands x 96 KB memcpy to PSRAM | ~10-25 ms | `esp_lvgl_port_disp.c:544` |
| Bounce-buffer refill, continuous | 27.6 MB/s, permanent | see below |
| LVGL render of the G1 spike workload | unmeasured | dense 8x8 backdrop + 300px card |

The bounce buffer is the find worth knowing about.
`BSP_LCD_DRAW_BUFF_SIZE = 480 x 20 = 9600 px` is passed as
`bounce_buffer_size_px` (`esp32_s3_touch_lcd_4b.c:460`), and
`esp_lcd_panel_rgb.c:1028-1042` implements the refill as a **CPU `memcpy` from the
PSRAM framebuffer inside an IRAM ISR**, fired on every bounce-buffer completion.
At 60 Hz that is ~460 KB/frame, ~27.6 MB/s of CPU-driven copy, running whether or
not anything is animating. On top of that, `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` and
`CONFIG_SPIRAM_RODATA=y` put code and constants in PSRAM too, competing for the
same bus.

**Roughly 100 ms of that frame time is unattributed.** It cannot be pinned down by
reading source. Candidates: PSRAM bandwidth saturation, cache thrashing from
PSRAM instruction fetch, or the spike's own overdraw. Do not assume the config
change reclaims it.

### The config fix may cost FPS, not gain it

`AVOID_TEAR` makes LVGL's draw buffers *be* the two panel framebuffers
(`esp_lvgl_port_disp.c:294-300`). Flush becomes a VSYNC swap — no copy, and no
partial band can reach the screen. That fixes tearing outright.

But it also moves the render target **from internal RAM into PSRAM**, and software
rendering into PSRAM is materially slower per pixel. And if you pick
`FULL_REFRESH`, `lv_refr.c:235-240` promotes *any* invalidation to a whole-screen
redraw — so the status-bar clock ticking once a second repaints 480x480.

So: tearing is fixed, FPS is genuinely uncertain and could get worse. This is why
Step 0 measures before Step 1 changes anything.

**Prefer `DIRECT_MODE` over `FULL_REFRESH`.** `lv_refr.c:636-650` shows direct mode
draws at absolute coordinates but clips to the invalid area, where full refresh
always redraws everything. For a full-screen drag the two are equivalent; for every
other repaint on this device direct mode is strictly cheaper.

## 3. Step 0 — Instrument first (do this before touching sdkconfig)

Cheap, reversible, and it turns Step 1 from a guess into a decision.

In the Phase 1 perf app, time the two halves of a frame separately and log them
once per second alongside the existing FPS line:

- **Render time:** wrap the `lv_timer_handler()` call, or hook
  `LV_EVENT_REFR_START` / `LV_EVENT_REFR_FINISH` on the screen.
- **Flush time:** accumulate `esp_timer_get_time()` deltas around the
  `flush_cb`. You can reach it via `display->driver->flush_cb` — wrap the existing
  pointer rather than replacing it.

Report three numbers: `render_ms`, `flush_ms`, `visible_fps`.

What the split tells you:

| Result | Meaning | Then |
| --- | --- | --- |
| `flush_ms` dominates (>60% of frame) | band-copy really is the wall | Step 1 should help; proceed |
| `render_ms` dominates | LVGL drawing is the wall | Step 1 likely makes it worse (render moves to PSRAM). Reduce per-frame draw work first — see §8 |
| neither accounts for the frame | the missing ~100 ms is bus/cache contention | Step 1 changes little. Stop and report; the crossover is probably not affordable |

**Do not skip this.** Without it, a bad Step 1 result is ambiguous — you won't know
whether the config was wrong or the workload is simply too heavy.

## 4. Step 1 — Fix the display config, re-measure

In `sdkconfig.defaults`, replace the ineffective `DIRECT_MODE` line with the full
set that actually satisfies the dependency chain:

```
CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2
CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y
```

Then `idf.py fullclean && idf.py build`, and **verify the options survived**:

```
grep -n "AVOID_TEAR\|DIRECT_MODE\|RGB_BUFFER_NUMS" sdkconfig
```

If `AVOID_TEAR` is still absent, the dependency is still unmet — fix that before
reflashing, or you will repeat G1's mistake exactly.

Flash and check two things, in order:

1. **Banding gone?** Swipe between cards. The snapshot should appear whole. This is
   the primary goal and it should succeed regardless of the FPS outcome.
2. **Drag FPS?** Re-run the Phase 1 perf app with the Step 0 instrumentation.

Gate on the visible FPS:

| Measured | Decision |
| --- | --- |
| >= 20 FPS | Build the crossover. Continue to Step 2. |
| 12-19 FPS | Build it, but apply the §8 reductions from the start. |
| < 12 FPS | **Stop this phase.** Keep the snap transition as the interim baseline, record the number, and defer crossover implementation to Phase 7.5. |

Whatever the number, write it into `IMPLEMENTATION_PLAN.md` §Phase 1 next to the
old one, and note that the original was taken in an unintended mode. Do not delete
the old measurement — the comparison is the useful part.

Also re-check `my_rounder_cb` (`main/main.cpp:82`) still behaves. It forces
even-aligned flush areas; direct mode changes what gets handed to it.

## 5. The target interaction

Two static images — **panes** — move together as one unit under the finger.
Nothing is instantiated during the drag.

```
   idle            drag 30%              drag 60%           released, settled
┌─────────┐    ┌───┬─────────┐    ┌──────────┬────┐    ┌─────────┐
│         │    │   │         │    │          │    │    │         │
│ current │    │pre│ current │    │   prev   │cur │    │  prev    │
│  pane   │    │   │  pane   │    │   pane   │pane│    │  live    │
└─────────┘    └───┴─────────┘    └──────────┴────┘    └─────────┘
 offset=0       offset=0.3W         offset=0.6W          switch done,
                                                         panes removed
```

- Both panes are **snapshots**, neither is live. The outgoing pane is captured when
  the drag starts; the incoming pane comes from cache (§6).
- They translate together. `offset` is the single piece of state.
- **Release past 50%** → animate `offset` to full width, then perform the real
  switch *behind* the panes, then remove them.
- **Release before 50%** → animate `offset` back to 0 and remove. No lifecycle
  events fire; the outgoing app was never paused.

This differs from `DESIGN.md` §5, which has the incoming card blurred, the outgoing
app live and stationary, and touch transferring at the 50% mark. The pane model
moves both, keeps neither live, and defers the switch until after release.

**`DESIGN.md` §5 is the authority; the pane model is a performance concession to
it, not a replacement.** What it preserves is the part the design is actually
about: the preview tracks the finger 1:1 from the edge, 50% is the commit
threshold, release past it completes to full screen, release before it returns to
the origin side. What it trades away is live incoming content and touch transfer
at the threshold. That trade is only acceptable while the render path cannot afford
them — see §11. Do not read this section as permission to drop finger tracking or
the 50% threshold; those are the requirement. If the pane model ships, record it in
`DESIGN.md` §5 as a deviation with its reason, and keep the live-content transfer
listed as open work. Update the diagram at `DESIGN.md:178` only when something
ships — it describes target state.

The advantage is that the expensive part (`onCreate()` of the incoming app) happens
once, after release, hidden behind an opaque pane — not during the drag where it
would stall the finger.

## 6. Step 2 — Cache panes at `onPause()`

Your instinct to pre-load both neighbours was right about *what* to cache and wrong
about *why*. It does not fix the banding (§1). What it buys is that each drag frame
becomes a **blit instead of a live render**, which is the only way the drag has a
chance of being affordable.

**Capture on the way out, not ahead of a drag.** To snapshot an app you need its
object tree. Building a neighbour's tree means calling `onCreate()` on it, which
breaks `max_running_num = 1` (`main/main.cpp:93`), puts three trees resident where
the design guarantees one (`DESIGN.md:203`), and produces an image that goes stale
at that app's next data update. At `onPause()` the tree is already live and the
capture is nearly free.

```
CrystalApp::close()  →  pause()  →  onPause()   ← capture here, tree alive
                     →  onDestroy()
                     →  Brookesia deletes the screen
```

`crystal_app.cpp:153-167` guarantees that ordering, so the tree is valid at
`onPause()`. Cards never visited this boot have no pane — use a flat fill in the
card's background colour. That is a real first-swipe difference and it is
acceptable; do not build a tree to avoid it.

Snapshot the **app's own screen**, not `lv_scr_act()`. Follow Brookesia's existing
pattern at `esp_brookesia_core_manager.cpp:329-381`: temporarily overwrite
`app->_active_screen->coords` with the visual area, call `lv_snapshot_take_to_buf`,
restore the coords. `lv_snapshot.c:119-129` renders through a fake display, so this
works on an object that is not the active screen.

Store panes at **exactly the size they will be drawn** — the app area, full
resolution. Then `zoom` stays at `LV_IMG_ZOOM_NONE` (256) and LVGL does an
untransformed RGB565 copy rather than an antialiased affine transform. That
distinction is most of the per-frame cost.

Reuse `downscale_crop()` from `crystal_shell.cpp` only if you decide panes should be
stored at reduced resolution (§8). For full-size panes no scaling is needed at all.
Do **not** carry the 90% crop into the panes — that crop belongs to the snap
transition's zoom effect and would make the drag look like it was zooming.

Suggested shape, in `crystal_shell.cpp`'s anonymous namespace:

```cpp
struct CardPane {
    lv_img_dsc_t *image = nullptr;   // app-area sized, RGB565
    size_t index = SIZE_MAX;         // which card this is
    bool valid = false;
};
```

Keep at most two cached (the two neighbours of the current card) plus the one
captured live at drag start. Free a pane when its card leaves the neighbour window,
so the count is bounded by position in the ring, not by the number of installed
apps.

## 7. Step 3 — Drive `offset` from the gesture

The shell already handles release (`crystal_shell.cpp:on_gesture_release`). Add the
other two event codes from `ESP_Brookesia_Gesture`:

```cpp
gesture->getPressEventCode()      // drag start — capture outgoing pane, build panes
gesture->getPressingEventCode()   // every tick — update offset
gesture->getReleaseEventCode()    // already wired — decide commit or cancel
```

`ESP_Brookesia_GestureInfo_t` carries live `start_x` / `stop_x`, refreshed every
`detect_period_ms`. The 480x480 stylesheet sets that to **20 ms** (50 Hz,
`stylesheets/480_480/dark/gesture_data.h:54`), comfortably above any frame rate you
will achieve, so the drag will not feel under-sampled.

State machine — keep it explicit and in one place:

```
IDLE ──press on edge──▶ DRAGGING ──release past 50%──▶ COMMITTING ──▶ IDLE
                            │                                          ▲
                            └──────release before 50%──▶ CANCELLING ───┘
```

Notes that will save you a debugging session:

- Entry condition must match the existing one: edge area flag plus direction, per
  `on_gesture_release`. `main/main.cpp:94-95` sets `direction_horizon = 12` and
  `horizontal_edge = 24`.
- No card either side → do not enter `DRAGGING` at all. Switching is non-wrapping.
- **Suppress app touch input while dragging.** The outgoing app is a static image;
  taps must not reach the live tree beneath it. Full gesture ownership is Phase 7's
  job — for now the panes being opaque and non-clickable on `lv_layer_top()` is
  enough, but make sure `LV_OBJ_FLAG_CLICKABLE` is cleared on both.
- Reuse `s_switching` so `on_app_event` does not fight the shell mid-transition.
- Cancel is not a special case. It is the same animation with a target of 0.

Use one `lv_anim_t` on `offset` for both outcomes, `kTransitionMs` with an ease-out
path. On completion of a commit, call the existing `start_card()` *while the panes
still cover the screen*, then remove them.

## 8. Step 4 — If FPS lands between 12 and 19

Apply in this order; each is independent and measurable.

1. **Half-resolution panes, 2x zoom.** Quarters the bytes touched per frame. Costs
   sharpness during motion only — the pane is replaced by the live app on settle.
   `downscale_crop()` already does this.
2. **One pane instead of two.** Slide the incoming pane over a *static* outgoing
   frame. Halves the per-frame blit. Visually weaker but much cheaper.
3. **Decouple the last stretch from the finger.** Track 1:1 to ~90%, let the
   animation carry the rest. The most expensive frames stop being input-latency
   sensitive.

Do not stack all three pre-emptively. Measure after each.

## 9. Memory budget

`CONFIG_SPIRAM_USE_MALLOC=y` with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` means
any allocation above 16 KB lands in PSRAM automatically. LVGL uses plain `malloc`
(`CONFIG_LV_MEM_CUSTOM_INCLUDE="stdlib.h"`), so panes go to PSRAM with no special
handling.

| Item | Bytes |
| --- | --- |
| Second RGB framebuffer (Step 1) | 480 x 480 x 2 = 460,800 |
| Pane, app-area size, each | 480 x 456 x 2 = 437,760 |
| Two cached panes + one live capture | ~1.31 MB |
| Transient full-screen snapshot during capture | 460,800, freed immediately |

Steady state adds roughly **1.77 MB** against 8 MB of octal PSRAM at 80 MHz.
Peak during capture is ~2.23 MB. Memory is not the constraint — frame time is.

Log `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` before and after Step 1 and after
the first pane allocation, and put the real numbers in the plan. The table above is
arithmetic, not measurement.

## 10. Rules for this work

- **Do not touch `clock_app`.** The owner has asked for it to be left alone twice.
- Keep this work separate from the lifecycle items retained historically in
  `HANDOFF.md`. That handoff is closed and is not part of crossover work.
- `idf.py build` must pass before you report anything. ESP-IDF lives at
  `~/.espressif/v6.1/esp-idf`; source `export.sh` first.
- Report the Step 0 and Step 1 numbers **as measured**, even when they defer the
  feature. A measured deferral closes this phase cleanly; an optimistic result
  wastes the next session too.
- `VALIDATION_CHECKLIST.md` records what the shipped shell *does*. It describes an
  interim fallback. It does not record a decision to drop the crossover, and a
  failed gate here does not create one — `DESIGN.md` §5 is the design authority and
  requires the crossover. A gate failure defers it and names the blocking cost.

## 11. Hardware result

Steps 0 and 1 ran on the Waveshare board on 2026-09-04.

- Single-buffer partial mode measured 49-96 ms render and 15-34 ms synchronous
  flush per refresh.
- Two-buffer avoid-tear direct mode measured 91-106 ms render and 3-10 ms
  synchronous flush per refresh.
- Sustained direct-mode dragging completed 8-10 refreshes per second; the LVGL
  long-window average decayed from 30 to 15 FPS.
- Display initialization consumed 922,232 bytes of PSRAM in direct mode.
- Physical observation confirmed that the obvious snapshot tearing disappeared.
- The measured rate is below the 12 FPS continuation gate, so Steps 2-4 were not
  implemented and the production shell retains snap/fade.
