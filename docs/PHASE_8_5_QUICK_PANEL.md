# Phase 8.5 — Corner-anchored quick panel

## Purpose

Phase 8 shipped quick settings as a full-screen overlay: a 480-wide panel over a
blurred snapshot, with every unoccupied pixel filled by the snapshot's grey wash.
This add-on replaces it with a panel that wraps its own content, hangs from the
top-right corner, and leaves the app visible everywhere else — the macOS Control
Center model. Content moves from absolute `lv_obj_align()` offsets to a real
grid, and on/off state moves from `lv_switch` to tile colour.

Nothing about gesture ownership, app lifecycle, or the one-resident-app rule
changes. This is a layout, input-zone, and state-presentation change only.

## Decisions

**Trigger shrinks to the top-right corner.** A downward drag inside the top-right
`kQuickCornerWidth` opens the panel; the rest of the top band returns to the app.
The gesture now starts where the panel appears.

**The panel is opaque, not frosted.** Dropping the snapshot removes ~880 KiB of
transient PSRAM allocation and a full box-average downscale pass from every open,
and lets the panel body draw through LVGL's solid-fill path instead of blending a
zoomed image on every drag frame. A vertical gradient plus a hairline top
highlight carry the depth; `LV_GRAD_DIR_VER` is also a fast path, while
`shadow_width` is not and is deliberately unused on the panel body.

**Tile colour carries state; text carries everything colour cannot.** Off is a
translucent white tile, on is an accent fill. Text stays wherever the state is
not binary — see [Tile state model](#tile-state-model).

## Geometry

Derived, never hardcoded to 480. The panel hangs below the status bar, whose
bottom edge is already available as `active_app_area().y1`.

```text
kQuickCell    62    grid cell, square
kQuickGap     10    row and column gap
kQuickPad     14    panel inner padding
kQuickMargin  12    gap to the display's right edge
kQuickGapTop   8    gap below the status bar

panel  = 4 * 62 + 3 * 10 + 2 * 14 = 306 x 306
x      = hor_res - 306 - 12
y_rest = active_app_area().y1 + 8
```

A square panel is intentional: it reads as one object rather than a truncated
sheet, and it keeps the 4x4 grid uniform so every tile is a whole number of
cells. `kQuickPanelHeight` (currently 320) is replaced by this derived value.

## Layout

```text
+-----------------------------------------------+
| status bar                                    |
|                    +----------------------+   |
|                    | +--------++--------+ |   |
|                    | | ((*))  ||   BT   | |   |
|                    | | WiFi   ||Bluetoo.| |   |
|                    | |MyNetwo.||Unavail.| |   |
|     app, fully     | +--------++--------+ |   |
|     visible and    | +--++--++----++----+ |   |
|     untouched      | |##||##||Ener||Gear| |   |
|                    | |##||##|| Off||    | |   |
|                    | |##||##|+----++----+ |   |
|                    | |##||##|             |   |
|                    | |Br||Vo| (reserved)  |   |
|                    | +--++--+             |   |
|                    +----------------------+   |
+-----------------------------------------------+
```

| Element | Column | Row | Cells |
| --- | --- | --- | --- |
| WiFi | 0–1 | 0–1 | 2x2 |
| Bluetooth | 2–3 | 0–1 | 2x2 |
| Brightness bar | 0 | 2–3 | 1x2 |
| Volume bar | 1 | 2–3 | 1x2 |
| Energy Saving | 2 | 2 | 1x1 |
| Settings gear | 3 | 2 | 1x1 |
| *reserved* | 2–3 | 3 | — |

Every toggle is 1x1. A wide Energy Saving tile would be the only stretched toggle
in the panel, which makes it read as more important than WiFi's status rather
than as one switch among several.

The two cells at columns 2–3 of row 3 stay **empty and reserved**. They are grid
cells that exist and are skipped, not padding: Phase 9's WiFi SSID expansion and
one further tile land there without a relayout. Nothing is invented to occupy
them, and no filler tile (battery readout, clock, decorative graphic) may be
added to square off the panel.

The grab handle is gone. It belonged to a full-width sheet; a corner card that
animates from its own corner does not need one.

Radii: 22 on the panel, 16 on the 2x2 tiles, 14 on the 1x1 tiles, 18 on the
vertical bars. Larger container, larger radius — the ratio is what reads as
deliberate rather than uniformly rounded.

## Palette

```text
panel top        0x2A2F38     gradient start
panel bottom     0x1B1E24     gradient end
panel highlight  0xFFFFFF @ 10%   1px top border only
tile off         0xFFFFFF @ 12%   translucent over the panel
tile on          0x3B82F6     accent fill
tile unavailable 0xFFFFFF @ 6%    plus 40% text opacity
text primary     0xF2F4F7
text secondary   0xF2F4F7 @ 60%
```

The panel body is opaque (`LV_OPA_COVER`), not the 95% of Phase 8. Tiles are
translucent *over the panel*, which is a blend against a known solid colour — the
cheap case — not against live app pixels.

## Tile state model

`lv_switch` is removed. A toggle is a tile whose background colour is its state,
using LVGL's `LV_STATE_CHECKED` so one style definition covers both appearances:

| State | Background | Text |
| --- | --- | --- |
| Off | `tile off` | name + `Off` |
| On | `tile on` | name + `On` |
| Unavailable | `tile unavailable` | name + reason |

Colour alone is not enough, and the reason is state count rather than colour
perception. Off and unavailable both render as a dim tile; without text,
Bluetooth — reserved and non-interactive in v1 — is indistinguishable from Energy
Saving being switched off, and a user taps it indefinitely. WiFi carries at least
three states (off, on but not associated, connected to *SSID*) that no single
colour can express. So:

- **Bars** (brightness, volume) carry no text. A fill level is self-evident.
- **Energy Saving** carries a one-word state line, so off never reads as broken.
- **WiFi and Bluetooth** carry a status line, since their states are not binary.

Feedback on press is a scale-down to 97% via `lv_obj_set_style_transform_zoom`
in `LV_STATE_PRESSED`. It costs one style property and no extra draw pass.

## Interaction

```text
Drag down, start inside top-right corner band
  → claim CrystalGestureOwner::QuickSettings
  → build the panel above the display, offset by its own height
  → panel y tracks the finger 1:1, clamped to [y_hidden, y_rest]

Release past 50% of travel   → animate to y_rest, 200ms ease-out
Release below 50%            → animate to y_hidden, then destroy
Drag up while open           → same claim, same thresholds, inverted
Tap outside the panel        → animate to y_hidden, then destroy
Tap the gear                 → dismiss, then open Settings
```

Tap-to-dismiss uses a transparent, full-screen, clickable object on
`lv_layer_top()` beneath the panel. LVGL hit-tests by area, not by opacity, so
`LV_OPA_TRANSP` still receives clicks — no visible scrim is required, which is
what keeps the app unobscured.

Two guards on that catcher, both of which are bugs if omitted:

1. It is added **after** the open animation completes, not at build time. A pull
   gesture that releases with the finger outside the panel would otherwise
   register as an immediate dismiss.
2. It ignores clicks while `s_gesture_owner == CrystalGestureOwner::QuickSettings`
   or while a settle animation is running.

Panel motion is translate-only. Do not animate opacity: in LVGL 8 a non-opaque
object with children is composited through an intermediate buffer, which on this
single-buffer RGB panel is exactly the cost the opaque decision avoided.

## Gesture zone

`kTopBand` still gates the vertical component; the new `kQuickCornerWidth` gates
the horizontal one.

```cpp
constexpr lv_coord_t kQuickCornerWidth = 120;

const bool in_corner = info->start_x >= lv_disp_get_hor_res(nullptr) - kQuickCornerWidth;
const bool top_pull  = info->start_y < kTopBand && in_corner &&
                       info->direction == ESP_BROOKESIA_GESTURE_DIR_DOWN;
```

Dismiss-up keeps working from anywhere while the panel is open — that precedence
rule in `DESIGN.md` §4 is unchanged. Only the *open* gesture narrows.

## Code sketch

```cpp
// Grid descriptors must outlive the panel: LVGL stores the pointers, not copies.
static lv_coord_t s_cols[] = {kQuickCell, kQuickCell, kQuickCell, kQuickCell,
                              LV_GRID_TEMPLATE_LAST};
static lv_coord_t s_rows[] = {kQuickCell, kQuickCell, kQuickCell, kQuickCell,
                              LV_GRID_TEMPLATE_LAST};

lv_obj_set_grid_dsc_array(panel, s_cols, s_rows);
lv_obj_set_style_pad_row(panel, kQuickGap, 0);
lv_obj_set_style_pad_column(panel, kQuickGap, 0);
lv_obj_set_style_pad_all(panel, kQuickPad, 0);

// WiFi: 2 columns from 0, 2 rows from 0.
lv_obj_set_grid_cell(wifi, LV_GRID_ALIGN_STRETCH, 0, 2,
                           LV_GRID_ALIGN_STRETCH, 0, 2);
// Energy Saving: single cell at (2,2). Nothing is placed at row 3, cols 2-3.
lv_obj_set_grid_cell(energy, LV_GRID_ALIGN_STRETCH, 2, 1,
                             LV_GRID_ALIGN_STRETCH, 2, 1);
```

The `static` on the descriptors is load-bearing. `lv_obj_set_grid_dsc_array()`
retains the array pointer; a stack-local array leaves LVGL reading freed memory
on the next layout pass.

```cpp
void energy_tile_event(lv_event_t *event)
{
    lv_obj_t *tile = static_cast<lv_obj_t *>(lv_event_get_target(event));
    const bool on = !lv_obj_has_state(tile, LV_STATE_CHECKED);
    on ? lv_obj_add_state(tile, LV_STATE_CHECKED)
       : lv_obj_clear_state(tile, LV_STATE_CHECKED);
    lv_label_set_text(sublabel(tile), on ? "On" : "Off");

    const uint8_t flag = on ? 1 : 0;
    if (hal().storage != nullptr) hal().storage->set("power.saving", &flag, sizeof(flag));
    // Effects (CPU cap, WIFI_PS_MAX_MODEM, brightness ceiling) land in Phase 11.
}
```

Phase 8.5 owns the flag and its appearance only. Wiring the flag to real power
behaviour stays in Phase 11 per `DESIGN.md` §8, so this tile persists state and
looks correct without yet changing clocks or radios.

## Implementation steps

1. Replace `kQuickPanelHeight` with derived cell-based geometry; add
   `kQuickCell`, `kQuickGap`, `kQuickPad`, `kQuickMargin`, `kQuickCornerWidth`.
2. Delete the snapshot path from `create_quick_settings()` — the
   `capture_app_area_full()` call, the `downscale_crop()`, and the background
   image child. Keep both functions; the card crossover still uses them.
3. Rebuild the root as a transparent full-screen hit-catcher holding one
   corner-anchored panel, and animate the panel rather than the root.
4. Convert panel content to `lv_obj_set_grid_dsc_array()` with the cell
   assignments above, leaving row 3 columns 2–3 unplaced.
5. Add the checked/unchecked tile style pair and the pressed transform; replace
   the `lv_switch` with an Energy Saving tile plus state sublabel.
6. Narrow the open trigger to the corner band in `on_gesture_pressing()`.
7. Add tap-outside dismissal with both guards, and confirm the panel's own
   children never see the catcher's clicks.
8. Verify on hardware: drag tracking, both thresholds, tap-outside, gear,
   brightness clamp at 95, and no leaked `lv_obj` or image buffer after close.

## Invariants

- The app beneath is never covered outside the panel rect, and never dimmed.
- No `lv_snapshot_take()` on the quick-settings path.
- Panel animation is translate-only; opacity and blur are not animated.
- Reserved grid cells stay empty until a real control needs them.
- Brightness stays clamped to 95 (`BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX`).
- Every state distinction has a non-colour cue wherever more than two states
  exist.
- Closing the panel frees the whole tree; `s_quick_*` pointers return to
  `nullptr` in one place.

## Validation checklist

- [ ] Downward drag in the top-right corner opens the panel; the same drag at
  top-left or top-centre reaches the app instead.
- [ ] Panel tracks the finger 1:1 and settles per the 50% threshold both ways.
- [ ] App content outside the panel stays fully visible and undimmed throughout.
- [ ] Tap outside dismisses with the 200ms ease-out; a release-outside at the end
  of the opening drag does **not** dismiss.
- [ ] Energy Saving changes colour and sublabel, and survives reboot.
- [ ] Bluetooth is visibly unavailable and cannot be tapped into a state.
- [ ] Brightness and volume bars drag over their full height; brightness stops at
  95.
- [ ] Reserved cells are empty, with no stray background or border.
- [ ] Card crossover and preview persistence still work — shared helpers intact.
- [ ] PSRAM free returns to its pre-open value after close, repeated 10x.

## Superseded documentation

`DESIGN.md` has been updated to match this spec. What changed:

| Location | Was | Now |
| --- | --- | --- |
| §2 layer rules | quick settings covers the indicator bar and keyboard | overlaps only what falls inside the panel rect |
| §3 page inventory | *top-edge pull*, *overlay* | *top-right corner pull*, *corner overlay* |
| §4 gesture table | drag down, top band ≤20px | top band ≤20px, right 120px only; tap-outside row added |
| §4 constants | `kTopBand = 20` | adds `kQuickCornerWidth = 120` |
| §4 precedence 2 | only dismiss-up available | dismiss-up and tap-outside |
| §4 `kTopBand` note | — | adds the discoverability trade-off |
| §6 | full-width diagram, ~320px, blurred snapshot, grab handle, `lv_switch` | corner grid panel, opaque gradient, colour-state tiles, reserved cells |

`IMPLEMENTATION_PLAN.md` gains a Phase 8.5 entry; the Phase 8 entry is marked
shipped and superseded rather than rewritten, since it describes what actually ran
on hardware.

Unchanged and still authoritative: §10 motion (200ms ease-out, drag tracking 1:1
with no easing), the brightness 0..95 clamp, §8 power-saving effects belonging to
Phase 11, and the whole WiFi flow in §6.

## Status

Specified, not yet implemented. `DESIGN.md`, `IMPLEMENTATION_PLAN.md`, and
`CODE_GUIDE.md` are aligned to this spec.

