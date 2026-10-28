#!/usr/bin/env python3
"""Unpack ESP32-S3 ESP-IDF v4 flash images: app image segments + partition table."""
import struct, sys, os

APP_MAGIC = 0xE9
PT_MAGIC = 0x50AA  # bytes aa50 read little-endian

def parse_app(path, outdir):
    data = open(path, "rb").read()
    if data[0] != APP_MAGIC:
        print(f"  ! not an app image (magic {data[0]:02x})")
        return
    nseg = data[1]
    entry = struct.unpack("<I", data[4:8])[0]
    spi_mode = data[2]
    spi_sz = data[3]
    print(f"  app image: segments={nseg} entry=0x{entry:08x} spi_mode=0x{spi_mode:02x} spi_speed_size=0x{spi_sz:02x}")
    # 16-byte extended header follows the 8-byte main header
    ext = data[8:24]
    wp_pin = ext[0]
    chip_id = struct.unpack("<H", ext[4:6])[0]
    hash_appended = ext[15]
    print(f"  ext hdr: wp_pin=0x{wp_pin:02x} chip_id=0x{chip_id:04x} hash_appended={hash_appended}")
    off = 24
    segs = []
    for i in range(nseg):
        addr, ln = struct.unpack("<II", data[off:off+8])
        off += 8
        seg = data[off:off+ln]
        off += ln
        segs.append((i, addr, ln, seg))
        fn = os.path.join(outdir, f"app_seg{i:02d}_0x{addr:08x}.bin")
        open(fn, "wb").write(seg)
        print(f"    seg{i}: load=0x{addr:08x} len=0x{ln:x} ({ln}) -> {os.path.basename(fn)}")
    rest = data[off:]
    print(f"  bytes after segments: {len(rest)}")
    if hash_appended and len(rest) >= 32:
        open(os.path.join(outdir, "app_image_sha256.bin"), "wb").write(rest[:32])
        print("    sha256 (32B) saved")
    if len(rest) > 32:
        open(os.path.join(outdir, "app_trailer.bin"), "wb").write(rest[32:])
        print(f"    extra {len(rest)-32} bytes saved as app_trailer.bin")

def parse_partitions(path, outdir):
    data = open(path, "rb").read()
    lines = []
    names = {0x00: "app", 0x01: "data"}
    subtypes_app = {0x00: "factory", 0x20: "test", 0x10: "ota_0", 0x11: "ota_1",
                    0x12: "ota_2", 0x13: "ota_3", 0x14: "ota_4", 0x15: "ota_5",
                    0x16: "ota_6", 0x17: "ota_7", 0x18: "ota_8", 0x19: "ota_9",
                    0x1a: "ota_10", 0x1b: "ota_11", 0x1c: "ota_12", 0x1d: "ota_13",
                    0x1e: "ota_14", 0x1f: "ota_15"}
    subtypes_data = {0x00: "ota", 0x01: "phy", 0x02: "nvs", 0x03: "coredump",
                     0x04: "nvs_keys", 0x05: "efuse", 0x81: "fat", 0x82: "spiffs",
                     0x83: "littlefs"}
    for i in range(0, len(data), 32):
        e = data[i:i+32]
        if len(e) < 32:
            break
        magic = struct.unpack("<H", e[0:2])[0]
        if magic != PT_MAGIC:
            print(f"  ! bad entry magic 0x{magic:04x} at 0x{i:x}")
            break
        t = e[2]; st = e[3]
        off = struct.unpack("<I", e[4:8])[0]
        size = struct.unpack("<I", e[8:12])[0]
        label = e[12:28].split(b"\x00")[0].decode("latin1", "replace")
        flags = struct.unpack("<I", e[28:32])[0]
        tn = names.get(t, f"0x{t:02x}")
        if t == 0x00:
            sn = subtypes_app.get(st, f"0x{st:02x}")
        elif t == 0x01:
            sn = subtypes_data.get(st, f"0x{st:02x}")
        else:
            sn = f"0x{st:02x}"
        lines.append(f"    {label:<8} type={tn:<5} subtype={sn:<9} offset=0x{off:06x} size=0x{size:06x} ({size//1024} KB) flags=0x{flags:04x}")
    print("  partition table:")
    print("\n".join(lines))

if __name__ == "__main__":
    base = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(base)
    outdir = os.path.join(root, "extracted")
    os.makedirs(outdir, exist_ok=True)
    for name in ("bootloader.bin", "firmware.bin", "partitions.bin"):
        p = os.path.join(root, name)
        print(f"== {name} ==")
        if name == "partitions.bin":
            parse_partitions(p, outdir)
        elif name == "firmware.bin":
            parse_app(p, outdir)
        else:
            print(f"  bootloader: {os.path.getsize(p)} bytes, magic {open(p,'rb').read(1).hex()}")
    # full strings dump
    fw = open(os.path.join(root, "firmware.bin"), "rb").read()
    strings = [s for s in fw.split(b"\x00") if len(s) >= 4]
    sf = os.path.join(outdir, "firmware_strings.txt")
    with open(sf, "w") as f:
        for s in strings:
            try:
                f.write(s.decode("utf-8", "replace") + "\n")
            except Exception:
                pass
    print(f"== strings ==\n  {len(strings)} strings -> {os.path.basename(sf)}")
