# Plan: clean custom firmware (new `firmware/` project)

## Context

`reconstructed/` is a fidelity-first port: it mirrors the decompile's function names,
addresses in comments, `UNVERIFIED` markers, and leaves 6 of 9 pages as placeholders.
It stays as-is, it's the research record.

The user now wants a second, separate deliverable: a clean-room custom firmware that
behaves like the original device (same pages, same data, same OTA/web config, same
feel) but is written as normal, well-structured embedded C++ with sensible names, not
a decompile transcript. It has to build and be installable on the real device, which
the user will do over OTA using the toolkit already documented in this repo's
`README.md` (`scripts/patch.py` recomputes the image checksum/SHA256; the existing
MITM flow serves the replacement image; this plan does not need to re-derive that,
it's already written down).

Scope decisions (from the user):
- Hardware pins (panel, touch, I2S, LED) must be read from the real decompile before
  building, not left as the public WT32-SC01 Plus guess currently in `reconstructed/`.
- Full feature parity: all 9 pages, config menu, stream bar, LED strip, buzzer, night
  mode, pageOrder, price alerts.
- New project lives in a new top-level dir (`firmware/`), builds with PlatformIO, and
  produces an app image installable the same way the repo already installs images
  (OTA swap via the documented process; `scripts/patch.py`'s checksum/SHA256 step
  applies to any image, including this one, if it's ever written via `esptool` too).

## What's reused vs. rewritten

Reused as reference, not copied verbatim:
- `reconstructed/STATUS.md` / `MODULES.md` for the function map, page table and task
  table (addresses, enable predicates, task table at `0x3fc95718`, page table at
  `0x3c21ae88`).
- `reconstructed/src/data.cpp` fetch logic and verified JSON schemas (price, mempool,
  chart, news, youtube) -- behavior is correct, gets renamed and restructured.
- `reconstructed/src/ota.cpp`, `web.cpp`, `wifi_mgr.cpp`, `config.h` -- same behavior,
  same NVS keys (must keep key names identical or old settings are silently dropped),
  cleaned up.
- `reconstructed/ui_translated.txt` -- mechanical LVGL translation of the 6 unported
  pages + config menu. This is the starting point for writing PREIS, CHART, BITAXE,
  RECHNER, NEWS, DIFFICULTY and the config menu as real code; the custom firmware does
  not have to match pixel layout, just the same information per page.
- `reconstructed/src/hw.cpp`/`assets.h` buzzer waveform (verified) is reused; LED and
  I2S pins get filled in once confirmed from the decompile (see below), the design
  does not have to match so a straightforward pin config is fine once we know which
  GPIOs are live on this board.

Rewritten from scratch, clean style:
- All file/function/variable names (`config.h/.cpp`, `net.h/.cpp` replaces
  `data.cpp`, `ui.h/.cpp` replaces `display.cpp`, `wifi.h/.cpp`, `ota.h/.cpp`,
  `web.h/.cpp`, `hw.h/.cpp`, `main.cpp`), normal comments (no `FUN_xxxxxxxx` /
  address breadcrumbs, no `UNVERIFIED` scaffolding language).
- Page builders as straightforward LVGL 8 widget trees per page (own layout, not a
  literal translation of the original screen).
- Assets: since pixel-identical fonts/images are out of scope ("design does not have
  to match"), use LVGL's built-in fonts at matched line-heights and simple LVGL
  drawing (lines/bars) instead of the embedded image assets. No asset extraction
  needed for this track.

## Step 1: confirm hardware pins from the decompile

Before writing `ui.cpp`/`hw.cpp`, reconnect to GhidraMCP (headless 8089 + bridge 8081
on the Kali box, restart via ssh if needed per `memory/ghidra-kali-setup.md`) and pull:
- Panel/bus/touch config from `4200b9c8` (LCD init): bus pins, panel geometry/offset,
  touch controller address/pins, backlight pin+channel.
- I2S pin assignment from `42032cac`.
- LED strip pin + protocol from `42033e90`/`4203408c`/`42034150`.

These go into `firmware/src/hw.cpp` as named constants, replacing the "UNVERIFIED
public board config" currently in `reconstructed/src/display.cpp`.

## Step 2: new project layout

```
firmware/
  platformio.ini        -- same target/board as reconstructed/platformio.ini
  include/lv_conf.h      -- copied, trimmed comments
  src/
    main.cpp             -- setup()/loop(), task spawn, same order as original
    config.h / config.cpp        -- Settings struct, NVS load/save/reset (same keys)
    net.h / net.cpp               -- the 6 fetch tasks + shared LiveData (was data.cpp)
    wifi.h / wifi.cpp             -- STA connect, reconnect task, watchdog
    ota.h / ota.cpp               -- manifest check, download+flash, stage machine
    web.h / web.cpp / web_page.h  -- routes, /save, /status.json, HTML (was index_html.h)
    ui.h / ui.cpp                 -- LVGL init, page table, all 9 page builders, config
                                     menu, stream bar, alert overlay, OTA/Wi-Fi screens
    hw.h / hw.cpp                 -- buzzer, LED, confirmed pins from Step 1
```

## Step 3: fill in the 6 unported pages + config menu

Using `ui_translated.txt` as the source of truth for *what each page shows* (labels,
fields, update cadence), write real LVGL widget code for:
- PREIS (main price page), CHART, BITAXE, RECHNER (unit converter), NEWS, DIFFICULTY
- Config menu (currency, timezone, brightness, night mode, LED, buzzer, alerts,
  page order toggle list)
- Stream bar (YouTube live/scheduled indicator)

Plus finishing the stubs already flagged in `reconstructed/STATUS.md`'s "known gaps":
apply `pageOrder`, wire up night mode, wire up the LED animation state.

## Step 4: build and verify

- `pio run` in `firmware/`, fix compile errors.
- Compare output size against the partition table (`app0`/`app1` are 0x330000 each,
  confirmed in the earlier build/compare pass) to make sure the image fits.
- Run `scripts/patch.py`-style checksum/SHA256 recompute only if any post-build byte
  patch is applied; a clean PlatformIO build already produces a correctly checksummed
  image, so this is only needed if hand-editing the binary afterward.
- No hardware-in-the-loop test available in this session; final on-device check is the
  user's job after they push it out over the existing OTA installation path described
  in `README.md`.

## Files this touches

New: everything under `firmware/`.
Unchanged: `reconstructed/*` (left as the research artifact), root `README.md`,
`scripts/*`.
