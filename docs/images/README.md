# README images

## Crystal OS screenshots

`launcher.png`, `quick_settings.png`, `weather.png`, `clock.png`, and
`calculator.png` are 480 × 480 captures of the production LVGL interfaces,
rendered by the host harness with deterministic sample data. They are not
photographs of a physical device.

Regenerate them with `python3 tools/capture_readme.py`; see the
[capture instructions](../../tools/README.md#readme-screenshots).

## Waveshare hardware photos

The hardware images identify the board targeted by Crystal OS. They are
unchanged copies of manufacturer images already present in the board
reference's `docs/assets/wiki/` directory:

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
Local copies allow the main README to display the photos without initializing
the reference submodule.
