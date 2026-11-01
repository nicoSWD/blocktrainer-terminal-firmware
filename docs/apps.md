# Installable apps

Status: plan only, nothing implemented.

## Context

Pages are a fixed compile-time set of 9 (`PageId` enum `firmware/src/config.h:35-45`, `PAGE_INFO`
`firmware/src/pages.cpp:3-7`, `PAGE_OPS` `firmware/src/ui_pages.cpp:1325-1334`). The goal is that users pick
which functions their device has and install them from an in-device store.

Decisions:

- Apps are written only by the repo owner and signed with the owner's key.
- Apps get full custom UI and logic: their own LVGL screens, touch, sound, LED, network.
- Storage keeps the stock partition table. A larger layout is an opt-in, USB-only migration.

## Recommendation

WASM apps run by wasm3, shipped as signed bundles, installed from a store that reuses the GitHub release
flow.

Why not the alternatives:

- **Native loadable ELF (pre-compiled C):** the pinned IDF 4.4 (Arduino 2.0.17, see `platformio.ini`) cannot
  execute code from PSRAM, there is no sandbox, and the app is tied to the exact firmware ABI. A buggy app
  panics the device and trips bootguard.
- **Lua or JS interpreter:** works, but source ships as text, it is one language, slower, and needs a bigger
  binding surface. WASM is pre-compiled, takes any language (C, Rust, Zig), has a stable host ABI, and traps
  instead of panicking.
- **Declarative JSON widgets:** too weak for full custom UI. Possible later as another app type behind the
  same manifest.
- **Per-app firmware variants or one OTA slot per app:** there are only two 3.2 MiB slots. No.

## Constraints

- **Flash:** the only free flash is the `spiffs` partition, 1.5 MiB at 0x670000, currently unused. Use it as
  LittleFS. WASM apps are roughly 10-150 KB, and bundles are compressed, so tens of apps fit.
- **Partition table:** it sits at 0x8000, outside both OTA slots, and `Update` only writes an app slot. Never
  rewrite it over OTA: power loss mid-write bricks a device that has no practical USB access. The store reads
  the real data-partition size at runtime (`esp_partition_find`) and budgets against it, so one firmware
  works on both layouts.
- **Unverified hardware figures:** the README's 8 MB flash and 8 MB PSRAM are inferred, not measured. Many
  WT32-SC01 Plus units are 16 MB flash with 2 MB PSRAM. PSRAM size drives the WASM memory budget, so measure
  first.
- **Heap:** WASM linear memory and the interpreter stack go in PSRAM, which keeps the internal heap free for
  Wi-Fi and TLS (same policy as `ui_alloc`, `ui_core.cpp:18-32`).
- **Trust:** OTA TLS is not certificate-verified and there is no signature check (`ota.cpp`). Running
  downloaded code makes that a real hole. Bundles are verified with an ed25519 public key embedded in the
  firmware, at install and again at load.
- **Firmware size:** about 1.2 MB of headroom in the 3.2 MiB slot. wasm3 adds roughly 100-150 KB.
- **Threading:** all LVGL calls run on the `lvgl` task (`ui.h:3-4`). Apps run in their own task and go through
  `ui_post()` (`ui_core.cpp:212`), or the host API marshals calls for them.
- **Bootguard:** three crashes in a row force safe mode, and a 90 s hang trips the watchdog. Apps must never
  be able to cause either.
- **Safe mode:** it skips UI and hardware init, so the app system must be fully inert there.

## Architecture

1. **App registry.** Replace the fixed enum with an id-keyed registry. Built-in pages become built-in apps
   using the same `PageOps` shape (`ui_internal.h:112-117`) plus a context pointer. Installed WASM apps
   register at boot from their manifests on LittleFS. `cfg.pageOrder` and `cfg.pg[]` key off string ids, not
   enum slots. NVS keys stay compatible (never rename a key or change its type).
2. **Bundle format** (`.btapp`, one file): manifest JSON (id, name, version, `min_fw_api`, permissions, icon,
   size), `app.wasm`, and an optional icon. The bundle is compressed and signed: ed25519 over the sha256 of
   the whole bundle, detached signature.
3. **Host API** (versioned by `api_level`, imports namespaced `bt.*`):
   - UI: create and destroy a screen root; label, button, image, bar and canvas primitives as thin LVGL
     wrappers that hand out integer handles, never raw pointers; style setters; event callbacks; the theme
     palette (`PX_*`).
   - Net: `http_get(url)` into a buffer, via the existing `http_get()` and `g_http_mutex`
     (`net.cpp:130-150`), with size and time caps and a host allowlist from the manifest.
   - Hardware: `buzzer_click`/`buzzer_alert`, `led_pulse`/`led_solid` (`hw.h`), brightness.
   - Storage: per-app key/value in its own namespace, with a quota.
   - Time, log (to `diag`), random.
   - Permissions are declared in the manifest, shown at install, and checked on every host call.
4. **Runtime.** One wasm3 instance per running app; only the foreground app runs. Own FreeRTOS task, WASM
   memory in PSRAM, hard caps on memory pages, time slice (periodic yield plus a deadline) and call depth.
   Lifecycle exports: `app_init`, `app_update` (1 Hz, like `update()`), `app_event`, `app_destroy`. A trap or
   timeout kills the instance, destroys its LVGL tree, shows an error card and marks the app faulted.
5. **Isolation from bootguard.** App faults are counted in a separate NVS counter (`btfw`, per-app key).
   Three faults auto-disable that app and leave the device alone. App code never runs before the wizard and
   web server are up, and never in safe mode. The bootguard logic itself does not change.
6. **Store and installer.**
   - Catalog: an `apps.json` asset on a GitHub release of the firmware repo (or a separate apps repo), listing
     id, name, version, `min_fw_api`, url, sha256, signature, size and icon. Cache with ETag.
   - Reuse from `ota.cpp`: the redirect-following `HTTPClient`, the stall and length checks,
     `ota_version_newer()`, and the ArduinoJson filtering. Download to a temp file, verify sha256 and
     signature, then rename atomically. Fail closed.
   - UI: a menu-only "Store" page (like CONVERTER, see `PageInfo.menuOnly`) to list, install, remove and
     update.
   - Web UI (`web.cpp`, `web_page.h`): the same list plus install and remove endpoints, so a headless device
     can use it. Enable/disable toggles replace the hard-coded page checkboxes.
   - The fetch-task gating in `net.cpp` (`wants_*()`) becomes per-app, so built-in data fetches only run when
     their app is enabled.
7. **SDK and CI.** An `apps/` directory with a small C header (`bt_app.h`) and an example app. CI builds
   bundles with clang `--target=wasm32` (or wasi-sdk), signs with a repo secret, and uploads `apps.json` plus
   the `.btapp` assets to the release. The private key is never in the repo; the public key is committed.

## Phases

Each phase is shippable on its own and goes through the host tests.

1. **Measure hardware.** Add flash size, PSRAM size and partition sizes to `/diag`. Decide the WASM memory cap
   and whether a larger layout is worth it.
2. **Registry refactor, no behaviour change.** Enum to id registry, `PageOps` gets a context pointer. Each
   existing page becomes a built-in app entry. The host `make run` PNGs must be identical before and after.
3. **Storage, bundle, verification.** LittleFS on `spiffs`, bundle parser, sha256 and ed25519 verify,
   install and remove. Host tests: tampered bundle, truncated bundle, bad signature, wrong `api_level`, quota
   full.
4. **wasm3 runtime and a minimal host API** (label, button, bar, log, time). A hello-world app renders in the
   host PNG test (wasm3 compiles on the host). Stress tests: infinite loop, huge allocation, trap, deep
   recursion, an app that never returns from `app_update`.
5. **Store.** Catalog fetch, download and install with progress, menu page, web endpoints. Extend
   `host/ota_test` `server.py` with a catalog and bundles (stalls, truncation, bad redirects, bad signature).
6. **Full host API** (net, hardware, storage, canvas) and the permissions UI.
7. **Opt-in large layout.** A custom partition CSV with a bigger data partition, sized from the measured
   flash, plus documented USB-only migration steps in the README (back up NVS first, flash table and app
   with esptool). The stock OTA path is untouched and the firmware adapts to whichever layout it finds.
8. **Port one or two built-ins out of the firmware** as real apps (for example Bitaxe or News) to prove the
   API and shrink the firmware. Core pages stay built in.

## Critical files

- `firmware/src/config.h`, `pages.{h,cpp}`, `ui_pages.cpp`, `ui_internal.h`, `ui_core.cpp` (`show_page`,
  `ui_next_page`, `ui_task`), `ui_menu.cpp`: registry refactor.
- `firmware/src/ota.cpp`: factor out the generic download loop for reuse.
- `firmware/src/net.cpp`: per-app fetch gating, `http_get()` reuse.
- `firmware/src/web.cpp`, `web_page.h`: app endpoints and toggles.
- `firmware/src/main.cpp`, `bootguard.cpp`: init order only; no change to crash logic.
- `firmware/platformio.ini`: add wasm3, LittleFS, an ed25519 library (monocypher is a few KB).
- `firmware/host/Makefile`, `host_main.cpp` (keep `host_step` in sync), `host/ota_test/`: new test targets.
- New: `firmware/src/apps/` (registry, bundle, runtime, host API, store), `apps/` SDK and examples, a release
  workflow step.

## Open risks

- wasm3 speed on the Xtensa S3 for animated UI. Prototype in phase 4 before committing to the large API.
- ed25519 verify cost and code size on device (small, but measure).
- Whether to also fix OTA TLS verification (cert bundle) as a prerequisite. Signing covers apps; firmware OTA
  stays unsigned. Separate decision.
- GitHub release asset limits and rate limits for catalog polling.

## Verification

- `cd firmware/host && make -j8 run && make wifi-test && make ota-test`, plus a new `make app-test`. Look at
  `host/out/*.png` for the registry refactor (must match pre-change) and for the sample app.
- `pio run` and `pio run -e safe` (apps inert). Check the image stays under the slot with headroom.
- On device: install a sample app from the store over Wi-Fi, reboot and confirm it persists, remove it.
  Tamper test (flip a byte in the hosted bundle, install must be refused). An infinite-loop app is killed,
  the device stays up and the bootguard counter is unchanged.
- `scripts/format.sh --check` before any PR.
