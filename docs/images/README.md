# README images

## Crystal OS screenshots

`launcher.png`, `quick_settings.png`, `weather.png`, `clock.png`,
`calculator.png`, and `ethernet.png` are **720 × 720 ESP32-P4 captures**,
regenerated on November 15, 2026. They render the production LVGL interfaces
and `crystal_stylesheet_create_dark()` with `CONFIG_IDF_TARGET_ESP32P4=1`,
including the current status-bar offset and Quick Settings layout.

The host harness installs a selected gallery of Clock, Weather, Calculator,
and Ethernet. Time, weather, battery, brightness, volume, and connectivity are
deterministic fixtures. Ethernet is the selected route; Wi-Fi is enabled but
disconnected. The example IP `192.0.2.42`, gateway/DNS `192.0.2.1`, and locally
administered MAC are fixture values, not device or network observations.
Screenshots are not photographs or evidence of physical-device performance;
the volume/battery display does not qualify those P4 hardware paths.

Regenerate them with `python3 tools/capture_readme.py`; see the
[capture instructions](../../tools/README.md#readme-screenshots).

## Waveshare hardware photos

The current README uses
[`esp32-p4-wifi6-touch-lcd-4b.jpg`](hardware/esp32-p4-wifi6-touch-lcd-4b.jpg),
an unchanged copy of the manufacturer's image in the checked-in
[P4 board reference](../../reference/ESP32-P4-WIFI6-Touch-LCD-4B-main/docs/images/esp32-p4-wifi6-touch-lcd-4b.jpg).
See the [vendor reference README](../../reference/ESP32-P4-WIFI6-Touch-LCD-4B-main/README.md)
for its product context. Any UI visible in the product photo is vendor imagery,
separate from the Crystal OS simulator captures.

### Retained S3 images

The older S3 manufacturer images are retained for historical references. They
are unchanged copies from the S3 reference's `docs/assets/wiki/` directory:

| Local image | Content |
| --- | --- |
| [ESP32-S3-Touch-LCD-4B-inter001.jpg](hardware/ESP32-S3-Touch-LCD-4B-inter001.jpg) | Touch display and 86-box enclosure; the screen shows the vendor demo UI |
| [ESP32-S3-Touch-LCD-4B-details-intro.jpg](hardware/ESP32-S3-Touch-LCD-4B-details-intro.jpg) | PCB and numbered component callouts; consult the hardware wiki for the legend |

Source: [Waveshare ESP32-S3-Touch-LCD-4B wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B).
The board reference records [wiki revision 137986](https://www.waveshare.net/w/index.php?title=ESP32-S3-Touch-LCD-4B&oldid=137986)
and a retrieval date of July 28, 2026. See its
[resource manifest](../../reference/ESP32-S3-Touch-LCD-4B/docs/wiki-resources.md)
and [image inventory](../../reference/ESP32-S3-Touch-LCD-4B/docs/assets/wiki/README.md).

Image credit: Waveshare. These third-party product images retain their original
rights and are not covered by the license for original Crystal OS artwork.
Local copies allow the main README to display its hardware photo independently
of the vendor reference checkout.
