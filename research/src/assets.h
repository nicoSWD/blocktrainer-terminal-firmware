// assets.h: binary assets the firmware embeds in flash and this reconstruction
// does NOT reproduce.
//
// Fonts: the firmware uses 9 custom LVGL fonts, identified by their line_height
// (struct at 0x3c17fdb0 = 17 px, 0x3c181c4c = 21, 0x3c184b40 = 30, 0x3c18a100 = 36,
// 0x3c190f5c = 42, 0x3c19a148 = 52, 0x3c1a8124 = 61, 0x3c122274 = 88, 0x3c12fe90 = 126).
// All share get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt, so they come from lv_font_conv.
// The glyph bitmaps live in the binary and are not recovered; the stand-ins below let the
// project compile with the closest built-in Montserrat size.
//
// Images: logo / header / block icon (e.g. 0x3c1cf310, 0x3c1c11f8, 0x3c1d8ce4, 0x3c1d855c)
// are lv_img_dsc_t blobs in rodata. They are left out (NULL) here.
#pragma once
#include <lvgl.h>

#define FONT_LH17   (&lv_font_montserrat_14)
#define FONT_LH21   (&lv_font_montserrat_18)
#define FONT_LH30   (&lv_font_montserrat_24)
#define FONT_LH36   (&lv_font_montserrat_28)
#define FONT_LH42   (&lv_font_montserrat_32)
#define FONT_LH52   (&lv_font_montserrat_36)
#define FONT_LH61   (&lv_font_montserrat_40)
#define FONT_LH88   (&lv_font_montserrat_48)
#define FONT_LH126  (&lv_font_montserrat_48)

// colours: RGB565 values taken from the decompile (LV_COLOR_DEPTH 16, so lv_color_t.full is RGB565)
static inline lv_color_t c565(uint16_t v) { lv_color_t c; c.full = v; return c; }

#define COL_WHITE     c565(0xFFFF)
#define COL_ORANGE    c565(0xC3E1)   // INSTALLIEREN button, accent
#define COL_GREEN     c565(0x07E0)   // positive change / "0,00 $" cost labels
#define COL_RED       c565(0xF800)   // negative change
#define COL_GRAY      c565(0x52AA)   // ÜBERSPRINGEN button
#define COL_AMBER     c565(0xFD20)   // version text on the update-done screen
#define COL_BAR_BG    c565(0x2966)   // progress bar track

extern const lv_img_dsc_t *IMG_LOGO;           // 0x3c1cf310, NULL here
extern const lv_img_dsc_t *IMG_BLOCK;          // 0x3c1d8ce4, NULL here
