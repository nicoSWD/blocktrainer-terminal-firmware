# Host UI test harness

Compiles the LVGL UI of the firmware on the Mac (clang, no hardware), renders every screen to PNG
and runs a stress test under AddressSanitizer + UBSan. It is not part of the PlatformIO build:
PlatformIO only builds `src/`, nothing here is picked up by `pio run`.

## Run

```
cd firmware/host && make -j8 run
```

Needs clang/make/sips (Xcode command line tools) and the libraries PlatformIO already downloaded to
`firmware/.pio/libdeps/wt32-sc01-plus` (run `pio run` once if they are missing). Other targets:
`make render` (PNGs only, a few seconds), `make stress` (sanitizer run only, ~15 s), `make clean`.
`build/host_test stress <seed>` repeats the stress run with another random seed.
Images land in `out/` (ignored by git). The render step prints `WARN` lines for text that is
clipped by its parent or off-screen and for glyphs the built-in fonts lack.

## What is built

* LVGL 8.3 (all of `src` plus the QR code library) with `include/lv_conf.h` and the real
  `lv_alloc.h` heap hooks, ArduinoJson, and the firmware sources `ui_core.cpp`, `ui_pages.cpp`,
  `ui_menu.cpp`, `pages.cpp` compiled as they are (the seam below is the only host-specific edit;
  the layout and robustness fixes made with this harness are ordinary firmware changes).
* `shim/` replaces Arduino (String, Serial, millis), FreeRTOS, Preferences, WiFi, `esp_heap_caps`
  and the LovyanGFX panel. `host_stubs.cpp` replaces `net.cpp`, `hw.cpp`, config storage, OTA and
  Wi-Fi state.
* The only host-specific edit in `src/` is a `#ifdef UI_HOST_TEST` seam in `ui_core.cpp` that swaps the
  LovyanGFX `Display` class for a framebuffer stub (`shim/host_display.h`).
* `host_main.cpp` includes `ui_core.cpp` itself so it can reach its statics. It runs the real
  `ui_init()` (same flush callback, same nav zones, stream bar and Wi-Fi banner) and a copy of the
  `ui_task` loop body (`host_step`; keep it in sync when `ui_task` changes). Time is virtual:
  `millis()` is also LVGL's tick (`LV_TICK_CUSTOM`), so `lv_tick_inc` is not needed.

## What it covers

* Render: all 9 pages at 320 px and 290 px (stream bar), price in split screen and each
  currency, chart timeframes, every news item, stream bar variants, menu (settings and pages),
  Wi-Fi status screens and wizard (list, password), OTA screens, price alert overlay, Wi-Fi lost
  banner on several pages, empty and extreme data. Navigation, menu and wizard use injected touches.
* Stress (`-fno-sanitize-recover=all` on the firmware sources, so any finding aborts): 200 random page
  switches while the stream bar toggles, 200 direct `show_page` rebuilds, 100 menu open/close
  cycles with every widget poked, settings that rebuild pages, all status screens in random order,
  alert overlay, 300 rounds of random data (NaN, inf, huge, negative, empty, 3000 character
  strings, broken chart JSON) through every page's `update()`, random touches and drags.
* LeakSanitizer does not exist on macOS arm64. Instead the `heap_caps_*` shim counts live blocks:
  the stress run takes a baseline after a warm-up, runs three full rounds and requires the number
  of LVGL heap blocks to be unchanged (bytes within 256, label text sizes vary).
  `HOST_SELFTEST=1 build/host_test stress` must abort with an ASAN report (proves the sanitizers are on).
* `DataLock` is an error-checking mutex: nested locking aborts instead of deadlocking silently.

## What it does not cover

* Only the built-in Montserrat fonts (the firmware's `fonts/ui_fonts.h` is not used) and no real
  panel: colours are RGB565 converted to PNG, no gamma, inversion or viewing angle.
* No touch hardware (touches are injected into the LVGL pointer driver), no real FreeRTOS tasks,
  scheduling, stack sizes or heap pressure (PSRAM vs internal RAM), no Wi-Fi, HTTP or OTA.
* `net.cpp`, `hw.cpp`, `web.cpp`, `main.cpp` are not compiled; `net_update_chart_change` is a no-op
  and the sample data sets `chartChange` directly.
* The host is 64-bit: ArduinoJson slots are 32 bytes instead of 16, so `shim/ArduinoJson.h`
  doubles every `DynamicJsonDocument` capacity. `long` is 64-bit here but 32-bit on the ESP32.

## Findings from the first run

Bugs found with this harness and fixed in `src/` (details in the code comments):

* Chart page: NaN, inf or huge values in the chart JSON hit an undefined float cast (the only
  sanitizer hit). Price, sats, fee and difficulty-bar casts are clamped for the 32-bit `long`.
* Text that ran off or was clipped on the 480x320 screen: fees captions, stream bar title, SSID on the
  Wi-Fi status screens, OTA error text, password screen title, two menu text lines.
* The Wi-Fi lost banner covered the news author line (banner now 20 px, author line moved up).
* Converter: the caption of the active (orange) field was unreadable gray.

Polish made along the way: Latin-1 letters lose their accent in `ui_tr` instead of showing '?',
"°C" unit on the Bitaxe page, duplicate SSIDs hidden in the Wi-Fi list, equal-width side cards on the
chart page, "Kein Geraet" instead of "No device".

Known and left alone: absurd fee values (12345 sat/vB) overflow their column on the fees page
(reported as `info` for the `huge_*` images), and the converter's SPEICHERN button is partly
covered by the Wi-Fi lost banner while it is shown (touches still reach it).

## OTA update test

`make ota-test` compiles the real `src/ota.cpp` against stand-ins for `HTTPClient`, `WiFiClient` and
`Update` (`ota_test/shim/`, plain TCP sockets and a recorder) and runs it against a scripted local
server (`ota_test/server.py`) that behaves like the GitHub API and release CDN, and misbehaves on
purpose. Real wall-clock time, real threads for the update task, ASan and UBSan on. About 600 checks.

* **Release lookup** (`ota_check()`): a "latest release" document with a 300 KB body and noise fields
  (the JSON filter must skip them), 60+ assets, and the firmware asset picked by exact name among
  `-debug`/`-safe`/checksum decoys and near misses (`.sig`, prefix, wrong case, zero-padded version)
  in any order. Tags with or without `v`/`V`. A 301 (renamed repository), a body that ends at
  connection close, a slowly trickling body. The request must carry a `User-Agent` and the vendor
  `Accept` type, or the server answers 403 like GitHub does.
* **Nothing offered** (IDLE, no URL, not active, never an error screen): equal or older version, an
  asset for a different version, only decoys, no assets, null/mistyped `assets`, `tag_name` or entries,
  an asset without a URL, `{}`, `[]`, `null`, an empty or invalid body, a body cut off or stalled
  half-way, hang-up before the answer, a redirect loop, HTTP 403 (rate limit), 404 (no release yet),
  429, 500, 503, a server that never answers, a closed port, Wi-Fi down.
* **No stale state:** after any failed or empty lookup the offer from a previous successful lookup
  is gone (URL, version, `active`), and repeated lookups give the same answer.
* **Version compare:** numeric per component (0.10.0 > 0.9.9), missing or extra components,
  pre-release suffixes, leading zeros and spaces, negatives, garbage, plus every pair of an 11-version
  list checked against the expected order.
* **Download:** complete; a slow trickle (must not trip the stall detector); redirects (relative,
  absolute, two hops); a body without Content-Length; extra bytes after Content-Length (never read or
  written); a stall mid-body and right after the headers (aborts after the stall limit, never
  finalises); a connection closed early (rejected as incomplete); HTTP 404; a redirect loop; an empty
  body; a captive-portal HTML page served as 200 (rejected by the image magic byte); a server that
  never sends headers; an image bigger than an app slot; a flash write error; a failing final image
  validation; a second start request while a download runs (one flash session).
* After every failure the test checks that `Update.end()` was never reached and that the task ended.

The stall limit is 20 s on the device and 2 s here (`OTA_STALL_MS`), HTTP timeouts are scaled by
`HOST_TIMEOUT_SCALE`. Two deliberate mutations (a longer stall limit, no completeness check) make the
suite fail, so it does detect the faults it is meant to detect. Without the completeness check a download
cut off at 12000 of 40000 bytes is accepted and finalised, which is what the original loop would have done.

Not covered: TLS (the stand-ins use plain HTTP), the real Arduino `HTTPClient`/`WiFiClient` and lwIP
(their `connected()`/`available()` semantics are re-implemented here from the library's behaviour),
flash and partition handling, and a Wi-Fi drop in the middle of a transfer.
