# First flash checklist (OTA only, no USB)

The only way onto the device is the OTA flow with the MITM proxy (`README.md`, section 7). There
is no serial log and no `esptool`, so a bad image cannot be repaired from the PC. This plan is
built around that:

* The stock bootloader has **no rollback** (no rollback code in `bootloader.bin`). An image that
  never reaches the network stays on the device. So the first image is the smallest one that
  cannot hang on hardware, and the full UI comes only after networking is proven.
* Observability comes over Wi-Fi: `http://<ip>/diag` (breadcrumbs, previous-run log, heap and stack
  numbers, reset reason). The device IP is in your router's client list.
* Three safety nets are built in: image validation by the bootloader (a corrupted transfer falls
  back to the stock slot), a boot watchdog (a hang restarts the device and counts as a crash) and
  safe mode (3 crashes in a row: no display, sound or LED code, networking and OTA only).

## Version rule: every stage is a higher version than the last

The device only installs a manifest version above its own. The stock image is 1.8.2. For each
stage, build with a version and start the proxy with the same one:

```bash
# stage A, build
cd firmware && PLATFORMIO_BUILD_FLAGS='-DFIRMWARE_VERSION=\"1.8.3\"' .venv/bin/pio run -e safe
# proxy: FAKE_VERSION = "1.8.3" in scripts/mitm_firmware.py, patched_firmware.bin = that image
```

Stage B uses 1.8.4 or higher, a later fix the next number, and so on. A new version also resets
the crash counter.

## Before you start

- [ ] The proxy works against the **stock** firmware: have you seen the device take a proxied image
      before? If not, test the proxy first by re-serving the stock `firmware.bin` as 1.8.3. It
      changes nothing but proves the path.
- [ ] Wi-Fi: the device tries `blocktrainer-lab` (built in, `include/secrets.h`) first, then its
      stored network. The lab network must be in range of the device, and the proxy machine must be
      reachable from it.
- [ ] You know the way back to stock: serve the stock `firmware.bin` with a version above the one
      installed (for example 1.8.9) and let the device update.

## Stage A: networking only (`safe` image)

No display, sound or LED code runs, so this stage proves Wi-Fi and OTA by themselves.

1. Build the `safe` image as 1.8.3 (command above) and serve it through the proxy.
2. Reboot the device so it checks for updates. The stock firmware shows its update prompt;
   accept it. After the install the device restarts.
3. Find the device in the router's client list (an Espressif MAC), then:
   - [ ] `http://<ip>/safe` returns `{"safe_mode":true,...,"version":"1.8.3"}`
   - [ ] `http://<ip>/diag` shows `safe mode: YES`, `wifi: blocktrainer-lab ...`, `stage now: ready`
         and a heap line with tens of kilobytes of internal RAM free
   - [ ] the screen stays dark (expected: nothing draws in safe mode)
4. **Prove OTA from the new image.** Build the same image as 1.8.4, set the proxy to 1.8.4, then
   `curl http://<ip>/ota` (reports current and remote version) and
   `curl 'http://<ip>/ota?install=1'`. The device restarts into 1.8.4 by itself after about 4 s
   (`/diag` shows `firmware 1.8.4`).
5. **Prove the upload path:** `curl -F firmware=@firmware.bin http://<ip>/update` with a 1.8.5
   build answers `OK, restarting`.

If the device does not show up on the network within 5 minutes of the reboot:

- Look for the access point `BTClock-Setup` on a phone: the image runs but cannot join the lab
  network. Open `http://192.168.4.1/`, enter the network, and `http://192.168.4.1/diag` shows why.
- Neither network nor access point: the image does not run. This plan cannot fix that from the PC,
  which is why Stage A is the smallest image possible.

## Stage B: the full image

1. Build the `debug` image (full UI, logging on, `/update` open) with the next version and install
   it over OTA from the running safe image (`/ota?install=1`).
2. Check `http://<ip>/diag` after a minute:
   - `display ready: yes`, `hardware drivers: yes`, `stage now: ready`
   - heap: internal free stays above about 40000, the minimum ever above about 20000
   - stacks: every task has at least 600 bytes never used
3. **If it keeps restarting:** after 3 crashes the device comes back in safe mode by itself (about
   2 minutes). `/diag` then shows `stage when the previous run ended:` and the log of the previous
   run, which says where it died. Fix, build the next version, install over OTA.
4. **If the screen stays dark but `/diag` looks healthy:** pins or panel settings. Compare with
   `HARDWARE.md`; the likely knobs are `invert`, `rgb_order` and the bus speed in `Display` in
   `ui_core.cpp`.
5. Work through the functional pass below.

| `stage when the previous run ended` | Meaning |
|---|---|
| `ui_init` | the display initialisation crashed or hung (pins, panel config, DMA memory) |
| `hw_init` | the I2S or FastLED setup crashed or hung |
| `wifi`, `wifi connected` | Wi-Fi bring-up problem |
| `web server up`, `ota check` | OTA manifest or prompt handling |
| `first page`, `page <NAME>` | a page's builder or update crashed (the name says which) |
| `ready` | it ran fine, the crash came later (read the log of the previous run) |

## Functional pass (Stage B)

On the device:

- [ ] Backlight on and an image drawn; touch lands where you tap; the click sound plays.
- [ ] Left quarter = previous page, right quarter = next page, centre opens the menu.
- [ ] Pages show data: PREIS (change % non-zero within a few seconds), CHART, BLOCK, UHR (right
      local time), FEES, DIFFICULTY, NEWS; BITAXE only with a miner IP set.
- [ ] LED breathes orange at boot, then follows the page (orange BLOCK, green/red PREIS and
      DIFFICULTY, off on UHR). A wrong colour means strip type or order.
- [ ] Menu: QR code and IP, currency, switches, steppers, brightness slider, page overview,
      RECHNER and its back arrow.
- [ ] A reboot keeps the settings changed in the menu.

From a computer:

- [ ] `http://<ip>/` loads, currency and "Rotation 180" show the stored values; saving applies at once.
- [ ] "Rotation 180" flips the screen and touch still lands correctly.
- [ ] `http://<ip>/status.json` returns price, block, fees, difficulty, clock.
- [ ] Page order list: drag, save, the rotation follows.
- [ ] A price alert just below the current price shows "PREISZIEL ERREICHT" and beeps once.
- [ ] Night mode with a window around "now" and a short delay: the display goes dark, a touch wakes it.

## Symptoms and where to look

| Symptom | Likely cause | Where to look |
|---|---|---|
| Black screen, `/diag` says `display ready: NO` | panel or bus init failed | `HARDWARE.md`, `Display` class in `ui_core.cpp` |
| Backlight on, nothing drawn | bus speed, `memory_*`/`panel_*` | try `freq_write` 40 MHz (the public config uses it) |
| Colours inverted | `invert` flag | `c.invert` |
| Red and blue swapped | `rgb_order` | `c.rgb_order` |
| Scrambled colours | pixel byte order in the LVGL flush | `flush_cb` in `ui_core.cpp` |
| Image upside down, touch mirrored | rotation 1 vs 3 | web "Rotation 180" |
| Touch dead or offset | I2C port or `bus_shared` | `HARDWARE.md` touch section (the public config uses port 1, stock uses 0) |
| LED wrong colours | strip type or order | web LED strip type; `led_begin()` in `hw.cpp` |
| Update prompt on every boot | manifest version above `FIRMWARE_VERSION` | raise the build version |
| Stuck in safe mode | 3 crashes were counted | `/diag`, then a new version or `/safe?exit=1` |
| Not on the network at all | image does not run, or lab network out of range | access point `BTClock-Setup`; see Stage A |

## Going back to stock

Serve the stock `firmware.bin` through the proxy with a version above the installed one (for
example `FAKE_VERSION = "1.8.9"`) and update as usual. No USB needed, as long as the device runs.

## Once it works

Build the normal image (`pio run`, no debug logging) with a higher version and install it. The
debug and safe images expose the unauthenticated `/update` route and should not stay installed.
Keep a copy of `/diag` and photos of each page; they show what to adjust.
