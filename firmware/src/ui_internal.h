// Shared declarations for the UI sources (ui_core, ui_pages, ui_menu). Not a public header.
#pragma once
#include <Arduino.h>
#include <functional>
#include <lvgl.h>
#include "config.h"
#include "net.h"
#include "pages.h"

// ---------------------------------------------------------------- geometry

static const int SCREEN_W = 480;
static const int SCREEN_H = 320;
static const int STREAM_BAR_H = 30;
static const int NAV_BAR_H = 49;            // bottom bar of the menu

// ---------------------------------------------------------------- fonts
//
// Built-in Montserrat only covers ASCII and the degree sign, so text goes through ui_tr()
// to transliterate German umlauts. firmware/tools/gen_fonts.sh can generate Latin-1 fonts
// with large digits; when they exist (UI_CUSTOM_FONTS) they are used and ui_tr() is a no-op.

#if __has_include("fonts/ui_fonts.h")
#include "fonts/ui_fonts.h"
#define UI_CUSTOM_FONTS 1
#else
#define FONT_XS  (&lv_font_montserrat_14)
#define FONT_S   (&lv_font_montserrat_18)
#define FONT_M   (&lv_font_montserrat_24)
#define FONT_L   (&lv_font_montserrat_28)
#define FONT_XL  (&lv_font_montserrat_36)
#define FONT_2XL (&lv_font_montserrat_40)
#define FONT_3XL (&lv_font_montserrat_48)
#define FONT_HUGE (&lv_font_montserrat_48)          // block height, clock
#endif

String ui_tr(const char *utf8);

// ---------------------------------------------------------------- colours (RGB565 values of the stock UI)

static inline lv_color_t c565(uint16_t v) { lv_color_t c; c.full = v; return c; }
#define COL_WHITE   c565(0xFFFF)
#define COL_ORANGE  c565(0xFD20)
#define COL_GREEN   c565(0x07E0)
#define COL_RED     c565(0xF800)
#define COL_GRAY    c565(0x632C)      // secondary text
#define COL_LABEL   c565(0x8C51)      // small caption text
#define COL_TRACK   c565(0x2966)      // bar background
#define COL_PANEL   c565(0x1082)      // cards
#define COL_BTN     c565(0x2124)      // idle buttons
#define COL_BLUE    c565(0x3CFF)
#define COL_AMBER   c565(0xFD20)

// ---------------------------------------------------------------- helpers

static inline bool valid(lv_obj_t *o) { return o && lv_obj_is_valid(o); }

lv_obj_t *mk_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color = COL_WHITE);
lv_obj_t *mk_box(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);                 // transparent, not scrollable
lv_obj_t *mk_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);                // filled rounded panel
lv_obj_t *mk_row(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);                 // flex row, centred
lv_obj_t *mk_col(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);                 // flex column, centred
void set_text(lv_obj_t *label, const String &text);                             // runs ui_tr()
String fmt_de(float value, int decimals);                                       // "1234,5" (decimal comma)
String fmt_int_sep(long value, bool separator);                                 // "97.123" or "97123"
const char *currency_symbol(int cur);                                           // "$", "EUR", "CHF"
int  currency_index(const String &code);                                        // USD 0, EUR 1, CHF 2
const char *currency_code(int cur);

// Runs `fn` on the lvgl task.
void ui_post(std::function<void()> fn);

// ---------------------------------------------------------------- pages (ui_pages.cpp)

struct PageOps {
    bool (*available)();                    // false when the page has nothing to show (BITAXE without miners)
    void (*build)(lv_obj_t *parent);        // parent fills the content area
    void (*update)();                       // called when data changed and once per second
    void (*destroy)();
};
extern const PageOps PAGE_OPS[PAGE_COUNT];

bool ui_page_usable(int page);              // enabled and available
void ui_page_reset_led();                   // forgets the LED direction cache (call on page change)

// ---------------------------------------------------------------- core (ui_core.cpp)

lv_obj_t *ui_screen();                      // root screen object
int  ui_current_page();
void ui_goto_page(int page);                // request a page switch
int  ui_next_page(int from, int step);      // next usable rotating page in page_order(), step +1 / -1
void ui_set_brightness(int percent);
void ui_apply_rotation();                   // re-applies cfg.rotation and redraws
bool ui_stream_bar_visible();               // pages that care about the available height rebuild on change
void ui_wake();                             // reset the night-mode idle timer

// ---------------------------------------------------------------- menu (ui_menu.cpp)

void ui_menu_open();
void ui_menu_close();
bool ui_menu_is_open();
void ui_menu_tick();                        // once per second while open (IP address, switch states)
