# Firmware module map: app code vs library code

The 2.6 MB app image (ESP32-S3, Xtensa) is mostly libraries. Only the product layer is
reconstructed; the rest comes from the registry.

## Library code (not reconstructed)

| library | where it shows up | source |
|---|---|---|
| **LVGL 8** | `0x4203d5xx` .. `0x4206xxxx`, `0x42113xxx..0x42114xxx` (object, style, label, bar, flex) | github.com/lvgl/lvgl (8.3) |
| LovyanGFX | `Bus_Parallel8`, panel + touch drivers (the only part of the stack that is LovyanGFX) | github.com/lovyan03/LovyanGFX |
| ESP-IDF v4.4.7 | Wi-Fi, esp_lcd, NVS, TLS, FreeRTOS | github.com/espressif/esp-idf |
| Arduino ESP32 core | `HTTPClient`, `WebServer`, `Preferences`, `Update`, I2S, ledc | github.com/espressif/arduino-esp32 |
| ArduinoJson 6.21.6 | `0x4200c...`..`0x4201d...` templates (`Latch.hpp`, `parseNumber.hpp`, ...) | github.com/bblanchon/ArduinoJson |
| lwIP + mbedTLS | network, TLS | bundled in ESP-IDF |

## App code

| module | address range | notes |
|---|---|---|
| setup / loop / tasks glue | `0x4200b2xx`..`0x4200c9xx` | `setup`=`4200bcf0`, `loop`=`4200c8d4`, UI task=`4200c404` |
| config, defaults, time strings | `0x42008a84`, `0x4200a598`, `0x4200bb40` | NVS namespace `BTCLOCK` |
| fetch tasks | `0x420113d0`..`0x42023190` | table at DRAM `0x3fc95718` |
| OTA | `0x42020330`..`0x42022xxx` | check `4202233c`, perform `42020430` |
| Wi-Fi manager | `0x42024424`..`0x42024b18` | reconnect `42024450`, watchdog `420242cc` |
| pages | `0x42025068`..`0x4202e9cc` | table at rodata `0x3c21ae88`, 9 rows of 0x20 bytes |
| Wi-Fi/OTA screens | `0x42030970`..`0x42031d88` | |
| buzzer / LED | `0x42032xxx`..`0x42034xxx` | |
| web server | `0x42035400`..`0x42039ee8` | routes in `42039ee8` |

## Page table (rodata `0x3c21ae88`)

Each 0x20-byte row is `{ index, menu_only, name*, pref_key*, key_code, available(), build(), destroy() }`.

| idx | name | pref key | build | destroy | update (from UI task) |
|---|---|---|---|---|---|
| 0 | PREIS | pg_price | `4202c430` | `4202c348` | `4202c504` |
| 1 | CHART | pg_chart | `42027d54` | `42027d1c` | `42027d40` |
| 2 | BLOCK | pg_block | `42026440` | `420262f8` | `42026480` |
| 3 | UHR | pg_clock | `42027fe8` | `42027da8` | `42028024` |
| 4 | BITAXE | pg_bitaxe | `420262a4` | `42025748` | `42025b38` |
| 5 | FEES | pg_fees | `4202a798` | `4202a318` | `4202a6b4` |
| 6 | RECHNER (menu only) | pg_conv | `42029134` | `42028c7c` | `42028ca0` |
| 7 | NEWS | pg_news | `4202b36c` | `4202b398` | `4202b3bc` |
| 8 | DIFFICULTY | pg_diff | `4202982c` | `42029860` | `42029884` |

Note on reading the table: the two function pointers of a row sit in the *last 8 bytes of the
previous 0x20-byte block*, so a naive dump of 0x20-byte rows shows each pair one row too late.
`4202982c` is the DIFFICULTY wrapper that calls the big builder `4202923c`
(the one with "DIFFICULTY-ANPASSUNG", "FORTSCHRITT", "bis Anpassung"). Earlier notes called
`4202923c` `draw_price_page`; it is the difficulty page.

## Data flow

```
Fetch* tasks --HTTPS--> api.blocktrainer.de (price, mempool, chart, news, youtube)
     |                       Bitaxe miners: http://<ip>/api/system/info
     v
 live data (DRAM 0x3fc9b8f4.., guarded by mutex 0x3fc9bb50), dataVersion++ (0x3fc9bb48)
     |
UI task (every 1 s): if dataVersion changed -> update the visible page, check price alert
```

## LVGL helper map (confirmed from call shapes and LVGL 8 constants)

| firmware symbol / address | LVGL |
|---|---|
| `lgfx_setTextColor` `4203d548` | `lv_obj_create` (class `0x3c229740`) |
| `lgfx_setFont` `4203d55c` | `lv_obj_clear_flag` |
| `lgfx_fillRect` `4203e81c` | `lv_obj_set_size` |
| `lgfx_fillScreen(o,0,0xfffff)` `42043f84` | `lv_obj_remove_style_all` |
| `lgfx_drawString` `4205e810` | `lv_label_set_text` |
| `4205de1c` | `lv_label_create` |
| `4203e8f8` | `lv_obj_align` |
| `4203dc60` | `lv_obj_is_valid` |
| `4204452c` / `42044544` / `420443b0` / `420443c8` | `lv_obj_set_style_text_color` / `text_font` / `bg_color` / `bg_opa` |
| `0x27d1` | `LV_SIZE_CONTENT` |
| `0x2064` | `LV_PCT(100)` |
