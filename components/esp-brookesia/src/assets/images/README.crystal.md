# Crystal OS wallpaper

The active assets use reference/crystalos-logo-3d-bluegreen.jpg (1024x1024),
resized with Lanczos filtering to the native wallpaper dimensions without cropping.
They use the same opaque LV_IMG_CF_TRUE_COLOR format, const pixel arrays,
alignment attributes, descriptors, and symbols as the original Brookesia assets.
On the existing 16-bit, non-swapped display configuration, pixels are RGB565.

| Asset | Usage | Active format | Pixel payload on RGB565 builds |
| --- | --- | --- | --- |
| 720x720 | Crystal OS P4 | True color, RGB565 | 1,036,800 bytes |
| 480x480 | Crystal OS S3 | True color, RGB565 | 460,800 bytes |
| 240x240 | Brookesia 320x240 / 320x480 presets; unused by Crystal OS | True color, RGB565 | 115,200 bytes |

Run python3 tools/prepare_wallpaper.py to regenerate the C assets (requires
FFmpeg). This tool runs offline; it is not part of firmware startup. It retains
LVGL's 1-, 8-, 16- (both byte orders), and 32-bit true-color configurations.
Only the selected color-depth branch is compiled.

## Launcher FPS investigation — 2026-11-15

The user reports approximately 20 FPS with the stock Brookesia wallpaper and
10 FPS with the earlier Crystal wallpaper during launcher page changes. These
are user-reported device observations. On 2026-11-15, after testing the RGB565
Crystal version, the user confirmed: "The fps is matching the brookesia's".
The wallpaper-related FPS regression is resolved in this device comparison;
retain RGB565 as the active wallpaper format. The confirmation reports parity
with the earlier approximately 20 FPS baseline, not a new precise measurement.

The earlier custom 720x720 asset used LV_IMG_CF_INDEXED_8BIT (519,424 bytes).
Local LVGL source inspection identifies additional work in that format:

- src/draw/lv_img_decoder.c builds a palette and decodes indexed pixels into
  color-plus-alpha scanlines. In-memory true-color images return their original
  pixel pointer directly.
- src/draw/lv_draw_img.c draws indexed images through the line-reading path;
  src/draw/sw/lv_draw_sw_img.c has a direct blend path for opaque, untransformed,
  unrecolored true-color buffers without masks.
- The firmware has CONFIG_LV_IMG_CACHE_DEF_SIZE=0. The indexed format also
  rebuilds its palette when the image is reopened. Enabling the ordinary image
  cache alone would not remove its scanline expansion path.

These format-dependent costs are a likely contributor to the reported slowdown.
The Crystal wallpaper now matches the stock RGB565 format. This adds 517,376
bytes versus the indexed 720x720 asset, matching the original wallpaper's payload.

The painting method is the existing Brookesia home background:
components/esp-brookesia/src/core/esp_brookesia_core_home.cpp sets
lv_obj_set_style_bg_img_src on the main screen. The P4 stylesheet selects the
720x720 descriptor. The wallpaper uses native size with no runtime zoom,
recoloring, alpha blending, or extra image overlay. Launcher paging, display
settings, and the separate boot-logo assets are unchanged by this fix.

## Validation

Run python3 tools/test_wallpaper_rendering.py (CMake and a C++ compiler required).
It compiles production Brookesia paging, the Crystal stylesheet, both the stock
and generated 720x720 assets, and real LVGL with AddressSanitizer and image cache
disabled to match firmware. It checks native opaque buffer drawing, absence of
palette/decoded-copy allocation, zero scanline decoding during animated page
changes, and full rendered pixel equality. It also validates the wallpaper
formats and payload sizes for all three resolutions.

The check passed: both stock and Crystal used 946 direct wallpaper draw calls
and zero decoded lines in the same page-change sequence. RGB565 payloads also
matched the resized source artwork. The P4 firmware build and whitespace checks
passed. The subsequent user device check confirms FPS parity with Brookesia.
The agent's validation remains host tests and the firmware build; no agent-run
flash, serial capture, or device FPS measurement accompanied this confirmation.
Broader sustained FPS and soak qualification remains separate.

Stock wallpaper source remains in
reference/Crystal-OS-bus-app-development/components/esp-brookesia/src/assets/images/.
Brookesia documentation PNG/SVG previews still show the stock reference artwork.
