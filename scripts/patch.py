#!/usr/bin/env python3
"""Patch an ESP32 (ESP-IDF) app image and recompute its SHA256.

The ESP-IDF app image stores its SHA256 in the final 32 bytes:
    image[-32:] = sha256(image[:-32])

After editing any bytes, that trailing hash must be recomputed or the
bootloader / OTA updater rejects the image.

Usage:
    patch.py firmware.bin '1.8.2=1.8.3'                    # replace first ASCII match
    patch.py firmware.bin 0x91438:312e382e33               # patch raw bytes at offset
    patch.py -o out.bin firmware.bin 'FOO=BAR' 0x200:00ff  # multiple patches
"""
import argparse
import hashlib


def apply(data: bytearray, spec: str) -> None:
    if ":" in spec and spec.split(":", 1)[0].startswith(("0x", "0X")):
        off_s, hex_s = spec.split(":", 1)
        off = int(off_s, 16)
        raw = bytes.fromhex(hex_s)
        data[off:off + len(raw)] = raw
    else:
        if "=" not in spec:
            raise SystemExit(f"patch must be STRING=NEW or 0xOFF:HEX — got {spec!r}")
        old, new = spec.split("=", 1)
        old_b, new_b = old.encode(), new.encode()
        if len(new_b) > len(old_b):
            raise SystemExit(f"replacement longer than original: {old!r} -> {new!r}")
        new_b = new_b.ljust(len(old_b), b" ")  # pad so offsets stay stable
        off = data.find(old_b)
        if off < 0:
            raise SystemExit(f"string not found: {old!r}")
        data[off:off + len(new_b)] = new_b


def main() -> None:
    ap = argparse.ArgumentParser(description="Patch ESP32 app image + recompute SHA256")
    ap.add_argument("input", help="input firmware image")
    ap.add_argument("patch", nargs="+", help="STRING=NEW or 0xOFF:HEX (repeatable)")
    ap.add_argument("-o", "--output", default="patched_firmware.bin")
    a = ap.parse_args()

    d = bytearray(open(a.input, "rb").read())
    for spec in a.patch:
        apply(d, spec)

    d[-32:] = hashlib.sha256(bytes(d[:-32])).digest()
    open(a.output, "wb").write(d)
    print(f"wrote {a.output} ({len(d)} bytes, sha256 recomputed)")


if __name__ == "__main__":
    main()
