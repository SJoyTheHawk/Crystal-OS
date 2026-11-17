# App icon sources

These JPEGs are source artwork only and are not copied into the device storage
partition. Run `python3 tools/prepare_app_icons.py` with FFmpeg installed to
regenerate the firmware assets.

The converter emits opaque `LV_IMG_CF_TRUE_COLOR` descriptors in the firmware's
native RGB565 format. Pixels are stored directly in flash and drawn without
JPEG decoding or runtime pixel generation. The Crystal fallback is emitted at
98px and 112px for Brookesia launcher presets; both 112px presets share one
pixel buffer. The Bus Lite artwork is written to
`examples/bus3p/assets/icon.bin`, a package-owned `CR16` binary image containing
the magic, little-endian width and height, and raw RGB565 pixels. Its `app.yaml`
manifest references that file so the Lua app runtime owns its launcher icon.
