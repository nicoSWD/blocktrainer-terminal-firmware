#!/usr/bin/env python3
"""Generates src/ui_assets.{h,cpp}: the round page icons (Bitcoin logo and one glyph per page) as LVGL images.

Format LV_IMG_CF_TRUE_COLOR_ALPHA with LV_COLOR_DEPTH 16 and LV_COLOR_16_SWAP 0: three bytes per
pixel (RGB565 little endian, then alpha). Needs Pillow:  python3 tools/gen_assets.py  (from firmware/)
Optional preview sheet:  python3 tools/gen_assets.py --preview sheet.png
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw

SS = 8                                  # supersampling, the downscale gives the anti-aliasing
SIZES = (("", 36), ("_s", 24))
ORANGE_TOP, ORANGE_BOT = (255, 176, 59), (240, 130, 10)
W = 7                                   # stroke width in 100-unit coordinates


class Canvas:
    def __init__(self, n):
        self.s = n * SS
        self.m = Image.new("L", (self.s, self.s), 0)
        self.d = ImageDraw.Draw(self.m)

    def p(self, v):
        return v * self.s / 100

    def pts(self, pts):
        return [(self.p(x), self.p(y)) for x, y in pts]

    def rrect(self, x0, y0, x1, y1, rad=0, corners=(True, True, True, True), fill=255, width=0):
        self.d.rounded_rectangle([self.p(x0), self.p(y0), self.p(x1), self.p(y1)], radius=self.p(rad), corners=corners,
                                 fill=fill if not width else None, outline=255 if width else None,
                                 width=int(self.p(width)) if width else 0)

    def line(self, pts, w=W):
        pts = self.pts(pts)
        self.d.line(pts, fill=255, width=int(self.p(w)), joint="curve")
        r = self.p(w) / 2
        for x, y in (pts[0], pts[-1]):
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=255)

    def poly(self, pts):
        self.d.polygon(self.pts(pts), fill=255)

    def ring(self, cx, cy, r, w=W):
        self.d.ellipse([self.p(cx - r), self.p(cy - r), self.p(cx + r), self.p(cy + r)], outline=255, width=int(self.p(w)))


def g_btc(c):
    k = 1.2                                             # glyph size relative to the disc
    def r(x0, y0, x1, y1, rad=0, corners=(True, True, True, True), fill=255):
        x0, y0, x1, y1 = (50 + (v - 50) * k for v in (x0, y0, x1, y1))
        c.d.rounded_rectangle([c.p(x0), c.p(y0), c.p(x1), c.p(y1)], radius=c.p(rad * k), corners=corners, fill=fill)
    right = (False, True, True, False)
    r(35, 25, 47, 75)
    r(35, 25, 66, 51, 11, right)
    r(35, 47, 70, 75, 13, right)
    r(47, 35, 55, 41, 2, right, 0)
    r(47, 56, 60, 66, 4, right, 0)
    for x in (41, 54):
        r(x, 17, x + 5, 27)
        r(x, 73, x + 5, 83)
    c.m = c.m.rotate(-14, resample=Image.BICUBIC, center=(c.s / 2, c.s / 2))


def g_block(c):                                         # isometric cube
    c.line([(50, 17), (79, 33), (79, 67), (50, 83), (21, 67), (21, 33), (50, 17)])
    c.line([(21, 33), (50, 50), (79, 33)])
    c.line([(50, 50), (50, 83)])


def g_clock(c):
    c.ring(50, 50, 31)
    c.line([(50, 50), (50, 30)])
    c.line([(50, 50), (65, 59)])


def g_bolt(c):
    c.poly([(58, 14), (28, 55), (46, 55), (40, 86), (72, 43), (53, 43)])


def g_chip(c):
    c.rrect(30, 30, 70, 70, 6, width=W)
    c.rrect(42, 42, 58, 58, 2)
    for v in (38, 50, 62):
        c.line([(v, 17), (v, 29)], 5)
        c.line([(v, 71), (v, 83)], 5)
        c.line([(17, v), (29, v)], 5)
        c.line([(71, v), (83, v)], 5)


def g_news(c):
    c.rrect(27, 19, 73, 81, 7, width=W)
    c.line([(38, 38), (62, 38)], 5)
    c.line([(38, 50), (62, 50)], 5)
    c.line([(38, 62), (53, 62)], 5)


def g_bars(c):
    c.rrect(25, 55, 38, 79, 3)
    c.rrect(43, 40, 56, 79, 3)
    c.rrect(61, 24, 74, 79, 3)


def g_swap(c):
    c.line([(24, 38), (68, 38)])
    c.poly([(64, 25), (64, 51), (81, 38)])
    c.line([(76, 63), (32, 63)])
    c.poly([(36, 50), (36, 76), (19, 63)])


ICONS = (("btc", g_btc), ("block", g_block), ("clock", g_clock), ("bolt", g_bolt), ("chip", g_chip),
         ("news", g_news), ("bars", g_bars), ("swap", g_swap))


def icon(glyph, n):
    s = n * SS
    disc = Image.new("RGBA", (s, s))
    px = disc.load()
    for y in range(s):                                  # vertical gradient
        t = y / (s - 1)
        col = tuple(round(a + (b - a) * t) for a, b in zip(ORANGE_TOP, ORANGE_BOT))
        for x in range(s):
            px[x, y] = col + (255,)
    circle = Image.new("L", (s, s), 0)
    ImageDraw.Draw(circle).ellipse([0, 0, s - 1, s - 1], fill=255)
    disc.putalpha(circle)
    c = Canvas(n)
    glyph(c)
    white = Image.new("RGBA", (s, s), (255, 255, 255, 255))
    white.putalpha(c.m)
    disc.alpha_composite(white)
    return disc.resize((n, n), Image.LANCZOS)


def c_array(name, img):
    out = []
    for r, g, b, a in img.getdata():
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out += [v & 0xFF, v >> 8, a]
    rows = [", ".join("0x%02X" % x for x in out[i:i + 24]) for i in range(0, len(out), 24)]
    w, h = img.size
    return (f"static const uint8_t {name}_map[] = {{\n    " + ",\n    ".join(rows) + "\n};\n"
            f"const lv_img_dsc_t {name} = {{\n"
            f"    {{ LV_IMG_CF_TRUE_COLOR_ALPHA, 0, 0, {w}, {h} }}, {w * h * 3}, {name}_map\n}};\n")


def main():
    root = Path(__file__).resolve().parent.parent
    head = ["// Generated by tools/gen_assets.py, do not edit.", "#pragma once", "#include <lvgl.h>", ""]
    body = ["// Generated by tools/gen_assets.py, do not edit.", '#include "ui_assets.h"', ""]
    sheet = Image.new("RGBA", (len(ICONS) * 50 + 10, 110), (0, 0, 0, 255))
    for i, (name, glyph) in enumerate(ICONS):
        for suffix, n in SIZES:
            img = icon(glyph, n)
            head.append(f"extern const lv_img_dsc_t ui_icon_{name}{suffix};     // {n} px")
            body.append(c_array(f"ui_icon_{name}{suffix}", img))
            sheet.alpha_composite(img, (10 + 50 * i, 10 if not suffix else 60))
    (root / "src" / "ui_assets.h").write_text("\n".join(head) + "\n")
    (root / "src" / "ui_assets.cpp").write_text("\n".join(body))
    if "--preview" in sys.argv:
        sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(sys.argv[sys.argv.index("--preview") + 1])


main()
