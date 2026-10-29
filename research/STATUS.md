# Reconstruction status

Source of truth: `extracted/firmware.elf` in Ghidra 12.1.4 (Kali box), walked from
`loopTask -> setup()/loop()`, the 6 FreeRTOS fetch tasks, the page table at `0x3c21ae88`,
and the web route table. Addresses are the `FUN_xxxxxxxx` entry points.

Fidelity column: **V** = behaviour read from the decompile, **V/UI** = structure and constants read
from the decompile, assets stubbed, **U** = partly guessed (marked `UNVERIFIED` in the code),
**-** = not reconstructed.

## Numbers

| | count |
|---|---|
| functions in Ghidra after repair | 6641 (6396 originally; 245 call targets had never been created) |
| functions reachable from the roots above inside the app range `0x42002784..0x42037000` | 299 |
| of those, ArduinoJson / HTTPClient / WebServer library bodies | roughly 30 flagged, more are inlined |
| app functions with reconstructed behaviour (rows below marked V, V/UI, U) | about 70 |

So roughly a quarter of the app-range functions are reconstructed. The rest are the
non-ported pages, the config menu, the LED/buzzer drivers and library bodies.

## Core

| address | role | file | fidelity |
|---|---|---|---|
| `4207a3b4` | app_main -> loopTask | (Arduino core) | V |
| `4207a384` | loopTask: `setup()`, then `loop()` forever | main.cpp | V |
| `4200bcf0` | `setup()` | main.cpp | V (Wi-Fi/wizard UI calls abstracted) |
| `4200c8d4` | `loop()`: `delay(2)`, `server.handleClient()`, timing warnings | main.cpp | V |
| `4200a598` | compile-time defaults | config.cpp | V |
| `42008a84` | NVS key creation + load, `BTCLOCK` namespace | config.cpp | V |
| `4203733c` | `/reset`: clear the whole namespace | config.cpp | V |

## Network tasks (core 0, prio 1)

| address | role | file | fidelity |
|---|---|---|---|
| `4200b888` / `4200b7a8` / `4200b824` | task table walker, start/resume, suspend | data.cpp | V |
| `4200b27c` `4200b298` `4200b2ac` `4200b2c8` `4200b2e8` | per-task enable predicates | data.cpp | V |
| `4201fc6c` | FetchPrice | data.cpp | V (payload checked against the live API: `{"t":..,"p":{"USD":[price,chg%],...}}`) |
| `4201770c` | FetchMempool | data.cpp | V (checked against the live API) |
| `42014a70` | FetchChart (hourly + jitter + retry table) | data.cpp | U (hour alignment) |
| `4201b28c` | FetchNews | data.cpp | V (checked against the live API: root is an array) |
| `42023190` | StreamStatus (YouTube bar) | data.cpp | U (delay rules only partly confirmed) |
| `420113d0` | BitaxeStatus | data.cpp | U (value formatting) |
| `42035094` / `42034b20` / `42034e8c` | chart change % from stored chart JSON (`tf.<24h|5d>.p = [[ts,EUR,USD,CHF],...]`) | data.cpp | V (schema), U (percent formula) |

## Wi-Fi

| address | role | file | fidelity |
|---|---|---|---|
| `420249c0` / `42024900` | load/save `ssid`+`password` | wifi_mgr.cpp | V |
| `42024830` | Wi-Fi event handler | wifi_mgr.cpp | V |
| `42024450` | WiFiReconnect task (scan, strongest BSSID, 20 s wait) | wifi_mgr.cpp | V |
| `420242cc` | Watchdog task | wifi_mgr.cpp | V (thresholds), U (exact branch order) |
| `42030994` `42030ef8` `42031d88` | setup wizard screens, WiFiScanTask, keyboard | display.cpp | U (strings only) |
| `42030a78` `42030ba8` `42030f60` `42031090` | connecting / connected / failed / lost screens | display.cpp | V/UI |

## OTA

| address | role | file | fidelity |
|---|---|---|---|
| `4202233c` | check manifest (`version`, `builds[]`, `chipFamily == ESP32-S3`, `parts[].path` contains `firmware.bin`) | ota.cpp | V |
| `42020330` | `%d.%d.%d` compare | ota.cpp | V |
| `42020430` | download + `Update` (stages 3,4,5,6) | ota.cpp | V |
| `420209fc` | spawns `OTAUpdateTask` | ota.cpp | V |
| `42020a3c` | progress bars refresh | display.cpp | V/UI |
| `42031224` `42031500` `420316e0` | OTA screens | display.cpp | V/UI |

## Web server

| address | role | file | fidelity |
|---|---|---|---|
| `42039ee8` | route registration: `/`, `/status.json`, `/save`, `/reset`, `/restart` | web.cpp | V |
| `42035400` | `/status.json` (price, block, fees, difficulty, bitaxe[], news[], clock) | web.cpp | V (format), U (second `moscow` value) |
| `42035b84` | `/` template substitution, timezone `<option>` list (29 entries from rodata `0x3c2292ac`) | web.cpp, timezones.h | V (placeholder list), U (`data-key` values and `pageOrder` format of the sortable list); HTML body is a rebuilt equivalent |
| `420373f8` | `/save` form handler (field names listed in web.cpp) | web.cpp | V |
| `420372f8` | `/restart` | web.cpp | V |

## UI (LVGL 8)

| address | role | file | fidelity |
|---|---|---|---|
| `4200b9c8` | display + LVGL init, layers | display.cpp | U (panel pins are the public board config) |
| `4200c404` | "lvgl" coordinator task (page requests, 1 s refresh, auto page, stream bar, alert check) | display.cpp | V (structure), U (stream bar) |
| `4200c210` | show page `n` (wrap 0..8, destroy all, build one) | display.cpp | V |
| `4200b4b4` | touch read callback + click sound | display.cpp | V |
| `4200b61c` | price alert (above/below, 60 s lockout, 10 s arm delay) | display.cpp | V |
| `4202ce74` | alert overlay "PREISZIEL ERREICHT" | display.cpp | U |
| `4202631c` `42026480` | BLOCK page | display.cpp | V/UI |
| `42027dcc` `42028024` `4200bb40` | UHR page and time strings | display.cpp | V/UI |
| `4202a33c` `42029c48` `4202a14c` `4202a6b4` `42029abc` | FEES page | display.cpp | V/UI |
| `4202c430` PREIS, `42027d54` CHART, `420262a4` BITAXE, `4202b36c` NEWS, `42029134`/`42028ccc` RECHNER, `4202982c`/`4202923c` DIFFICULTY | page builders | display.cpp | - (placeholder label; full mechanical LVGL translation in `ui_translated.txt`) |
| `4202e9cc` | config menu (790 decompiled lines) | - | - (translation in `ui_translated.txt`) |

## Hardware

| address | role | file | fidelity |
|---|---|---|---|
| `42032d1c` | click waveform (300 Hz decaying sine, 22.05 kHz) | hw.cpp | V |
| `42032cac` | I2S setup | hw.cpp | - |
| `42033e90` `4203408c` `42034150` | LED strip driver + animation | hw.cpp | - |

## Things in the saved settings that no page uses in this build

`stackUnit`, `btcHoldingSats`, `pg_stack` are created and read at boot but the page table has
9 pages and none of them is a STACK page. The previous reconstruction had a `draw_stack_page`;
that page does not exist in this firmware.

## Known gaps in what is reconstructed

* `pageOrder` is stored and shown in the web form but not applied: `ui_default_page()` and `next_page()` ignore it
  (the originals are `FUN_4200b77c` and `FUN_4200c39c`).
* PREIS, CHART, BITAXE, RECHNER, NEWS, DIFFICULTY and the config menu are placeholders.
* The stream bar (`FUN_42032660`, `FUN_42032a0c`, `FUN_42032b48`) is not built; `s_statusbar` is an empty hidden object.
* Night mode (`nightActive`, `nightStart`, `nightStop`, `nightDelay`) is stored and editable but nothing consumes it yet.
* `StreamStatus` next-poll delay rules are only partly confirmed.
