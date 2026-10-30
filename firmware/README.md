# Custom firmware for the BT Clock / BT Terminal

A clean, from-scratch firmware for the same device (WT32-SC01 Plus class board, ESP32-S3,
3.5" ST7796 touch panel). It shows the same nine pages, uses the same data sources, web
configuration page, OTA mechanism and NVS settings as the stock firmware, but is ordinary
embedded C++ with ordinary names.

Hardware pins and the decompile addresses they came from: [HARDWARE.md](HARDWARE.md).

## Build

```bash
cd firmware
.venv/bin/pio run          # standalone venv; or any PlatformIO install
```

Output: `.pio/build/wt32-sc01-plus/firmware.bin` (about 1.66 MB; the app slots are 3.3 MB).
It is built with the same toolchain as the stock image (ESP-IDF 4.4.7, `default_8MB.csv`
partitions), so the stock bootloader and partition table stay valid. A PlatformIO build is
already correctly checksummed and SHA256-terminated; no post-processing needed.

## Testing without the device

`host/` compiles the real UI sources against the real LVGL with stubbed hardware, renders every
page and status screen to `host/out/*.png`, and runs a randomised build/destroy stress test under
AddressSanitizer and UBSan: `cd firmware/host && make -j8 run` (about 30 s). It checks layout (text
clipped or off screen), widget lifetime and out-of-bounds access. It does not cover fonts beyond
the built-in ones, real touch or panel behaviour, FreeRTOS scheduling or heap pressure. See
`host/README.md`. `make ota-test` in the same folder runs the real OTA code against a scripted server that
stalls, trickles, closes early, redirects and fails (GitHub release lookup, version compare, download loop).

## Install over USB

With physical access to the board: `pip install esptool`, put the device in download mode (hold
BOOT, tap RESET, release BOOT), then write just the app image — the bootloader, partition table
and stored settings are left alone:

```bash
esptool.py --chip esp32s3 --port <PORT> write_flash 0x10000 .pio/build/wt32-sc01-plus/firmware.bin
```

For a blank board with nothing on it, write all three images the build produces:

```bash
esptool.py --chip esp32s3 --port <PORT> write_flash \
  0x0     .pio/build/wt32-sc01-plus/bootloader.bin \
  0x8000  .pio/build/wt32-sc01-plus/partitions.bin \
  0x10000 .pio/build/wt32-sc01-plus/firmware.bin
```

Same commands with a stock `firmware.bin` (from `scripts/fetch.sh`, repo root) restore stock, or
recover a device that's stuck. See the repo README's "Install over USB" for the full walkthrough.

## Install over OTA

Same flow as the repo README. The only difference is the image:

```bash
cp firmware/.pio/build/wt32-sc01-plus/firmware.bin patched_firmware.bin
mitmproxy --mode transparent --emudhra -s scripts/mitm_firmware.py     # from the repo root
```

The proxy is only needed once, to get off the stock firmware. `FAKE_VERSION` in
`scripts/mitm_firmware.py` just has to be above the stock version (1.8.2); it is unrelated to
`FIRMWARE_VERSION`, which is `0.1.0` here and is set from the git tag in the release workflow.

**Updates after that come from GitHub.** At boot (when "check for updates" is on) and on `/ota`, the
firmware asks `api.github.com/repos/nicoSWD/blocktrainer-terminal-firmware/releases/latest` and compares
the tag (`v0.1.1`, compared as `%d.%d.%d`) with its own `FIRMWARE_VERSION`. If it is newer, the
asset named `bt-terminal-firmware-v0.1.1.bin` is offered and installed through the normal prompt (the
`-debug` and `-safe` assets are never picked). Drafts and prereleases are ignored. The TLS connection
is not certificate-verified, the same as the stock updater was, so treat the channel as only as
trustworthy as your network. Going back to stock over OTA needs the proxy serving the stock
`firmware.bin` with a version above the one the stock firmware reports, or a USB flash.

**Before the first OTA install, make sure you can recover over USB** (above). The realistic
failure is a panel that initialises but stays blank.

## Reliability

Wi-Fi, OTA and the web server do not depend on the display or on the sound/LED drivers.

* **No USB needed for diagnosis:** `http://<ip>/diag` (always on, read-only) shows the version, reset
  reason, safe-mode state, whether the display and hardware drivers came up, heap and PSRAM,
  stack headroom of every task, the OTA state, and two logs: this run and the previous one. The
  logs and the boot milestone ("stage") live in RTC memory, so after a crash or watchdog reset the
  previous run's last stage and log lines are still there. The same page is served on the setup
  access point.
* **Boot watchdog:** a hang (a call that never returns) restarts the device after 90 s without
  progress and counts like a crash, so repeated hangs also end in safe mode.
* **Safe mode.** Three crashes in a row (panic, watchdog or brownout, counted in NVS namespace `btfw`)
  before the firmware has run stably for 5 minutes start the next boot without the display, sound and LED code.
  Networking, the web server and OTA still run. A new `FIRMWARE_VERSION` resets the counter;
  `http://<ip>/safe` shows the state and `/safe?exit=1` returns to normal mode.
* **Headless behaviour** (safe mode, or a panel that fails to initialise): no setup wizard. If the
  stored network does not connect, the access point `BTClock-Setup` serves a form at
  `http://192.168.4.1/`; while nobody uses it the stored network is retried every 30 s. An
  available update installs without a prompt and the device restarts into it.
* **Wi-Fi order:** once a network has been saved (from a previous run of the setup wizard), a network
  compiled in from `include/secrets.h` (gitignored; copy `secrets.example.h`) is tried first, then
  the saved credentials, alternating for six attempts (about 3 minutes) before the setup screen
  appears, so a router that boots slower than the device after a power cut is waited for. Before
  anything has been saved there is no known-good network worth that wait: the built-in network (if
  any) gets a single attempt, then the wizard appears. The built-in network is never written to NVS,
  so the stock credentials stay as they were. The password is in clear text inside `firmware.bin`: do not share
  images built with it.
* **The update prompt skips itself after 2 minutes** (a blank or unresponsive panel cannot hold the
  device), and the web server is already running while it is up. `curl http://<ip>/ota` reports
  the latest release version, `curl 'http://<ip>/ota?install=1'` installs a newer one without touching
  the screen. An on-screen Wi-Fi wizard that nobody answers for 5 minutes hands over to the
  setup access point.
* **OTA** has connect and read timeouts, aborts a stalled or short download instead of flashing it,
  and restarts into the new image 4 s after a successful install.
* **Direct upload:** in safe mode (or with the debug build or the `debug` setting on)
  `curl -F firmware=@firmware.bin http://<ip>/update` installs an image without any proxy. The
  route does not exist in normal operation because the page has no authentication.
* **Build environments:** `pio run` normal; `-e debug` serial logging; `-e safe` networking only.

## Layout

| file | role |
|---|---|
| `main.cpp` | `setup()`/`loop()`, boot order |
| `config.*` | settings, NVS load/save/reset (same keys and types as stock) |
| `net.*` | the six fetch tasks and the shared `LiveData` |
| `wifi_link.*` | credentials, reconnect task, watchdog (named `wifi_link` because `wifi.h` would shadow the framework's `WiFi.h` on a case-insensitive filesystem) |
| `wifi_portal.*` | setup access point and form for devices without a usable screen |
| `bootguard.*` | crash-loop counter and safe mode |
| `ota.*` | GitHub release check and download/flash task |
| `web.*`, `web_page.h`, `timezones.h` | configuration server |
| `pages.*` | page names, enable keys, `pageOrder` handling (no LVGL) |
| `ui_core.cpp` | panel, LVGL port, UI task, navigation, status screens, alert, stream bar, night mode |
| `ui_pages.cpp` | the nine page builders |
| `ui_menu.cpp` | configuration menu |
| `hw.*` | I2S buzzer and FastLED strip |
| `diag.*`, `bootguard.*` | RTC-memory log and breadcrumbs (`/diag`), crash and hang counter, safe mode |
| `host/` | host-side test harness: the UI code on the Mac against the real LVGL, rendered to PNG, under AddressSanitizer (`cd host && make -j8 run`) |

## Behaviour notes

* **Navigation:** tap the left or right quarter of a page for the previous or next page, the
  centre opens the menu. RECHNER is reachable from the menu's page overview only.
* **Menu:** currency, price alert, page rotation interval, night mode, livestream bar and chart
  timeframe, a brightness slider, a QR code for the web page, and a page overview (tap opens or
  enables a page, long press switches it off).
* **Night mode** (inferred, the stock behaviour was only partly readable): inside the configured
  window the display and LED go dark after `nightDelay` seconds without a touch; a touch wakes them.
* **Rotation:** the web page's "Rotation 180" stores 3, otherwise 1, and applies live.

## Known gaps

* **Fonts:** only LVGL's built-in ASCII fonts are used. German umlauts are transliterated (`AE`,
  `ue`) and the largest digits are 48 px (the stock UI uses up to 126 px). `tools/gen_fonts.sh`
  generates Latin-1 fonts and a 96 px digit font; the build picks them up automatically when
  `src/fonts/ui_fonts.h` exists. The script needs node and network access and has not been run.
* **Not tested on hardware.** Page layouts, the menu, LED timing and the night-mode behaviour
  have only been compiled, not seen on the device.
* The LED effect functions (pulse, solid, fade) follow the stock state machine, but the exact
  colours the stock firmware uses on each event were only recovered for the BLOCK, PREIS and
  DIFFICULTY pages.
* The stock images (logo, block icon) are not reproduced.
