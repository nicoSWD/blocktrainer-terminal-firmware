# Blocktrainer Terminal Custom Firmware

A custom firmware for the **BT Clock / BT Terminal** (ESP32-S3) by
[blocktrainer.de](https://btclock.blocktrainer.de), reimplemented as ordinary embedded C++ from
an analysis of the stock image (behaviour, settings keys, pin map), without copying its code. It has the same pages, data sources, web
config page, OTA mechanism and NVS settings as the stock firmware, but none of its code — see
[`firmware/`](firmware/README.md). It installs two ways: over the device's own OTA update, no
flashing tools needed, or over USB with `esptool` if you have physical access to the board.

> Educational / research use only. The stock firmware is copyright blocktrainer.de and is
> **not** included in this repo.

## The device

- **WT32-SC01 Plus** (ESP32-S3), 3.5" touch LCD
- ESP-IDF v4.4.7, Arduino (PlatformIO), LVGL 8 UI on a LovyanGFX panel driver
- Shows Bitcoin data (price, difficulty, fees, mempool) + hosts a web config page
- Stock OTA update from `https://btclock.blocktrainer.de/v2/firmware/firmware.json`; this custom
  firmware updates itself from [GitHub releases](https://github.com/nicoSWD/blocktrainer-terminal-firmware/releases) instead

### Hardware

| part | detail | source |
|---|---|---|
| MCU | ESP32-S3 (dual Xtensa LX7), 8 MB PSRAM | firmware (`chipFamily`, `psramInit`) |
| Flash | 8 MB | partition table spans `0x0–0x800000` |
| Display | 3.5" IPS, **ST7796**, 320×480 panel used as 480×320 (rotation 1 or 3), 8-bit parallel | LovyanGFX `Bus_Parallel8`; the ST7796 init table is in the image (`3c1b164c`) |
| Touch | FT6336U-class capacitive (FT5x06 protocol), I2C port 0, address `0x38`, SDA 6 / SCL 5 / INT 7 | LovyanGFX touch config in the LGFX constructor (`4200af00`) |
| WiFi | ESP32-S3 built-in 2.4 GHz 802.11b/g/n | WiFi stack |
| Buzzer | I2S speaker amp, 22.05 kHz 16-bit stereo, BCK 36 / WS 35 / DOUT 37 | `buzzer_click`, `buzzer_alert`, I2S setup `42032cac` |
| LEDs | 8 addressable LEDs (WS2812 or WS2811, `ledStripType`), data pin 10, FastLED | `42033e90` |

The panel pins, touch, I2S and LED details above were read from the decompile
(see `firmware/HARDWARE.md` for the exact addresses and values); the panel and touch
chip names are identified from their init data and I2C address, not from strings.
No RTC chip, sensors, or SD card — time is NTP, config is NVS.

## Build

The firmware itself, how to build it, the three build environments (normal, `debug` with serial
logging forced on, `safe` networking-only for first bring-up), and the offline UI/OTA test
harnesses are documented in [`firmware/README.md`](firmware/README.md). Short version:

```bash
cd firmware
.venv/bin/pio run      # or: python3 -m venv .venv && .venv/bin/pip install platformio, first time
```

Output is `firmware/.pio/build/wt32-sc01-plus/firmware.bin`, already built with the same
toolchain, partition table and image format as the stock firmware (ESP-IDF 4.4.7,
`default_8MB.csv`), so it installs and boots exactly like a stock update — no byte-patching,
checksum recomputation or other post-processing needed.

Install it either over USB (below) or over the device's own OTA update (further down).

## Install over USB

Needs physical access to the board and a USB cable. Install `esptool` once:

```bash
pip install esptool
```

1. Connect the device and put it in download mode: hold the **BOOT** button, tap **RESET**, then
   release **BOOT**. It enumerates as a new serial port; note which one.
2. Optional, confirms the connection before writing anything:
   ```bash
   esptool.py --chip esp32s3 --port <PORT> chip_id
   ```
3. Write the firmware. A device that's already running the stock firmware (or an earlier build of
   this one) only needs the app image rewritten — the bootloader, partition table and stored
   settings (Wi-Fi credentials, preferences) are left as they are:
   ```bash
   esptool.py --chip esp32s3 --port <PORT> write_flash 0x10000 firmware/.pio/build/wt32-sc01-plus/firmware.bin
   ```
   A blank board with nothing on it yet needs all three images the build produces, written at
   their respective offsets:
   ```bash
   esptool.py --chip esp32s3 --port <PORT> write_flash \
     0x0     firmware/.pio/build/wt32-sc01-plus/bootloader.bin \
     0x8000  firmware/.pio/build/wt32-sc01-plus/partitions.bin \
     0x10000 firmware/.pio/build/wt32-sc01-plus/firmware.bin
   ```
4. The device resets and boots the new image on its own once the write finishes.

This is also how you go back to stock, or recover a device: run the same commands with the stock
`firmware.bin` (from `scripts/fetch.sh`) in place of the custom one.

## Install over OTA

This is the one-time step that gets the custom firmware onto a device that still runs the stock
image. The stock firmware checks `firmware.json` for a newer version, then downloads `firmware.bin`.
Both are HTTPS, so you need a MITM the device trusts (the device ships a custom CA).

Once the custom firmware is running it no longer talks to blocktrainer.de for updates: it asks the
GitHub API for the latest release of this project at boot (and on `/ota`), and installs the
`bt-terminal-firmware-vX.Y.Z.bin` asset when the tag is newer than its own version. Later updates
need no proxy.

### How the interception works

Transparent proxy. The device never talks to mitmproxy directly, its traffic is
redirected at the kernel level:

```
device ──TLS(mitmproxy cert)──▶ mitmproxy ──TLS(real cert)──▶ btclock.blocktrainer.de
```

1. Device resolves `btclock.blocktrainer.de` (via dnsmasq) and opens TCP to :443.
2. An iptables rule silently redirects that connection to mitmproxy on :8080.
3. mitmproxy (`--mode transparent`) terminates TLS with its own CA cert, then opens its own
   TLS connection to the real server. Two tunnels, plaintext in the middle.

The intercept box (Kali, the AP/gateway) needs:

```bash
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
sudo iptables -t nat -A PREROUTING -i wlan0 -p tcp --dport 80 -j REDIRECT --to-port 8080
sudo iptables -t nat -A PREROUTING -i wlan0 -p tcp --dport 443 -j REDIRECT --to-port 8080
```

These are ephemeral, re-apply after every reboot.

### Run mitmproxy

```bash
cd ~ && mitmproxy --mode transparent --emudhra -s scripts/mitm_firmware.py
```

`mitm_firmware.py` bumps `firmware.json`'s `version` (so the stock firmware sees a newer build) and
serves our own `firmware.bin` (`PATCHED_FIRMWARE` in the script; the name is historical, it now
points at the custom firmware build) when `firmware.bin` is requested. The updater uses Arduino
`Update` (no signature check, magic + CRC only), so it accepts the image.

**Both intercepts are required**, without the version bump the device logs
"Bereits auf neuester Version" and never requests `firmware.bin`.

Going back to stock, or recovering a device, works the same way: re-serve the real
`firmware.bin` (from `scripts/fetch.sh`) with a version above whatever's installed.

## Safety & recovery

The custom firmware shares the device's image format, partition layout and NVS key names, so it
installs and boots exactly like a stock update — same `Update.end()` validation, same OTA slots,
and it keeps whatever Wi-Fi credentials and settings were already stored. If something is ever
wrong with a build, three layers catch it:

1. **OTA flash-time verification**, `Update.end()` verifies the written image (magic + SHA256)
   before committing; a corrupt image is rejected and the old firmware stays put.
2. **Boot-time rollback**, rollback support is compiled in; a crashing new app can auto-roll
   back to the previous app after a few failed boots (bootloader-dependent).
3. **USB download mode**, the ESP32-S3 ROM bootloader is un-brickable: see "Install over USB"
   above. Recovery is guaranteed unless secure boot is enabled with a lost key (this device has
   no secure boot) or the USB/BOOT pins are physically unreachable.

The custom firmware also has its own layers for when USB isn't an option in practice (it rarely
is, once a device is deployed): a boot watchdog, a crash counter that drops into a
display/sound/LED-free safe mode after repeated crashes, and an `/ota` and `/update` HTTP
endpoint that work without touching the screen. See `firmware/README.md`'s Reliability section.

## Testing the custom firmware offline

The device has no USB access once deployed, so both the UI and the OTA download logic are tested
on the Mac instead, against the real code:

* `cd firmware/host && make -j8 run` — compiles the real LVGL and UI sources with stubbed
  hardware, renders every screen to a PNG (`firmware/host/out/`), and stress-tests widget
  build/destroy cycles under AddressSanitizer and UBSan.
* `cd firmware/host && make ota-test` — runs the real OTA download code against a scripted local
  server that stalls, trickles, closes early, redirects and fails, to check the download loop and
  manifest handling without needing a device or a real stall to happen to catch it.

What each harness covers and does not cover is in `firmware/host/README.md`.

## License

The code in this repository is released under the [MIT License](LICENSE). Third-party libraries
that end up in the firmware image (LVGL, ArduinoJson, FastLED, LovyanGFX, the Arduino-ESP32 core
and ESP-IDF) keep their own licenses, listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The MIT license covers this project's own code only. The name "Blocktrainer", the `blocktrainer.de`
servers and APIs the firmware talks to, and the stock firmware itself belong to blocktrainer.de and
are not covered by it. The web configuration page and the timezone labels are original to this
project; nothing from the stock image is shipped.

## Disclaimer

This is an independent reverse-engineering / interoperability project. It is not affiliated
with blocktrainer.de. Don't distribute the stock firmware image; don't use this to bypass
anything you're not authorized to modify. This is not legal advice.
