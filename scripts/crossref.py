#!/usr/bin/env python3
"""Reliable l32r -> rodata string cross-reference using objdump's resolved output.

objdump disassembles the code (from a known code start) and prints l32r lines as:
    42003abc:   a133f3   l32r a10, 0x4200158c (0x3c21aca8)
where the parenthesized value is the dereferenced literal-pool entry.
We parse these and keep only those whose resolved value lands in rodata (seg0).
"""
import os, re, subprocess, struct, json, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EX = os.path.join(ROOT, "extracted")
OD = os.environ.get("XTENSA_OBJDUMP") or shutil.which("xtensa-esp32s3-elf-objdump")
if not OD:
    raise SystemExit("xtensa-esp32s3-elf-objdump not found: set XTENSA_OBJDUMP=/path/to/xtensa-esp32s3-elf-objdump")

SEGS = [
    ("app_seg02_0x40374000.bin", 0x40374000, 0x40374000),  # code starts at load addr (entry here)
    ("app_seg03_0x42000020.bin", 0x42000020, 0x42003908),  # code starts after leading literal pool
    ("app_seg04_0x4037a09c.bin", 0x4037A09C, 0x4037A09C),
]

RO_BASE = 0x3C120020
RO_LEN = 0x144330
RO_LO, RO_HI = RO_BASE, RO_BASE + RO_LEN

# build rodata string map
ro = open(os.path.join(EX, "app_seg00_0x3c120020.bin"), "rb").read()
def strings_map(data, base):
    m = {}
    i = 0
    while i < len(data):
        j = data.find(b"\x00", i)
        if j < 0: j = len(data)
        s = data[i:j]
        if len(s) >= 3:
            m[base + i] = s.decode("utf-8", "replace")
        i = j + 1
    return m
strmap = strings_map(ro, RO_BASE)

l32r_re = re.compile(r"^([0-9a-f]+):\s+[0-9a-f]{6}\s+l32r\s+a(\d+),\s+0x([0-9a-f]+)\s+\((0x[0-9a-f]+)\)")

refs = {}
for fname, load, start in SEGS:
    path = os.path.join(EX, fname)
    end = load + os.path.getsize(path)
    cmd = [OD, "-b", "binary", "-m", "xtensa", "--adjust-vma", f"0x{load:08x}",
           "--start-address", f"0x{start:08x}", "--stop-address", f"0x{end:08x}", "-D", path]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    n = 0
    for line in out.splitlines():
        m = l32r_re.match(line.strip())
        if not m:
            continue
        code_va = int(m.group(1), 16)
        lit_va = int(m.group(3), 16)
        val = int(m.group(4), 16)
        if RO_LO <= val < RO_HI:
            s = strmap.get(val, "")
            refs.setdefault(val, []).append((code_va, lit_va))
            n += 1
    print(f"{fname}: code 0x{start:08x}-0x{end:08x}, rodata l32r refs: {n}")

print(f"total unique rodata targets referenced: {len(refs)}")
# map rodata VA -> string, and dump refs for display strings
out = []
for val, sites in sorted(refs.items()):
    s = strmap.get(val, "")
    for code_va, lit_va in sites:
        out.append({"code": f"0x{code_va:08x}", "lit": f"0x{lit_va:08x}",
                    "target": f"0x{val:08x}", "string": s})
json.dump(out, open(os.path.join(EX, "crossref2.json"), "w"), indent=1)

# show display-relevant
KEY = ("PREIS", "DIFFICULTY", "BLOCK", "FEES", "SEITEN", "RETARGET", "sat/vB",
       "HashRate", "Hodl", "Halving", "NÄCHSTER", "Blöcke", "Blockzeit")
print(f"\n=== display-relevant refs ===")
for val, sites in sorted(refs.items()):
    s = strmap.get(val, "")
    if any(k in s for k in KEY):
        for code_va, lit_va in sites:
            print(f"code 0x{code_va:08x}  ->  {s!r}")
