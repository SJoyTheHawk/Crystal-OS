# P4 whole-screen teal flash investigation — 2026-11-18

## Evidence and confidence

The user reports a one-frame teal flash during Bus refreshes, including the
system status bar. No serial log synchronized with a flash has been captured.
The physical cause is not confirmed; disappearance remains a board check.

Local ESP-IDF v6.1 source, esp_lcd/dsi/esp_lcd_panel_dpi.c, explicitly identifies
a blue frame when scanout cannot fetch external memory fast enough. Its ISR logs:

    can't fetch data from external memory fast enough, underrun happens

This is a strong candidate. LVGL refresh-debug highlighting is disabled. Bus
does not intentionally paint system chrome. Earlier Wi-Fi teal-flash fixes in
ESP32_P4_PHASE2_VALIDATION.md did not isolate their physical mechanism and do
not prove this case has the same cause.

## Changes

- P4 startup gives DW-GDMA memory-port reads a QoS priority of at least 1, above
  the CPU/cache/DMA2D reset priority 0. IDF's dw_gdma_ll.h maps memory reads to
  master 1; DPI scanout uses that port. Write priority and any higher existing
  read priority are preserved. This is a contention mitigation, not a measured
  guarantee. Other DW-GDMA memory readers also share this port.
- CrystalUI compares existing label text before calling LVGL, including button
  captions and list rows. Bus republishes unchanged ETA minutes/status text;
  LVGL otherwise reallocates/reflows/invalidates these labels. Skipping them
  reduces unnecessary partial-frame copying and PSRAM traffic.
- Editable textarea events remain unchanged. No additional timer, full-page
  repaint, public Lua API, display clock reduction, SDK patch or app data change.

These are firmware changes. Bus Lite remains 0.2.25; its installed package and
favorites can stay. A new CAPP cannot deploy these changes.

## Verification

The production Lua/LVGL Bus harness passes at 480 and 720 pixels, including
catalog, persistence, endpoints, UI and memory tests. Its new regression verifies
30 identical label/button/list updates cause zero flushes and changed values
still redraw. Host tests cannot verify P4 scanout timing or AXI arbitration.

Board retest:

1. Use idf.py app-flash monitor from the configured ESP-IDF terminal. Confirm
   the startup line Display PSRAM read priority: 1 (or higher).
2. Keep Bus installed. Open several Favorites and wait through automatic
   refreshes, then refresh manually.
3. Search/change routes, scroll long stop lists, open ETA and switch tabs while
   requests finish. Watch system chrome as well as the app.
4. Monitor for at least five minutes. If a flash occurs, save surrounding serial
   output and note whether the underrun message appears. Also check touch,
   scrolling and network completion after the priority adjustment.
5. If flashes persist without underrun logs, investigate framebuffer ownership
   and cache coherency rather than raising QoS blindly.

ESP-IDF v6.1 P4 build passes. Signed build/crystal_os.bin is 0x4c1000 bytes
(41% app slot free), SHA-256:

    0b8d2e1a5654971c60ac46756f0fa32cda5f88dffdf3826cdd1d40dfdfbb54db

Not flashed in this session. Board verification, flash disappearance and
performance comparison are pending.
