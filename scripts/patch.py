#!/usr/bin/env python3
"""Patch an ESP32 (ESP-IDF) app image and recompute its integrity metadata.

The ESP-IDF app image carries two integrity values that must be refreshed after
editing any byte:

1. A 1-byte XOR checksum over all segment data (0xEF seed), stored at image[-33].
2. The SHA256 over the whole image up to the final 32 bytes:
       image[-32:] = sha256(image[:-32])

Both are checked by the bootloader and the OTA updater (esp_image_verify).

Usage:
    patch.py firmware.bin '1.8.2=1.8.3'                    # replace first ASCII match
    patch.py firmware.bin 0x91438:312e382e33               # patch raw bytes at offset
    patch.py -o out.bin firmware.bin 'FOO=BAR' 0x200:00ff  # multiple patches
"""
import argparse
import hashlib
import struct


def apply(data: bytearray, spec: str) -> None:
    if ":" in spec and spec.split(":", 1)[0].startswith(("0x", "0X")):
        off_s, hex_s = spec.split(":", 1)
        off = int(off_s, 16)
        raw = bytes.fromhex(hex_s)
        data[off:off + len(raw)] = raw
    else:
        if "=" not in spec:
            raise SystemExit(f"patch must be STRING=NEW or 0xOFF:HEX, got {spec!r}")
        old, new = spec.split("=", 1)
        old_b, new_b = old.encode(), new.encode()
        if len(new_b) > len(old_b):
            raise SystemExit(f"replacement longer than original: {old!r} -> {new!r}")
        new_b = new_b.ljust(len(old_b), b" ")  # pad so offsets stay stable
        off = data.find(old_b)
        if off < 0:
            raise SystemExit(f"string not found: {old!r}")
        data[off:off + len(new_b)] = new_b


def recompute_checksum(data: bytearray) -> None:
    """XOR checksum over all segment data, stored at image[-33]."""
    nseg = data[1]
    off = 24  # 8-byte header + 16-byte extended header
    ck = 0xEF
    for _ in range(nseg):
        _, ln = struct.unpack("<II", data[off:off + 8])
        off += 8
        for b in data[off:off + ln]:
            ck ^= b
        off += ln
    data[len(data) - 33] = ck & 0xFF


def main() -> None:
    ap = argparse.ArgumentParser(description="Patch ESP32 app image + recompute checksum/SHA256")
    ap.add_argument("input", help="input firmware image")
    ap.add_argument("patch", nargs="+", help="STRING=NEW or 0xOFF:HEX (repeatable)")
    ap.add_argument("-o", "--output", default="patched_firmware.bin")
    a = ap.parse_args()

    d = bytearray(open(a.input, "rb").read())
    for spec in a.patch:
        apply(d, spec)

    recompute_checksum(d)
    d[-32:] = hashlib.sha256(bytes(d[:-32])).digest()
    open(a.output, "wb").write(d)
    print(f"wrote {a.output} ({len(d)} bytes, checksum + sha256 recomputed)")


if __name__ == "__main__":
    main()
