#!/usr/bin/env python3
"""Reconstruct a minimal ELF32 (EM_XTENSA) from the ESP32-S3 app image segments,
with symbols for the entry point, display object, and key display functions.
Loadable in Ghidra (native Xtensa, 11.0+)."""
import struct, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EX = os.path.join(ROOT, "extracted")

# (filename, vaddr, flags)  flags: 5=RX, 6=RW, 4=R
SEGS = [
    ("app_seg00_0x3c120020.bin", 0x3C120020, 4),  # rodata
    ("app_seg01_0x3fc956f0.bin", 0x3FC956F0, 6),  # .data
    ("app_seg02_0x40374000.bin", 0x40374000, 5),  # IRAM code
    ("app_seg03_0x42000020.bin", 0x42000020, 5),  # XIP code
    ("app_seg04_0x4037a09c.bin", 0x4037A09C, 5),  # IRAM code
]
ENTRY = 0x403779B8

SYMBOLS = [
    ("entry",               ENTRY,      0x12),  # GLOBAL FUNC
    ("display_obj",         0x3FC9F384, 0x11),  # GLOBAL OBJECT
    ("lgfx_drawString",     0x4205E810, 0x12),
    ("lgfx_fillRect",       0x4203E81C, 0x12),
    ("lgfx_setTextColor",   0x4203D548, 0x12),
    ("lgfx_setFont",        0x4203D55C, 0x12),
    ("lgfx_fillScreen",     0x42043F84, 0x12),
    ("draw_price_page",     0x4202923C, 0x12),
    ("draw_fees_page",      0x4202A33C, 0x12),
    ("draw_news_page",      0x4202B36C, 0x12),
    ("draw_menu_config",    0x4202E9CC, 0x12),
]

def align(n, a):
    return (n + a - 1) & ~(a - 1)

def build():
    # gather data
    datas = []
    for fname, vaddr, flags in SEGS:
        datas.append((open(os.path.join(EX, fname), "rb").read(), vaddr, flags))

    ehsize = 52
    phentsize = 32
    nphdr = len(SEGS)
    shnum = 4  # null, .symtab, .strtab, .shstrtab
    shentsize = 40

    # file layout: ehdr, phdrs, segment data (aligned), symtab, strtab, shstrtab, shdr table
    off = ehsize + nphdr * phentsize
    seg_offsets = []
    for data, vaddr, flags in datas:
        off = align(off, 0x1000)
        seg_offsets.append(off)
        off += len(data)

    # build strtab (names)
    names = [b""]  # index 0 = null
    name_off = {}
    def add_name(n):
        if n not in name_off:
            name_off[n] = len(names[-1]) if False else 0
        # simpler: sequential
    # build string table sequentially
    strtab = b"\x00"
    for name, addr, info in SYMBOLS:
        nb = name.encode() + b"\x00"
        offname = len(strtab)
        strtab += nb
        # stash
    # we need per-symbol name offset; rebuild cleanly
    strtab = b"\x00"
    sym_names = []
    for name, addr, info in SYMBOLS:
        offname = len(strtab)
        strtab += name.encode() + b"\x00"
        sym_names.append((name, addr, info, offname))

    symtab_off = off
    symtab = b""
    # null symbol
    symtab += struct.pack("<IIIBBH", 0, 0, 0, 0, 0, 0)
    for name, addr, info, offname in sym_names:
        # st_name, st_value, st_size, st_info, st_other, st_shndx
        # map symbol to a section index: use 1 for .text-ish? we don't have .text section;
        # use SHN_ABS (0xfff1) so Ghidra treats as absolute address
        symtab += struct.pack("<IIIBBH", offname, addr, 0, info, 0, 0xFFF1)
    off += len(symtab)

    strtab_off = off
    off += len(strtab)

    # .shstrtab
    shstrtab = b"\x00.symtab\x00.strtab\x00.shstrtab\x00"
    shstrtab_off = off
    off += len(shstrtab)

    shdr_off = off
    off += shnum * shentsize

    # ELF header
    e_ident = b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\x00" * 8  # 32-bit LE, ver 1
    ehdr = e_ident + struct.pack(
        "<HHIIIIIHHHHHH",
        2,              # e_type = ET_EXEC
        0x5E,           # e_machine = EM_XTENSA
        1,              # e_version
        ENTRY,          # e_entry
        ehsize,         # e_phoff
        shdr_off,       # e_shoff
        0,              # e_flags
        ehsize,         # e_ehsize
        phentsize,      # e_phentsize
        nphdr,          # e_phnum
        shentsize,      # e_shentsize
        shnum,          # e_shnum
        3,              # e_shstrndx
    )

    # program headers
    phdrs = b""
    for (data, vaddr, flags), p_offset in zip(datas, seg_offsets):
        phdrs += struct.pack("<IIIIIIII",
            # p_type=PT_LOAD(1), p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align
            1, p_offset, vaddr, vaddr, len(data), len(data), flags, 0x1000)

    # section headers
    shdrs = b""
    # null section
    shdrs += b"\x00" * shentsize
    # .symtab (sh_name=1, sh_type=2=SHT_SYMTAB, sh_link=2 -> .strtab)
    shdrs += struct.pack("<IIIIIIIIII", 1, 2, 0, 0, symtab_off, len(symtab), 2, 1, 4, 16)
    # .strtab (sh_name=9)
    shdrs += struct.pack("<IIIIIIIIII", 9, 3, 0, 0, strtab_off, len(strtab), 0, 0, 1, 0)
    # .shstrtab (sh_name=17)
    shdrs += struct.pack("<IIIIIIIIII", 17, 3, 0, 0, shstrtab_off, len(shstrtab), 0, 0, 1, 0)

    out = bytearray()
    out += ehdr
    out += phdrs
    # pad to first segment
    out += b"\x00" * (seg_offsets[0] - len(out))
    for (data, vaddr, flags), p_offset in zip(datas, seg_offsets):
        if len(out) < p_offset:
            out += b"\x00" * (p_offset - len(out))
        assert len(out) == p_offset, (hex(len(out)), hex(p_offset))
        out += data
    assert len(out) == symtab_off
    out += symtab
    out += strtab
    out += shstrtab
    out += shdrs

    path = os.path.join(EX, "firmware.elf")
    open(path, "wb").write(bytes(out))
    print(f"wrote {path} ({len(out)} bytes)")
    print(f"  segments: {nphdr}, symbols: {len(SYMBOLS)}, entry 0x{ENTRY:08x}")

if __name__ == "__main__":
    build()
