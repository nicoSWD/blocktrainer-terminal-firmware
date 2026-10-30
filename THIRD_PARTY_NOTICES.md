# Third-party notices

The firmware images in the releases are built from this repository's code (MIT, see `LICENSE`)
together with the libraries below, which keep their own licenses. Check each project for the full
license text and copyright notices.

| Component | Version | License | Source |
|---|---|---|---|
| LVGL | 8.3.x | MIT | https://github.com/lvgl/lvgl |
| ArduinoJson | 6.21.6 | MIT | https://github.com/bblanchon/ArduinoJson |
| FastLED | 3.6.0 | MIT | https://github.com/FastLED/FastLED |
| LovyanGFX | 1.x | BSD-2-Clause (some files MIT/CC0, see its repository) | https://github.com/lovyan03/LovyanGFX |
| Arduino-ESP32 core (via PlatformIO `espressif32@6.9.0`) | 2.0.x | LGPL-2.1-or-later | https://github.com/espressif/arduino-esp32 |
| ESP-IDF and its components | 4.4.x | Apache-2.0 (components carry their own notices) | https://github.com/espressif/esp-idf |
| Roboto Medium (UI font, embedded as bitmaps in `firmware/src/fonts/`) | 3.x | SIL Open Font License 1.1 | https://github.com/googlefonts/roboto-3-classic |
| Montserrat (built into LVGL; used for symbol glyphs only) | - | SIL Open Font License 1.1 | https://github.com/JulietaUla/Montserrat |

## LGPL-2.1 (Arduino-ESP32 core)

The core is statically linked into the firmware image. The complete source of this firmware is
public in this repository, and `firmware/platformio.ini` pins the exact toolchain and library
versions, so anyone can rebuild the image with a modified core and flash it (see `firmware/README.md`).

## SIL Open Font License 1.1 (Roboto, Montserrat)

The UI text is drawn with bitmaps generated from Roboto Medium (`firmware/tools/gen_fonts.py`, output in
`firmware/src/fonts/`). Copyright 2011 The Roboto Project Authors. The font and the complete license text
are kept in `firmware/assets/fonts/` (`Roboto-Medium.ttf`, `OFL-Roboto.txt`). The OFL allows embedding in
software; the font files themselves are not sold on their own, and no Reserved Font Name is used for the
derived bitmaps. Montserrat comes with LVGL under the same license.

## Not included

The stock Blocktrainer firmware, its web page and its images are not part of this project or its
releases.
