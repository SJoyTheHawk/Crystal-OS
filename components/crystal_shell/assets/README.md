# Crystal boot logo

Source: the user-supplied reference/crystalos-boot_icon.png, **598x598 RGBA**.
Keep that original unchanged. Original SHA-256:
49a83f0853a456a928073e01158aac1c4fd48571b807d6e11cf5685d0da46a27.

crystalos_boot_logo_256.png is a **256x256** copy with the same square
proportions and transparent background. It is displayed at native zoom on
Crystal's #11151B boot background. The logo remains visible through Ready
and the existing boot fade; it replaces the circular spinner.

crystalos_boot_logo_256.bin is **196,608 bytes** of LVGL 8 native RGB565 plus
an alpha byte per pixel. It is embedded only for P4, with no runtime image
decoder, resize, animation, recoloring, or additional heap-backed image buffer.

To reproduce from the repository root on macOS:

    sips --resampleHeightWidth 256 256 reference/crystalos-boot_icon.png \
      --out components/crystal_shell/assets/crystalos_boot_logo_256.png
    python3 tools/prepare_boot_logo.py

To render and check the production boot layout with real LVGL:

    python3 tools/test_p4_visuals.py --output-dir /private/tmp/crystal-boot-preview

Image ownership and usage rights remain with the supplied artwork's owner.
