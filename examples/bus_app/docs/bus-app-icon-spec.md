# Bus App Icon Specification
**Date:** 2026-09-24  
**Status:** Ready for Implementation  
**File:** `components/bus_app/src/bus_icon.c`

---

## Design Reference

**Source:** Official KMB App icon (front view of double-decker bus)
- Bold red background (#E31E24 - KMB brand color)
- White bus silhouette (simplified, high contrast)
- Flat, iconic style suitable for 64×64px

---

## Crystal OS Implementation

### Canvas: 64×64 pixels

```
     0  4  8  12 16 20 24 28 32 36 40 44 48 52 56 60 64
     ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
  0  │                                                 │
  4  │          ╔═══════════════════════╗             │
  8  │          ║                       ║             │
 12  │          ║  ◉ ◉          ◉ ◉    ║             │ ← Headlights
 16  │          ║                       ║             │
 20  │          ║  ┌────┐     ┌────┐   ║             │ ← Windows
 24  │          ║  │    │     │    │   ║             │
 28  │          ║  └────┘     └────┘   ║             │
 32  │          ║                       ║             │
 36  │          ║  ┌──────────────┐    ║             │ ← Windshield
 40  │          ║  │              │    ║             │
 44  │          ║  └──────────────┘    ║             │
 48  │          ╚═══════════════════════╝             │
 52  │             ●                 ●                │ ← Wheels
 56  │                                                 │
 60  │                                                 │
 64  └─────────────────────────────────────────────────┘
```

---

## Color Palette

```c
// bus_icon.c

// Background (transparent - uses launcher background)
#define ICON_BG            lv_color_hex(0x1E293B)  // Dark slate (for reference)

// Primary colors
#define ICON_BUS_RED       lv_color_hex(0xE31E24)  // KMB brand red
#define ICON_WHITE         lv_color_hex(0xF8FAFC)  // Off-white (outline/details)

// Accent colors
#define ICON_WINDOW_BLUE   lv_color_hex(0x38BDF8)  // Sky blue (glass tint)
#define ICON_HEADLIGHT     lv_color_hex(0xFBBF24)  // Amber/yellow
#define ICON_WHEEL_GRAY    lv_color_hex(0x334155)  // Dark slate gray
```

---

## Drawing Primitives

### Layer 1: Bus Body (Base)
```c
lv_obj_t *body = lv_obj_create(canvas);
lv_obj_set_size(body, 48, 44);
lv_obj_set_pos(body, 8, 4);  // Centered horizontally, near top
lv_obj_set_style_bg_color(body, ICON_BUS_RED, 0);
lv_obj_set_style_radius(body, 6, 0);  // Rounded corners
lv_obj_set_style_border_width(body, 0, 0);
```

### Layer 2: White Outline/Frame
```c
lv_obj_set_style_border_width(body, 2, 0);
lv_obj_set_style_border_color(body, ICON_WHITE, 0);
```

### Layer 3: Windows (Upper Deck)
```c
// Left window
lv_obj_t *win_left = lv_obj_create(body);
lv_obj_set_size(win_left, 10, 12);
lv_obj_set_pos(win_left, 8, 16);
lv_obj_set_style_bg_color(win_left, ICON_WINDOW_BLUE, 0);
lv_obj_set_style_radius(win_left, 2, 0);
lv_obj_set_style_border_width(win_left, 1, 0);
lv_obj_set_style_border_color(win_left, ICON_WHITE, 0);

// Right window
lv_obj_t *win_right = lv_obj_create(body);
lv_obj_set_size(win_right, 10, 12);
lv_obj_set_pos(win_right, 28, 16);
// Same style as left
```

### Layer 4: Windshield (Front Window)
```c
lv_obj_t *windshield = lv_obj_create(body);
lv_obj_set_size(windshield, 32, 12);
lv_obj_set_pos(windshield, 8, 32);
lv_obj_set_style_bg_color(windshield, ICON_WINDOW_BLUE, 0);
lv_obj_set_style_radius(windshield, 2, 0);
lv_obj_set_style_border_width(windshield, 1, 0);
lv_obj_set_style_border_color(windshield, ICON_WHITE, 0);
lv_obj_set_style_opa(windshield, LV_OPA_70, 0);  // Slightly transparent
```

### Layer 5: Headlights (4 small circles)
```c
// Top-left headlight
lv_obj_t *hl_tl = lv_obj_create(body);
lv_obj_set_size(hl_tl, 4, 4);
lv_obj_set_pos(hl_tl, 10, 8);
lv_obj_set_style_bg_color(hl_tl, ICON_HEADLIGHT, 0);
lv_obj_set_style_radius(hl_tl, LV_RADIUS_CIRCLE, 0);
lv_obj_set_style_border_width(hl_tl, 0, 0);

// Top-right headlight
lv_obj_t *hl_tr = lv_obj_create(body);
lv_obj_set_size(hl_tr, 4, 4);
lv_obj_set_pos(hl_tr, 18, 8);
// Same style

// Bottom-left headlight
lv_obj_t *hl_bl = lv_obj_create(body);
lv_obj_set_size(hl_bl, 4, 4);
lv_obj_set_pos(hl_bl, 26, 8);
// Same style

// Bottom-right headlight
lv_obj_t *hl_br = lv_obj_create(body);
lv_obj_set_size(hl_br, 4, 4);
lv_obj_set_pos(hl_br, 34, 8);
// Same style
```

### Layer 6: Wheels (2 circles, outside body)
```c
// Left wheel
lv_obj_t *wheel_l = lv_obj_create(canvas);
lv_obj_set_size(wheel_l, 10, 10);
lv_obj_set_pos(wheel_l, 14, 50);
lv_obj_set_style_bg_color(wheel_l, ICON_WHEEL_GRAY, 0);
lv_obj_set_style_radius(wheel_l, LV_RADIUS_CIRCLE, 0);
lv_obj_set_style_border_width(wheel_l, 1, 0);
lv_obj_set_style_border_color(wheel_l, ICON_WHITE, 0);

// Right wheel
lv_obj_t *wheel_r = lv_obj_create(canvas);
lv_obj_set_size(wheel_r, 10, 10);
lv_obj_set_pos(wheel_r, 40, 50);
// Same style
```

---

## Simplified Alternative (If Above is Too Complex)

**Option B: Solid Silhouette**

```c
void bus_icon_prepare_simple(lv_obj_t *canvas) {
    // 1. Red rounded square background
    lv_obj_t *bg = lv_obj_create(canvas);
    lv_obj_set_size(bg, 56, 56);
    lv_obj_center(bg);
    lv_obj_set_style_bg_color(bg, ICON_BUS_RED, 0);
    lv_obj_set_style_radius(bg, 8, 0);
    
    // 2. White bus silhouette (simple rounded rect with cutouts)
    lv_obj_t *bus = lv_obj_create(bg);
    lv_obj_set_size(bus, 40, 48);
    lv_obj_center(bus);
    lv_obj_set_style_bg_color(bus, ICON_WHITE, 0);
    lv_obj_set_style_radius(bus, 4, 0);
    
    // 3. Red "windows" (cutouts - actually red rects on top)
    // Create red rects to simulate window gaps
    // ... (2-3 rects to break up the white silhouette)
}
```

**Pros of Option B:**
- Fewer draw calls (faster rendering)
- Cleaner look at small size
- Easier to maintain

**Cons:**
- Less detailed
- May look too generic

**Recommendation:** Start with Option B (simple silhouette), upgrade to detailed version later if needed.

---

## Implementation Checklist

- [ ] Create `components/bus_app/src/bus_icon.c`
- [ ] Add `bus_icon_prepare(lv_obj_t *canvas)` function
- [ ] Call from `BusApp` constructor (like clock/weather)
- [ ] Test on device at 64×64px
- [ ] Validate contrast on dark launcher background
- [ ] Ensure rendering completes in <16ms
- [ ] Compare with other app icons (visual consistency)

---

## Fallback Icon (If Procedural Generation Fails)

**Emoji-style:**
- Unicode character: 🚌 (U+1F68C Bus)
- Render as text label with large font
- Color: Red text on transparent background

```c
lv_obj_t *label = lv_label_create(canvas);
lv_label_set_text(label, LV_SYMBOL_BUS);  // Or "🚌"
lv_obj_set_style_text_color(label, ICON_BUS_RED, 0);
lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
lv_obj_center(label);
```

---

## References

- Official KMB App: `/Users/szemy/Workspace/ESP32 Crystal OS/reference/unnamed.png`
- Clock icon implementation: `components/clock_app/src/clock_icon.c`
- Weather icon implementation: `components/weather_app/src/weather_icon.c`
- Calculator icon implementation: `components/calculator_app/src/calculator_icon.c`

---

## Visual Testing

**Test cases:**
1. Icon alone on dark background (launcher)
2. Icon with "Bus" label below
3. Icon in app switcher (Recent Apps)
4. Icon at different brightness levels (day/night)
5. Icon next to other app icons (relative size/weight)

**Success criteria:**
- Instantly recognizable as a bus/transport app
- Visually distinct from clock/weather/calculator
- Readable at 64×64px from arm's length (~40cm)
- Renders in <16ms on ESP32-S3

---

**Next Step:** Implement `bus_icon_prepare()` using Option B (simple silhouette) first, measure performance, then enhance if needed.
