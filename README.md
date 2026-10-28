# Blocktrainer Terminal Firmware Patch

Reverse-engineering toolkit for the **BT Clock / BT Terminal** firmware (ESP32-S3) by
[blocktrainer.de](https://btclock.blocktrainer.de). Fetch, unpack, decompile, patch, and
redeploy a modified firmware over the device's own OTA update, no flashing tools required.

> Educational / research use only. The firmware is copyright blocktrainer.de and is
> **not** included in this repo.

## The device

- **WT32-SC01 Plus**, ESP32-S3, 3.5" touch LCD
- ESP-IDF v4.4.7, Arduino (PlatformIO), LovyanGFX graphics
- Shows Bitcoin data (price, difficulty, fees, mempool) + hosts a web config page
- OTA update from `https://btclock.blocktrainer.de/v2/firmware/firmware.json`

## Workflow

```
fetch.sh ─▶ unpack.py ─▶ build_elf.py ─▶ Ghidra MCP (decompile) ─▶ patch.py ─▶ mitm_firmware.py
```

| step | tool | output |
|---|---|---|
| 1. Download firmware | `scripts/fetch.sh` | `firmware.bin` + `partitions.bin` + `bootloader.bin` + `firmware.json` |
| 2. Unpack app image | `scripts/unpack.py` | `extracted/` segments, strings, partition table |
| 3. Cross-ref strings | `scripts/crossref.py` | `extracted/crossref2.json` (code → string) |
| 4. Build ELF | `scripts/build_elf.py` | `extracted/firmware.elf` (for Ghidra) |
| 5. Decompile | Ghidra MCP | C pseudocode |
| 6. Patch | `scripts/patch.py` | `patched_firmware.bin` (SHA256 recomputed) |
| 7. Deploy via OTA | `scripts/mitm_firmware.py` | serve patched firmware during update |

> **Decompilation is optional.** Steps 3-5 (cross-ref, ELF, Ghidra MCP) are only needed to
> understand and change *code* (logic). A string patch (text, labels, URLs) skips them:
> `fetch.sh` → find the string → `patch.py` → `mitm_firmware.py`.

## Setup

```bash
git clone git@github.com:nicoSWD/blocktrainer-terminal-hack.git
cd blocktrainer-terminal-hack
python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt  # (if present)
```

Only Python stdlib is used, except `crossref.py` which needs an Xtensa objdump:

```bash
export XTENSA_OBJDUMP=/path/to/xtensa-esp32s3-elf-objdump
# or install the Espressif toolchain and add its bin/ to PATH
```

## 1. Fetch the firmware

```bash
bash scripts/fetch.sh
```

Downloads `firmware.json` (manifest: version + parts/offsets) and the three flash images.
`firmware.bin` is the app image; `bootloader.bin` and `partitions.bin` are context.

## 2. Unpack the app image

```bash
python3 scripts/unpack.py
```

Parses the ESP-IDF v4 image header (magic `0xE9`), dumps each segment to `extracted/`
with its load address, decodes the partition table, and writes every printable string to
`extracted/firmware_strings.txt`.

| segment | load addr | content |
|---|---|---|
| seg0 | `0x3c120020` | rodata, strings, fonts, web UI |
| seg1 | `0x3fc956f0` | DRAM `.data` |
| seg2/seg4 | `0x40374000` / `0x4037a09c` | IRAM code |
| seg3 | `0x42000020` | XIP code (main app) |

## 3. Cross-reference strings

```bash
python3 scripts/crossref.py
```

Disassembles the code segments with `xtensa-esp32s3-elf-objdump`, parses every `l32r`
literal-pool load, and maps each referenced rodata address back to its string, producing
`extracted/crossref2.json` (`code → string`). This is how you find which function draws
which label.

## 4. Build an ELF for Ghidra

```bash
python3 scripts/build_elf.py
```

Reconstructs a minimal ELF32 (`EM_XTENSA`) from the dumped segments, with the entry point
and symbols for the display functions (`draw_price_page`, `draw_fees_page`, `lgfx_drawString`, …).
Ghidra 11+ has native Xtensa, so it loads this directly.

## 5. Decompile via Ghidra MCP

Ghidra runs headless on a Linux box (tested on Kali / aarch64). Two scripts:

```bash
# on the Kali machine, installs Java 21, Ghidra 12.1.4, ghidra-mcp, and builds the
# ARM64 decompiler native binary (Ghidra ships none for linux_arm_64)
sudo ghidra/setup_kali.sh

# import firmware.elf + start the GhidraMCP headless server on :8089
ghidra/run_headless.sh firmware.elf
```

Then run the MCP bridge (HTTP transport, reachable from your workstation):

```bash
bridge-mcp-ghidra --transport streamable-http --mcp-host 0.0.0.0 --mcp-port 8081
```

Point your MCP client at it:

```json
{ "mcpServers": { "ghidra": { "type": "http", "url": "http://<kali-ip>:8081/mcp" } } }
```

From there the agent can call `decompile_function`, `rename_function`, `get_functions`, etc.

## 6. Patch the firmware

```bash
python3 scripts/patch.py firmware.bin '1.8.2=1.8.3' 'PREIS=PRICE'
# or raw bytes:   python3 scripts/patch.py firmware.bin 0x91438:312e382e33
```

Replaces strings in place (padding keeps offsets stable) and recomputes the app image
SHA256, the trailing 32 bytes are `sha256(image[:-32])`, required or the bootloader
rejects the image. Output: `patched_firmware.bin`.

## 7. Deploy via OTA interception

The device checks `firmware.json` for a newer version, then downloads `firmware.bin`.
Both are HTTPS, so you need a MITM the device trusts (the device ships a custom CA).
With that in place:

```bash
mitmproxy --emudhra -s scripts/mitm_firmware.py
```

`mitm_firmware.py` bumps `firmware.json`'s `version` (so the device sees a newer build)
and serves `patched_firmware.bin` when `firmware.bin` is requested. The updater uses
Arduino `Update` (no signature check, magic + CRC only), so the modified image is accepted.

**Both intercepts are required**, without the version bump the device logs
"Bereits auf neuester Version" and never requests `firmware.bin`.

## Safety & recovery

String-only patches (labels, text) are the safe kind: code and offsets stay byte-identical,
replacements are length-preserved (space-padded before the original null terminator), format
specifiers are kept, and the SHA256 is recomputed. A patched image boots identically to the
original, just different text.

If a patch is ever broken, three layers catch it:

1. **OTA flash-time verification**, `Update.end()` verifies the written image (magic + SHA256)
   before committing; a corrupt image is rejected and the old firmware stays put.
2. **Boot-time rollback**, rollback support is compiled in; a crashing new app can auto-roll
   back to the previous app after a few failed boots (bootloader-dependent).
3. **USB download mode**, the ESP32-S3 ROM bootloader is un-brickable. Hold BOOT + reset,
   re-flash any firmware with `esptool.py write_flash`. Recovery is guaranteed unless secure
   boot is enabled with a lost key (this device has no secure boot) or the USB/BOOT pins are
   physically unreachable.

## Repo layout

```
scripts/   fetch, unpack, cross-ref, patch, mitmproxy addon
ghidra/    Ghidra 12.1.4 + ghidra-mcp setup (headless, arm64 decompiler)
```

Firmware and all derived artifacts are gitignored, see `.gitignore`.

## Disclaimer

This is an independent reverse-engineering / interoperability project. It is not affiliated
with blocktrainer.de. Don't distribute the firmware or any derivative of it; don't use this
to bypass anything you're not authorized to modify.
