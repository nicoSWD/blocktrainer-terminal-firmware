# Agent notes

Custom ESP32-S3 firmware for the BT Clock / BT Terminal (WT32-SC01 Plus class board). Main
code lives in `firmware/`; see `firmware/README.md` for the full picture (build, install, OTA,
reliability design). This file is the short version for an agent working in the repo.

## Build

```bash
cd firmware
.venv/bin/pio run              # default env: wt32-sc01-plus
.venv/bin/pio run -e debug      # serial logging build
.venv/bin/pio run -e safe       # networking only, no display/sound/LED
```

If PlatformIO isn't installed, `pip install platformio` into a venv first; don't assume it's
on PATH.

## Tests (no hardware needed)

```bash
cd firmware/host
make -j8 run          # renders every page/status screen to out/*.png, ASan/UBSan stress test
make wifi-test        # wifi_join()/scan/radio-mutex unit tests
make ota-test          # OTA against a scripted server (stalls, trickles, early close, redirects)
```

Run all three before opening a PR that touches `src/`. `make run` output PNGs are worth
actually looking at (`host/out/`) when a change touches layout, not just a passing exit code.

## Layout

See the table in `firmware/README.md` ("Layout" section) for the file-to-role map
(`wifi_link.*`, `ui_core.cpp`, `bootguard.*`, etc.). Don't duplicate it here; it drifts.

## Conventions

- Formatting is `clang-format` (config: `firmware/.clang-format`), enforced in CI. Run
  `scripts/format.sh` before committing (`--check` to verify). Install the pinned version with
  `firmware/.venv/bin/pip install clang-format==23.1.3`; a different version can format differently.
  - Control-flow bodies always get braces (`InsertBraces`), and one-liner lambdas/ifs are expanded;
    don't fight the formatter by hand, change `.clang-format` if a rule is wrong.
  - A trailing comment on a line that the formatter expands can make the output unstable (a second
    run changes it). Put such comments on their own line above the code.
  - Don't mix a version bump or style change of the formatter with other edits: reformat in its own
    commit and add its hash to `.git-blame-ignore-revs`.
- No comments explaining *what* code does; only *why*, for non-obvious constraints
  (hardware quirks, a specific bug workaround, an invariant that would surprise a reader).
- Keep the host test suite (`firmware/host/`) in sync with any change to `src/` logic it
  covers — the host build compiles the real `.cpp` files, so new/changed logic paths usually
  need new assertions there rather than a hardware test.
- `include/secrets.h` is gitignored and holds the real built-in Wi-Fi SSID/password
  (`BUILTIN_WIFI_SSID`/`BUILTIN_WIFI_PASS`); never commit it or print its contents.
- Wi-Fi/OTA/boot reliability is the highest-stakes part of this codebase: the device has no
  USB access in normal testing, so a boot that can't reach the wizard or the web server is
  hard to recover from. Changes there should go through the host tests, not just "builds
  clean".

## Git / PRs

- Base branch is `main`. Open PRs against it.
- Don't touch the MITM/OTA-bridge path (`scripts/mitm_firmware.py`, mitmproxy setup) or do
  anything resembling TLS interception setup without the user explicitly asking in that
  session.

## Releases

- Release = push a `vMAJOR.MINOR.PATCH` tag on a commit that is on `main`. Nothing else is manual.
- `.github/workflows/firmware-release.yml` builds with `-DFIRMWARE_VERSION` from the tag, runs the
  three host test suites as a gate, then publishes the GitHub release (asset names matter: the
  device's OTA check matches `bt-terminal-firmware-v<version>.bin`).
- Last step commits the new version to the default in `firmware/src/config.h` on `main`
  (`.github/scripts/bump-firmware-version.sh`, `[skip ci]`). Don't bump it by hand or tag from the
  workflow: a tag pushed with `GITHUB_TOKEN` doesn't trigger other workflows.
- `workflow_dispatch` builds and tests only; it creates no release and just prints the bump diff.
