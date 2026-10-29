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

## LGPL-2.1 (Arduino-ESP32 core)

The core is statically linked into the firmware image. The complete source of this firmware is
public in this repository, and `firmware/platformio.ini` pins the exact toolchain and library
versions, so anyone can rebuild the image with a modified core and flash it (see `firmware/README.md`).

## Not included

The stock Blocktrainer firmware, its web page and its images are not part of this project or its
releases.
