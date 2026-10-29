# BT Clock / BT Terminal Custom Firmware

A clean-room custom firmware for the **BT Clock / BT Terminal** (ESP32-S3) by
[blocktrainer.de](https://btclock.blocktrainer.de), reverse-engineered from the stock image and
rewritten from scratch as ordinary embedded C++. It has the same pages, data sources, web
config page, OTA mechanism and NVS settings as the stock firmware, but none of its code — see
[`firmware/`](firmware/README.md). It installs the same way any update does: over the device's
own OTA, no flashing tools or USB access required.

> Educational / research use only. The stock firmware is copyright blocktrainer.de and is
> **not** included in this repo.

## The device

- **WT32-SC01 Plus** (ESP32-S3), 3.5" touch LCD
- ESP-IDF v4.4.7, Arduino (PlatformIO), LVGL 8 UI on a LovyanGFX panel driver
- Shows Bitcoin data (price, difficulty, fees, mempool) + hosts a web config page
- OTA update from `https://btclock.blocktrainer.de/v2/firmware/firmware.json`

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

## Build and install

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

Installing it is the same OTA interception described below: point `mitm_firmware.py` at the
built `firmware.bin` and let the device update itself.

## Deploy via OTA interception

The device checks `firmware.json` for a newer version, then downloads `firmware.bin`.
Both are HTTPS, so you need a MITM the device trusts (the device ships a custom CA).

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

`mitm_firmware.py` bumps `firmware.json`'s `version` (so the device sees a newer build) and
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
3. **USB download mode**, the ESP32-S3 ROM bootloader is un-brickable. Hold BOOT + reset,
   re-flash any firmware with `esptool.py write_flash`. Recovery is guaranteed unless secure
   boot is enabled with a lost key (this device has no secure boot) or the USB/BOOT pins are
   physically unreachable.

The custom firmware also has its own layers for when USB isn't an option in practice (it rarely
is, once a device is deployed): a boot watchdog, a crash counter that drops into a
display/sound/LED-free safe mode after repeated crashes, and an `/ota` and `/update` HTTP
endpoint that work without touching the screen. See `firmware/README.md`'s Reliability section.

## Repo layout

```
scripts/   fetch the stock firmware, unpack it; mitmproxy addon for OTA interception
ghidra/    Ghidra 12.1.4 + ghidra-mcp setup (headless, arm64 decompiler); functions.tsv export
firmware/  the custom firmware; firmware/host/ is an offline UI and OTA test harness
```

The downloaded/extracted stock firmware (`*.bin`, `*.elf`, `extracted/`) is gitignored, since it's
copyrighted and not ours to commit — see `.gitignore`. `firmware/` is our own code and is tracked
normally, aside from build artifacts and `firmware/include/secrets.h` (local Wi-Fi credentials,
see `firmware/README.md`).

`ghidra/functions.tsv` holds the function annotations (address, name, symbol source) made during
reverse-engineering: manually created functions and renames on top of auto-analysis. It contains
only addresses and names, no firmware bytes. The Ghidra project itself is not committed because it
embeds the firmware; `ghidra/apply_functions.java` and `ghidra/export_functions.java` reproduce or
re-export the annotations against a freshly imported project.

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

## Disclaimer

This is an independent reverse-engineering / interoperability project. It is not affiliated
with blocktrainer.de. Don't distribute the firmware or any derivative of it; don't use this
to bypass anything you're not authorized to modify.
