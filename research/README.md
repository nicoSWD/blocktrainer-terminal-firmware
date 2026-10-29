# Firmware reconstruction (application layer) — archived research record

Fidelity-first reconstructed C++ for the BT Terminal / BT Clock firmware (ESP32-S3, 480x320
display), checked against the Ghidra decompile of `extracted/firmware.elf`. This is the port
`firmware/` was built from; it stays here frozen as the research record and is **not built or
maintained** (no `.venv`/`.pio` are kept — reinstall PlatformIO if you want to build it standalone).
For the working, maintained firmware, see `../firmware/`.

```bash
python3 -m venv .venv && .venv/bin/pip install platformio
.venv/bin/pio run -e wt32-sc01-plus      # compiles and links, if you want to build this snapshot
```

## What this firmware is

* Arduino-ESP32 on ESP-IDF 4.4.7, tasks pinned to two cores.
* **UI is LVGL 8** (custom `lv_font_fmt_txt` fonts, `LV_SIZE_CONTENT` = `0x27d1`, flex layouts).
  **LovyanGFX only provides the panel driver** (`Bus_Parallel8`) and the touch controller.
  The old claim "LovyanGFX draws the pages" in earlier notes, and the `lgfx_*` names in
  `extracted/decompiled/`, were wrong: those functions are LVGL (`lgfx_setTextColor` is
  `lv_obj_create`, `lgfx_fillRect` is `lv_obj_set_size`, ...).
* Settings: `Preferences` namespace `BTCLOCK` (see `src/config.cpp`).
* Six fetch tasks (`FetchPrice`, `FetchChart`, `FetchMempool`, `BitaxeStatus`, `StreamStatus`,
  `FetchNews`) that the firmware starts, resumes or suspends depending on which pages are enabled.
* `WebServer` on port 80: `/`, `/status.json`, `/save`, `/reset`, `/restart`.
* OTA from `https://btclock.blocktrainer.de/v2/firmware/firmware.json`.

## Layout

```
platformio.ini        env wt32-sc01-plus; LovyanGFX, ArduinoJson 6.21.6, LVGL 8.3
include/lv_conf.h     minimal LVGL config (16-bit colour, Montserrat stand-in fonts)
src/main.cpp          setup()/loop()
src/config.*          Settings (NVS), live data struct, shared mutexes
src/data.*            task table + the six fetch tasks
src/wifi_mgr.*        credentials, WiFiReconnect task, event handler, Watchdog
src/ota.*             manifest check, download, flash
src/web.*             web server routes
src/display.*         panel + LVGL glue, UI coordinator task, ported pages, system screens
src/hw.*              click waveform; LED/buzzer I/O stubs
src/assets.h          fonts/images that are binary data in the firmware (stand-ins here)
src/index_html.h      rebuilt equivalent of the embedded web page (original fragments: ../extracted/web/)
ui_translated.txt     mechanical LVGL translation of every decompiled page builder (reference, not built)
STATUS.md             per-function table with fidelity
MODULES.md            app/library boundary and address map
```

## Honest limits (as of this snapshot)

* About a quarter of the app-range functions are reconstructed (see `STATUS.md`).
* Pages ported by hand: BLOCK, UHR, FEES, OTA and Wi-Fi status screens. PREIS, CHART, BITAXE,
  RECHNER, NEWS, DIFFICULTY and the config menu show a placeholder label; their LVGL translation
  is in `ui_translated.txt`.
* Fonts and images are not recovered. `assets.h` maps the 9 custom fonts to the nearest built-in
  Montserrat size and leaves images null.
* Spots marked `UNVERIFIED` in the code are guesses; everything else follows the decompile.
* Nothing here has been run on hardware.

**Superseded by `firmware/`:** the two items the original snapshot above called unverified or
missing are now confirmed from the decompile and corrected — panel/touch pins (the public
WT32-SC01 Plus guess here turned out right, but is now read from the image: `4200af00`) and the
LED strip driver, I2S pins and buzzer alert pattern (`42032cac`, `42033e90`). See
`../firmware/HARDWARE.md` for the exact addresses and values, and `../firmware/src/` for the
code that uses them.
