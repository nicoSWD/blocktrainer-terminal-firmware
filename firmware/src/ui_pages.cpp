// The nine pages. Each page builds its widgets into the content area, refreshes them in update()
// (once per second and after every data change) and deletes them in destroy().
//
// Layouts use percentages and flex so they fit both the full 320 px height and the 290 px left
// while the stream bar is shown.

#include <ArduinoJson.h>
#include <time.h>
#include <vector>
#include "ui_internal.h"
#include "hw.h"

static lv_obj_t *page_root(lv_obj_t *parent)
{
    lv_obj_t *r = mk_box(parent, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(r, 0, 0);
    return r;
}

static void del(lv_obj_t *&o)
{
    if (valid(o)) lv_obj_del(o);
    o = nullptr;
}

// Reads `live` under the lock; the pages copy what they need.
static LiveData snapshot()
{
    DataLock lock;
    LiveData copy;
    copy.price[0] = live.price[0]; copy.price[1] = live.price[1]; copy.price[2] = live.price[2];
    for (int i = 0; i < 3; i++) { copy.priceChange[i] = live.priceChange[i]; copy.chartChange[i] = live.chartChange[i]; }
    copy.satsPerUnit = live.satsPerUnit;
    copy.blockHeight = live.blockHeight;
    copy.feeLow = live.feeLow; copy.feeMid = live.feeMid; copy.feeHigh = live.feeHigh;
    memcpy(copy.feeCost, live.feeCost, sizeof(copy.feeCost));
    copy.diffProgress = live.diffProgress; copy.diffChange = live.diffChange; copy.diffBlocks = live.diffBlocks;
    copy.chartVersion = live.chartVersion;
    copy.dataVersion = live.dataVersion;
    return copy;
}

// Float to long without undefined behaviour for NaN, inf and values beyond the range of a 32-bit long.
static long to_long(double v)
{
    if (isnan(v)) return 0;
    if (v > 2e9) return 2000000000L;
    if (v < -2e9) return -2000000000L;
    return (long)v;
}

static String change_text(float pct, int decimals = 2)
{
    return String(pct >= 0 ? "+" : "") + fmt_num(pct, decimals) + "%";
}

static const char *timeframe_label()
{
    return cfg.chartTf == 3 ? "5d" : cfg.chartTf == 2 ? "3d" : "24h";
}

// ---------------------------------------------------------------- LED follows the price direction

static int s_led_dir = -1;      // -1 unset, 0 up, 1 down

void ui_page_reset_led() { s_led_dir = -1; }

static void led_for_change(float pct)
{
    int dir = pct < 0 ? 1 : 0;
    if (dir == s_led_dir) return;
    s_led_dir = dir;
    if (dir) led_solid(255, 0, 0);
    else led_solid(0, 128, 0);
}

// ================================================================ shared page helpers

static const int SPARK_SCALE = 1000;
static const int CHART_LOW = 150, CHART_HIGH = 850;       // value range the line spans, the rest is headroom for the price labels

static const lv_font_t *const FONTS_HERO[]  = { FONT_3XL, FONT_XL, FONT_L, FONT_M, FONT_S };
static const lv_font_t *const FONTS_PRICE[]  = { FONT_PRICE, FONT_3XL, FONT_XL, FONT_L, FONT_M };   // the first has digits and separators only
static const lv_font_t *const FONTS_SPLIT[] = { FONT_2XL, FONT_XL, FONT_L, FONT_M, FONT_S };
static const lv_font_t *const FONTS_NUM[]   = { FONT_HUGE, FONT_3XL, FONT_XL, FONT_L, FONT_M };   // digits and punctuation only
static const lv_font_t *const FONTS_TIER[]  = { FONT_XL, FONT_L, FONT_M, FONT_S };
static const lv_font_t *const FONTS_STAT[]  = { FONT_S, FONT_XS };

static const char *const WEEKDAYS[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

// Every page is a flex column that fills the page, so a change of the page height (stream bar) needs no arithmetic.
static lv_obj_t *page_col(lv_obj_t *root, int row_gap)
{
    lv_obj_t *col = mk_col(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(col, 10, 0);
    lv_obj_set_style_pad_row(col, row_gap, 0);
    return col;
}

// The big area in the middle of a page; takes the height the header and the cards leave.
// `framed` draws it as a rounded panel, otherwise the content sits straight on the black background.
static lv_obj_t *mk_hero(lv_obj_t *col, bool framed = true)
{
    lv_obj_t *p = mk_panel(col, PX_INNER_W, 10);
    lv_obj_set_flex_grow(p, 1);
    lv_obj_set_style_radius(p, 22, 0);
    if (!framed) {
        lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(p, 0, 0);
    }
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 10, 0);
    return p;
}

// Three equal info cards in a row.
static lv_obj_t *card_row3(lv_obj_t *col, const char *const caps[3], lv_obj_t *cap[3], lv_obj_t *val[3])
{
    const lv_coord_t gap = 8, cw = (PX_INNER_W - 2 * gap) / 3, ch = 58;
    lv_obj_t *row = mk_row_of_cards(col, ch);
    for (int i = 0; i < 3; i++) mk_info_card(row, cw, ch, caps[i], &cap[i], &val[i]);
    return row;
}

static lv_obj_t *mk_progress(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, w, h);
    lv_bar_set_range(b, 0, 1000);
    lv_bar_set_value(b, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(b, PX_EDGE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, PX_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    return b;
}

static void set_progress(lv_obj_t *bar, float pct)
{
    if (!valid(bar)) return;
    lv_bar_set_value(bar, isnan(pct) ? 0 : (int)constrain(pct * 10.0f, 0.0f, 1000.0f), LV_ANIM_OFF);
}

static void show_if(lv_obj_t *o, bool visible)
{
    if (!valid(o)) return;
    if (visible) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static String fmt_date(time_t t, const char *format = "%d %b %Y")
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    char b[32];
    strftime(b, sizeof(b), format, &tmv);
    return b;
}

// "3 d 5 h", "5 h 20 min", "40 min"
static String fmt_duration(long minutes)
{
    if (minutes < 0) minutes = 0;
    if (minutes >= 1440) return String(minutes / 1440) + " d " + String(minutes % 1440 / 60) + " h";
    if (minutes >= 60) return String(minutes / 60) + " h " + String(minutes % 60) + " min";
    return String(minutes) + " min";
}

// ================================================================ PRICE (price only)

struct PriceView {
    lv_obj_t *cap, *val, *unit, *tf, *dot;
    Chip chip;
};

static lv_obj_t *w_price_root;
static PriceView s_pv[2];
static bool s_price_split;

static int secondary_currency()
{
    if (!cfg.splitSecCur.isEmpty()) return currency_index(cfg.splitSecCur);
    int main = currency_index(cfg.currency);          // automatic: EUR -> USD -> CHF -> EUR
    return main == CUR_EUR ? CUR_USD : main == CUR_USD ? CUR_CHF : CUR_EUR;
}

// Price, unit and change chip of one currency, shared by the price and the chart page.
static void price_fill(PriceView &v, int cur, const LiveData &d, const lv_font_t *const *fonts, lv_coord_t max_w)
{
    float price = d.price[cur];
    if (v.cap) set_text(v.cap, String("BTC / ") + currency_code(cur));
    if (v.tf) set_text(v.tf, timeframe_label());
    fit_text(v.val, price > 0 ? fmt_int_sep(to_long(price + 0.5f), cfg.priceSeparator) : String("--"), fonts, 5, max_w);
    set_text(v.unit, price > 0 ? String(currency_code(cur)) : String(""));
    chip_set(v.chip, d.chartChange[cur], price > 0);
    if (v.dot) lv_obj_set_style_bg_color(v.dot, price > 0 ? (d.chartChange[cur] < 0 ? PX_DOWN : PX_UP) : PX_IDLE, 0);
}

static void price_build(lv_obj_t *parent)
{
    w_price_root = page_root(parent);
    s_price_split = cfg.priceSplit;
    memset(s_pv, 0, sizeof(s_pv));
    lv_obj_t *col = page_col(w_price_root, 10);

    if (!s_price_split) {
        Header h = mk_header(col, &ui_icon_btc, "Bitcoin", "BTC / USD");
        s_pv[0].cap = h.sub;

        lv_obj_t *hero = mk_hero(col, false);
        lv_obj_set_style_pad_row(hero, 12, 0);
        lv_obj_t *amount = mk_row(hero, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_align(amount, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(amount, 8, 0);
        s_pv[0].val = mk_label(amount, "--", FONT_PRICE, PX_TEXT);
        s_pv[0].unit = mk_label(amount, "USD", FONT_M, PX_DIM);
        lv_obj_set_style_pad_bottom(s_pv[0].unit, 5, 0);
        mk_chip(hero, s_pv[0].chip, FONT_S, 12, 4);
        return;
    }

    for (int i = 0; i < 2; i++) {
        PriceView &v = s_pv[i];
        lv_obj_t *card = mk_panel(col, PX_INNER_W, 10);
        lv_obj_set_flex_grow(card, 1);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_pad_hor(card, 18, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *left = mk_col(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(left, 6, 0);
        lv_obj_t *head = mk_row(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(head, 8, 0);
        lv_img_set_src(lv_img_create(head), &ui_icon_btc_s);
        v.cap = mk_label(head, "BTC / USD", FONT_XS, PX_DIM);
        v.tf = mk_label(head, "24h", FONT_XS, PX_IDLE);
        lv_obj_t *amount = mk_row(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_align(amount, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(amount, 6, 0);
        v.val = mk_label(amount, "--", FONT_2XL, PX_TEXT);
        v.unit = mk_label(amount, "USD", FONT_S, PX_DIM);
        lv_obj_set_style_pad_bottom(v.unit, 4, 0);
        mk_chip(card, v.chip, FONT_S, 12, 5);
    }
}

static void price_update()
{
    if (!valid(w_price_root)) return;
    net_update_chart_change(3000);              // the chart task only runs hourly; prices move more often
    LiveData d = snapshot();
    int main = currency_index(cfg.currency);

    if (s_price_split) {
        int cur[2] = { main, secondary_currency() };
        for (int i = 0; i < 2; i++) price_fill(s_pv[i], cur[i], d, FONTS_SPLIT, 190);
        led_for_change(d.chartChange[main]);
        return;
    }

    price_fill(s_pv[0], main, d, FONTS_PRICE, 400);
    led_for_change(d.chartChange[main]);
}

static void price_destroy()
{
    del(w_price_root);
    memset(s_pv, 0, sizeof(s_pv));
}

// ================================================================ CHART (price with history)

// Reads the points of the selected timeframe for `cur` out of live.chartJson:
// {"tf":{"24h":{"p":[[ts,EUR,USD,CHF],...]},"5d":{...}}}. False when there is nothing usable.
static bool chart_values(int tf, int cur, std::vector<float> &out, uint32_t *version)
{
    String json;
    {
        DataLock lock;
        json = live.chartJson;
        if (version) *version = live.chartVersion;
    }
    out.clear();
    if (json.isEmpty()) return false;

    DynamicJsonDocument doc(24576);
    if (deserializeJson(doc, json)) return false;
    JsonArray pts = doc["tf"][tf >= 2 ? "5d" : "24h"]["p"];
    if (pts.isNull() || pts.size() < 2) return false;

    size_t first = 0;
    if (tf == 2 && pts.size() > 72) first = pts.size() - 72;      // "3d" shows the last 72 points
    int field = cur == CUR_EUR ? 1 : cur == CUR_USD ? 2 : 3;
    for (size_t i = first; i < pts.size(); i++) {
        float v = pts[i][field] | 0.0f;
        out.push_back(isfinite(v) ? v : 0.0f);          // 1e39 in the JSON parses to inf
    }
    return true;
}

// Faded area under the line (after LVGL's chart example); the line itself comes from the series.
// Only the newest point gets a dot, with a glow.
static void spark_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || !dsc->rect_dsc) return;
    if (!dsc->p1 || !dsc->p2) {                         // LVGL passes no segment for the newest point
        lv_color_t col = dsc->rect_dsc->bg_color;       // series colour as ring and glow, white centre
        dsc->rect_dsc->bg_opa = LV_OPA_COVER;
        dsc->rect_dsc->bg_color = PX_TEXT;
        dsc->rect_dsc->border_width = 3;
        dsc->rect_dsc->border_color = col;
        dsc->rect_dsc->border_opa = LV_OPA_COVER;
        dsc->rect_dsc->shadow_width = 16;
        dsc->rect_dsc->shadow_spread = 2;
        dsc->rect_dsc->shadow_color = col;
        dsc->rect_dsc->shadow_opa = LV_OPA_60;
        return;
    }
    dsc->rect_dsc->bg_opa = LV_OPA_TRANSP;              // the point dsc is shared by all segments of the series

    lv_draw_mask_line_param_t line_mask;
    lv_draw_mask_line_points_init(&line_mask, dsc->p1->x, dsc->p1->y, dsc->p2->x, dsc->p2->y,
                                  LV_DRAW_MASK_LINE_SIDE_BOTTOM);
    int16_t line_id = lv_draw_mask_add(&line_mask, nullptr);

    lv_draw_mask_fade_param_t fade_mask;
    lv_draw_mask_fade_init(&fade_mask, &obj->coords, LV_OPA_COVER, obj->coords.y1 + lv_obj_get_height(obj) / 6,
                           LV_OPA_TRANSP, obj->coords.y2);
    int16_t fade_id = lv_draw_mask_add(&fade_mask, nullptr);

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_40;
    rect.bg_color = dsc->line_dsc->color;
    lv_area_t a;
    a.x1 = dsc->p1->x;
    a.x2 = dsc->p2->x;
    a.y1 = LV_MIN(dsc->p1->y, dsc->p2->y);
    a.y2 = obj->coords.y2;
    lv_draw_rect(dsc->draw_ctx, &rect, &a);

    lv_draw_mask_free_param(&line_mask);
    lv_draw_mask_free_param(&fade_mask);
    lv_draw_mask_remove_id(line_id);
    lv_draw_mask_remove_id(fade_id);
}

static lv_obj_t *w_chart_root, *w_chart, *w_chart_empty, *w_chart_time, *w_chart_date;
static lv_obj_t *w_chart_line[2], *w_chart_level[2];      // high and low marker, label on the right
static lv_chart_series_t *w_chart_series;
static PriceView s_cv;
static uint32_t s_chart_version = 0;
static int s_chart_cur = -1, s_chart_tf = -1;
static float s_chart_hi, s_chart_lo;
static bool s_chart_ok;
static int s_chart_dir = -1;                 // line colour is changed only when the direction flips (it redraws the whole chart)

static void chart_build(lv_obj_t *parent)
{
    w_chart_root = page_root(parent);
    s_chart_version = 0;
    s_chart_cur = s_chart_tf = -1;
    s_chart_ok = false;
    s_chart_dir = -1;
    memset(&s_cv, 0, sizeof(s_cv));
    memset(w_chart_line, 0, sizeof(w_chart_line));
    memset(w_chart_level, 0, sizeof(w_chart_level));

    lv_obj_t *col = page_col(w_chart_root, 6);
    Header h = mk_header(col, &ui_icon_btc, "Bitcoin", "BTC / USD");
    s_cv.cap = h.sub;
    lv_obj_t *clock = mk_col(h.right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(clock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    w_chart_time = mk_label(clock, "--:--", FONT_S, PX_TEXT);
    w_chart_date = mk_label(clock, "--", FONT_XS, PX_DIM);
    s_cv.tf = mk_live_pill(h.right, &s_cv.dot);

    lv_obj_t *prow = mk_row(col, PX_INNER_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *amount = mk_row(prow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(amount, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_flex_grow(amount, 1);                    // keeps the price at the left while the chip is hidden
    lv_obj_set_style_pad_column(amount, 8, 0);
    s_cv.val = mk_label(amount, "--", FONT_3XL, PX_TEXT);
    s_cv.unit = mk_label(amount, "USD", FONT_M, PX_DIM);
    lv_obj_set_style_pad_bottom(s_cv.unit, 5, 0);
    mk_chip(prow, s_cv.chip, FONT_S, 12, 5);

    lv_obj_t *box = mk_box(col, SCREEN_W, 10);          // full-bleed, takes the height that is left
    lv_obj_set_flex_grow(box, 1);
    w_chart = lv_chart_create(box);
    lv_obj_set_size(w_chart, LV_PCT(100), LV_PCT(100));
    lv_chart_set_type(w_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(w_chart, 3, 0);
    lv_obj_set_style_bg_opa(w_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w_chart, 0, 0);
    lv_obj_set_style_pad_all(w_chart, 0, 0);
    lv_obj_set_style_pad_right(w_chart, 12, 0);         // room for the end dot
    lv_obj_set_style_line_color(w_chart, PX_EDGE, LV_PART_MAIN);
    lv_obj_set_style_line_width(w_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_opa(w_chart, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_line_width(w_chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_size(w_chart, 12, LV_PART_INDICATOR);
    lv_obj_set_style_radius(w_chart, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_chart_set_range(w_chart, LV_CHART_AXIS_PRIMARY_Y, 0, SPARK_SCALE);
    lv_chart_set_point_count(w_chart, 2);
    w_chart_series = lv_chart_add_series(w_chart, PX_UP, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_event_cb(w_chart, spark_draw_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);
    w_chart_empty = mk_label(box, "no chart data", FONT_XS, PX_IDLE);
    lv_obj_center(w_chart_empty);
    lv_obj_add_flag(w_chart_empty, LV_OBJ_FLAG_HIDDEN);

    // High and low of the shown period: a hairline at the height of the extreme point, the price at its right end.
    // The percentages follow from the value scaling in chart_load (CHART_LOW .. CHART_HIGH of SPARK_SCALE).
    for (int i = 0; i < 2; i++) {
        int pct = (SPARK_SCALE - (i == 0 ? CHART_HIGH : CHART_LOW)) * 100 / SPARK_SCALE;
        w_chart_line[i] = mk_box(box, LV_PCT(100), 1);
        lv_obj_set_pos(w_chart_line[i], 0, lv_pct(pct));
        lv_obj_set_style_bg_color(w_chart_line[i], PX_DIM, 0);
        lv_obj_set_style_bg_opa(w_chart_line[i], LV_OPA_40, 0);
        w_chart_level[i] = mk_label(box, "", FONT_XS, PX_DIM);
        lv_obj_set_align(w_chart_level[i], LV_ALIGN_TOP_RIGHT);
        lv_obj_set_pos(w_chart_level[i], -14, lv_pct(pct));
        lv_obj_set_style_translate_y(w_chart_level[i], i == 0 ? -19 : 2, 0);     // high above its line, low below
    }
}

static void chart_load(int cur)
{
    std::vector<float> values;
    bool ok = chart_values(cfg.chartTf, cur, values, &s_chart_version);
    s_chart_cur = cur;
    s_chart_tf = cfg.chartTf;
    s_chart_ok = ok;
    show_if(w_chart, ok);
    show_if(w_chart_empty, !ok);
    for (int i = 0; i < 2; i++) { show_if(w_chart_line[i], ok); show_if(w_chart_level[i], ok); }
    if (!ok) return;

    float lo = values[0], hi = values[0];
    for (float v : values) { if (v < lo) lo = v; if (v > hi) hi = v; }
    s_chart_hi = hi;
    s_chart_lo = lo;
    double range = (double)hi - (double)lo;             // double: hi - lo overflows a float for +-3e38
    if (range < 1e-3) range = 1;
    lv_chart_set_point_count(w_chart, values.size());
    for (size_t i = 0; i < values.size(); i++)
        lv_chart_set_value_by_id(w_chart, w_chart_series, i, CHART_LOW + (lv_coord_t)(((double)values[i] - lo) / range * (CHART_HIGH - CHART_LOW)));
    lv_chart_refresh(w_chart);
}

static void chart_update()
{
    if (!valid(w_chart_root)) return;
    net_update_chart_change(3000);
    LiveData d = snapshot();
    int cur = currency_index(cfg.currency);
    if (d.chartVersion != s_chart_version || cur != s_chart_cur || cfg.chartTf != s_chart_tf) chart_load(cur);

    price_fill(s_cv, cur, d, FONTS_HERO, 236);
    int dir = d.chartChange[cur] < 0 ? 1 : 0;
    if (dir != s_chart_dir) {
        s_chart_dir = dir;
        lv_chart_set_series_color(w_chart, w_chart_series, dir ? PX_DOWN : PX_UP);
    }

    set_text(w_chart_level[0], s_chart_ok ? fmt_int_sep(to_long(s_chart_hi + 0.5f), cfg.priceSeparator) : String(""));
    set_text(w_chart_level[1], s_chart_ok ? fmt_int_sep(to_long(s_chart_lo + 0.5f), cfg.priceSeparator) : String(""));

    time_t now = time(nullptr);
    if (now > 1700000000) {
        set_text(w_chart_time, fmt_date(now, "%H:%M"));
        set_text(w_chart_date, fmt_date(now));
    }
    led_for_change(d.chartChange[cur]);
}

static void chart_destroy()
{
    del(w_chart_root);
    w_chart = w_chart_empty = w_chart_time = w_chart_date = nullptr;
    w_chart_series = nullptr;
    memset(&s_cv, 0, sizeof(s_cv));
    memset(w_chart_line, 0, sizeof(w_chart_line));
    memset(w_chart_level, 0, sizeof(w_chart_level));
}

// ================================================================ BLOCK

static const long HALVING_INTERVAL = 210000;

static lv_obj_t *w_block_root, *w_block_num, *w_block_bar, *w_block_epoch;
static lv_obj_t *w_block_cap[3], *w_block_val[3];

// Block subsidy in BTC, "3.125": integer satoshis shifted once per halving, zero after 33 halvings.
static String fmt_subsidy(long height)
{
    long halvings = height / HALVING_INTERVAL;
    long long sats = halvings >= 33 ? 0 : 5000000000LL >> halvings;
    char b[24];
    snprintf(b, sizeof(b), "%.8f", (double)sats / 1e8);
    int n = strlen(b);
    while (n > 1 && b[n - 1] == '0') b[--n] = 0;
    if (b[n - 1] == '.') b[--n] = 0;
    return b;
}

// Satoshis issued up to and including `height`: the subsidy of each completed halving era plus the part of the current one.
static long long supply_sats(long height)
{
    const long long MAX_SATS = 2099999997690000LL;
    long long total = 0, subsidy = 5000000000LL;
    long remaining = height + 1;                        // block 0 counts
    for (int era = 0; era < 33 && remaining > 0; era++, subsidy >>= 1) {
        long blocks = remaining < HALVING_INTERVAL ? remaining : HALVING_INTERVAL;
        total += blocks * subsidy;
        remaining -= blocks;
    }
    return total > MAX_SATS ? MAX_SATS : total;
}

static void block_build(lv_obj_t *parent)
{
    w_block_root = page_root(parent);
    memset(w_block_cap, 0, sizeof(w_block_cap));
    memset(w_block_val, 0, sizeof(w_block_val));
    lv_obj_t *col = page_col(w_block_root, 10);

    Header h = mk_header(col, &ui_icon_block, "Bitcoin", "BLOCK HEIGHT");
    mk_text_pill(h.right, "Mainnet");

    lv_obj_t *hero = mk_hero(col, false);
    lv_obj_set_style_pad_row(hero, 14, 0);
    w_block_num = mk_label(hero, "--", FONT_HUGE, PX_TEXT);
    lv_obj_t *prog = mk_col(hero, 340, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(prog, 8, 0);
    w_block_bar = mk_progress(prog, LV_PCT(100), 8);
    w_block_epoch = mk_label(prog, "", FONT_XS, PX_DIM);

    static const char *const caps[3] = { "REWARD", "TO HALVING", "EST. HALVING" };
    card_row3(col, caps, w_block_cap, w_block_val);
    led_solid(255, 120, 0);
}

static void block_update()
{
    if (!valid(w_block_root)) return;
    long height;
    {
        DataLock lock;
        height = live.blockHeight;
    }
    bool ok = height >= 1;
    fit_text(w_block_num, ok ? fmt_int_sep(height, true) : String("--"), FONTS_NUM, 5, 400);

    long into = height % HALVING_INTERVAL, left = HALVING_INTERVAL - into;
    long long mined = ok ? supply_sats(height) : 0;
    float mined_pct = mined / 2099999997690000.0f * 100.0f;
    set_progress(w_block_bar, mined_pct);
    set_text(w_block_epoch, ok ? fmt_num(mined / 1e14f, 2) + "M of 21M BTC mined  |  " + fmt_num(mined_pct, 1) + "%" : String(""));

    fit_text(w_block_val[0], ok ? fmt_subsidy(height) + " BTC" : String("--"), FONTS_STAT, 2, 124);
    fit_text(w_block_val[1], ok ? fmt_int_sep(left, true) + " blocks" : String("--"), FONTS_STAT, 2, 124);
    time_t now = time(nullptr);
    bool clock_ok = now > 1700000000;
    set_text(w_block_cap[0], "REWARD");
    set_text(w_block_cap[1], "TO HALVING");
    set_text(w_block_cap[2], "EST. HALVING");
    fit_text(w_block_val[2], ok && clock_ok ? fmt_date(now + (time_t)left * 600, "%b %Y") : String("--"), FONTS_STAT, 2, 124);
}

static void block_destroy()
{
    del(w_block_root);
    w_block_num = w_block_bar = w_block_epoch = nullptr;
    memset(w_block_cap, 0, sizeof(w_block_cap));
    memset(w_block_val, 0, sizeof(w_block_val));
}

// ================================================================ CLOCK

static lv_obj_t *w_clock_root, *w_clock_time, *w_clock_date, *w_clock_dot;
static lv_obj_t *w_clock_cap[3], *w_clock_val[3];

static void clock_build(lv_obj_t *parent)
{
    w_clock_root = page_root(parent);
    memset(w_clock_cap, 0, sizeof(w_clock_cap));
    memset(w_clock_val, 0, sizeof(w_clock_val));
    lv_obj_t *col = page_col(w_clock_root, 10);

    Header h = mk_header(col, &ui_icon_clock, "Clock", "LOCAL TIME");
    lv_label_set_text(mk_live_pill(h.right, &w_clock_dot), "Live");

    lv_obj_t *hero = mk_hero(col, false);
    lv_obj_set_style_pad_row(hero, 8, 0);
    w_clock_time = mk_label(hero, "--:--", FONT_HUGE, PX_TEXT);
    w_clock_date = mk_label(hero, "", FONT_M, PX_DIM);

    static const char *const caps[3] = { "WEEK", "DAY OF YEAR", "QUARTER" };
    card_row3(col, caps, w_clock_cap, w_clock_val);
    led_off();
}

static void clock_update()
{
    if (!valid(w_clock_root)) return;
    time_t now = time(nullptr);
    if (now < 1700000000) {                       // no NTP time yet
        set_text(w_clock_time, "--:--");
        set_text(w_clock_date, "Waiting for time sync...");
        for (int i = 0; i < 3; i++) set_text(w_clock_val[i], "--");
        lv_obj_set_style_bg_color(w_clock_dot, PX_IDLE, 0);
        return;
    }
    lv_obj_set_style_bg_color(w_clock_dot, PX_UP, 0);
    struct tm t;
    localtime_r(&now, &t);
    set_text(w_clock_time, fmt_date(now, "%H:%M"));
    set_text(w_clock_date, String(WEEKDAYS[t.tm_wday]) + ", " + fmt_date(now));
    int year = t.tm_year + 1900;
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    set_text(w_clock_val[0], fmt_date(now, "%V"));
    set_text(w_clock_val[1], String(t.tm_yday + 1) + " / " + String(leap ? 366 : 365));
    set_text(w_clock_val[2], String("Q") + String(t.tm_mon / 3 + 1));
}

static void clock_destroy()
{
    del(w_clock_root);
    w_clock_time = w_clock_date = w_clock_dot = nullptr;
    memset(w_clock_cap, 0, sizeof(w_clock_cap));
    memset(w_clock_val, 0, sizeof(w_clock_val));
}

// ================================================================ FEES

static lv_obj_t *w_fees_root, *w_fees_height;
static lv_obj_t *w_fee_val[3], *w_fee_cost[3];
static lv_obj_t *w_fees_cap[2], *w_fees_stat[2];

static void fees_build(lv_obj_t *parent)
{
    w_fees_root = page_root(parent);
    memset(w_fee_val, 0, sizeof(w_fee_val));
    memset(w_fee_cost, 0, sizeof(w_fee_cost));
    lv_obj_t *col = page_col(w_fees_root, 10);

    Header h = mk_header(col, &ui_icon_bolt, "Network fees", "SAT / VB");
    w_fees_height = mk_text_pill(h.right, "Block --");

    // three priority tiers
    const lv_coord_t gap = 8, cw = (PX_INNER_W - 2 * gap) / 3;
    lv_obj_t *tiers = mk_row(col, PX_INNER_W, 10);
    lv_obj_set_flex_grow(tiers, 1);
    lv_obj_set_flex_align(tiers, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    static const char *const NAMES[3] = { "LOW", "MEDIUM", "HIGH" };
    const lv_color_t colors[3] = { PX_UP, PX_ORANGE, PX_DOWN };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = mk_panel(tiers, cw, LV_PCT(100));
        lv_obj_set_style_radius(c, 18, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *tag = mk_pill(c, colors[i], LV_OPA_20, 12, 3);
        mk_label(tag, NAMES[i], FONT_XS, colors[i]);
        lv_obj_t *line = mk_row(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_align(line, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(line, 4, 0);
        w_fee_val[i] = mk_label(line, "--", FONT_XL, PX_TEXT);
        lv_obj_t *cost = mk_col(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(cost, 1, 0);
        w_fee_cost[i] = mk_label(cost, "--", FONT_S, colors[i]);
        mk_label(cost, "140 vB tx", FONT_XS, PX_IDLE);
    }

    const lv_coord_t hw = (PX_INNER_W - gap) / 2;
    lv_obj_t *row = mk_row_of_cards(col, 58);
    mk_info_card(row, hw, 58, "1 USD =", &w_fees_cap[0], &w_fees_stat[0]);
    mk_info_card(row, hw, 58, "BTC PRICE", &w_fees_cap[1], &w_fees_stat[1]);
}

static void fees_update()
{
    if (!valid(w_fees_root)) return;
    LiveData d = snapshot();
    int cur = currency_index(cfg.currency);
    float price = d.price[cur];
    set_text(w_fees_height, d.blockHeight > 0 ? String("Block ") + fmt_int_sep(d.blockHeight, true) : String("Block --"));
    float fee[3] = { d.feeLow, d.feeMid, d.feeHigh };
    for (int i = 0; i < 3; i++) {
        fit_text(w_fee_val[i], fmt_num(fee[i], 1), FONTS_TIER, 4, 100);
        fit_text(w_fee_cost[i], fmt_num(d.feeCost[cur][i], 2) + " " + currency_code(cur), FONTS_STAT, 2, 100);
    }
    set_text(w_fees_cap[0], String("1 ") + currency_code(cur) + " =");
    fit_text(w_fees_stat[0], price > 0 ? fmt_int_sep(to_long(1e8f / price), true) + " sats" : String("--"), FONTS_STAT, 2, 190);
    set_text(w_fees_cap[1], "BTC PRICE");
    fit_text(w_fees_stat[1], price > 0 ? fmt_int_sep(to_long(price + 0.5f), true) + " " + currency_code(cur) : String("--"), FONTS_STAT, 2, 190);
}

static void fees_destroy()
{
    del(w_fees_root);
    w_fees_height = nullptr;
    memset(w_fee_val, 0, sizeof(w_fee_val));
    memset(w_fee_cost, 0, sizeof(w_fee_cost));
}

// ================================================================ BITAXE

static lv_obj_t *w_bx_root, *w_bx_name, *w_bx_pill, *w_bx_dot, *w_bx_hash, *w_bx_unit;
static lv_obj_t *w_bx_diff_val[3], *w_bx_temp_val[3], *w_bx_temp_cap[3];
static uint32_t s_bx_device = 0, s_bx_switch_ms = 0;

static bool bitaxe_available()
{
    DataLock lock;
    return !g_bitaxe.empty();
}

static void bitaxe_build(lv_obj_t *parent)
{
    w_bx_root = page_root(parent);
    s_bx_device = 0;
    s_bx_switch_ms = millis();
    memset(w_bx_diff_val, 0, sizeof(w_bx_diff_val));
    memset(w_bx_temp_val, 0, sizeof(w_bx_temp_val));
    memset(w_bx_temp_cap, 0, sizeof(w_bx_temp_cap));
    lv_obj_t *col = page_col(w_bx_root, 10);

    Header h = mk_header(col, &ui_icon_chip, "Bitaxe", "MINER", 270);
    w_bx_name = h.title;
    w_bx_pill = mk_live_pill(h.right, &w_bx_dot);

    lv_obj_t *hero = mk_hero(col);
    lv_obj_set_style_pad_row(hero, 12, 0);
    lv_obj_t *hash = mk_row(hero, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(hash, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(hash, 8, 0);
    w_bx_hash = mk_label(hash, "--.--", FONT_3XL, PX_TEXT);
    w_bx_unit = mk_label(hash, "TH/s", FONT_M, PX_DIM);
    lv_obj_set_style_pad_bottom(w_bx_unit, 5, 0);

    lv_obj_t *diffs = mk_row(hero, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(diffs, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    static const char *const DIFF[3] = { "SESSION", "BEST", "NETWORK" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = mk_col(diffs, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(c, 2, 0);
        mk_label(c, DIFF[i], FONT_XS, PX_DIM);
        w_bx_diff_val[i] = mk_label(c, "--", FONT_S, PX_TEXT);
    }

    static const char *const caps[3] = { "ASIC TEMP", "VR TEMP", "FAN" };
    card_row3(col, caps, w_bx_temp_cap, w_bx_temp_val);
}

// Temperature text with a unit, coloured when the chip runs hot.
static void bx_temp(lv_obj_t *label, const String &value, const char *unit, float warn, float hot)
{
    if (value.isEmpty()) { set_text(label, "--"); lv_obj_set_style_text_color(label, PX_TEXT, 0); return; }
    float v = value.toFloat();
    set_text(label, value + unit);
    lv_obj_set_style_text_color(label, warn <= 0 ? PX_TEXT : v >= hot ? PX_DOWN : v >= warn ? PX_ORANGE : PX_TEXT, 0);
}

static void bitaxe_update()
{
    if (!valid(w_bx_root)) return;
    BitaxeDevice d;
    size_t count;
    {
        DataLock lock;
        count = g_bitaxe.size();
        if (count == 0) { set_text(w_bx_name, "No device"); return; }
        if (count > 1 && millis() - s_bx_switch_ms > 6000) {          // rotate through the miners
            s_bx_device = (s_bx_device + 1) % count;
            s_bx_switch_ms = millis();
        }
        if (s_bx_device >= count) s_bx_device = 0;
        d = g_bitaxe[s_bx_device];
    }
    set_text(w_bx_name, d.hostname.isEmpty() ? d.ip : d.hostname);
    bool online = !d.hashrate.isEmpty();
    String status = online ? "Online" : "Offline";
    if (count > 1) status += "  " + String(s_bx_device + 1) + "/" + String(count);
    set_text(w_bx_pill, status);
    lv_obj_set_style_bg_color(w_bx_dot, online ? PX_UP : PX_IDLE, 0);
    if (!online) {
        set_text(w_bx_hash, "--.--");
        set_text(w_bx_unit, "TH/s");
    } else if (d.hashGh >= 1000) {
        set_text(w_bx_hash, fmt_num(d.hashGh / 1000.0f, 2));
        set_text(w_bx_unit, "TH/s");
    } else {
        set_text(w_bx_hash, fmt_num(d.hashGh, 0));
        set_text(w_bx_unit, "GH/s");
    }
    const String *diffs[3] = { &d.sessionDiff, &d.bestDiff, &d.networkDiff };
    for (int i = 0; i < 3; i++) fit_text(w_bx_diff_val[i], diffs[i]->isEmpty() ? String("--") : *diffs[i], FONTS_STAT, 2, 120);
    bx_temp(w_bx_temp_val[0], d.temp, " \xC2\xB0" "C", 65, 75);
    bx_temp(w_bx_temp_val[1], d.vrTemp, " \xC2\xB0" "C", 75, 90);
    bx_temp(w_bx_temp_val[2], d.fan, " %", 0, 0);
}

static void bitaxe_destroy()
{
    del(w_bx_root);
    w_bx_name = w_bx_pill = w_bx_dot = w_bx_hash = w_bx_unit = nullptr;
    memset(w_bx_diff_val, 0, sizeof(w_bx_diff_val));
    memset(w_bx_temp_val, 0, sizeof(w_bx_temp_val));
    memset(w_bx_temp_cap, 0, sizeof(w_bx_temp_cap));
}

// ================================================================ CONVERTER (menu only)

static lv_obj_t *w_conv_root, *w_conv_field[4], *w_conv_value[4], *w_conv_saved;
static int s_conv_active;                 // 0 BTC, 1 SAT, 2 EUR, 3 USD
static String s_conv_input;               // digits typed into the active field
static const char *CONV_NAMES[4] = { "BTC", "SAT", "EUR", "USD" };

static double conv_btc_from_input()
{
    double v = s_conv_input.length() ? s_conv_input.toDouble() : 0.0;
    DataLock lock;
    switch (s_conv_active) {
        case 0: return v;
        case 1: return v / 1e8;
        case 2: return live.price[CUR_EUR] > 0 ? v / live.price[CUR_EUR] : 0;
        default: return live.price[CUR_USD] > 0 ? v / live.price[CUR_USD] : 0;
    }
}

static void conv_refresh()
{
    if (!valid(w_conv_root)) return;
    double btc = conv_btc_from_input();
    double eur, usd;
    {
        DataLock lock;
        eur = btc * live.price[CUR_EUR];
        usd = btc * live.price[CUR_USD];
    }
    String shown[4] = { String(btc, 8), String(btc * 1e8, 0), String(eur, 2), String(usd, 2) };
    for (int i = 0; i < 4; i++) {
        bool active = i == s_conv_active;
        String text = active ? (s_conv_input.isEmpty() ? String("0") : s_conv_input) : shown[i];
        fit_text(w_conv_value[i], text, FONTS_HERO + 3, 2, 150);       // FONT_M, then FONT_S for long amounts
        lv_obj_set_style_bg_color(w_conv_field[i], active ? PX_ORANGE : PX_CARD, 0);
        lv_obj_set_style_border_color(w_conv_field[i], active ? PX_ORANGE : PX_EDGE, 0);
        lv_obj_set_style_text_color(w_conv_value[i], active ? lv_color_black() : PX_TEXT, 0);
        lv_obj_t *name = lv_obj_get_child(w_conv_field[i], 0);      // caption: gray on orange is unreadable
        if (name) lv_obj_set_style_text_color(name, active ? lv_color_black() : PX_DIM, 0);
    }
}

static void conv_select(int field)
{
    if (field == s_conv_active) return;
    double btc = conv_btc_from_input();
    double values[4];
    {
        DataLock lock;
        values[0] = btc; values[1] = btc * 1e8; values[2] = btc * live.price[CUR_EUR]; values[3] = btc * live.price[CUR_USD];
    }
    s_conv_active = field;
    s_conv_input = field == 1 ? String(values[field], 0) : String(values[field], field == 0 ? 8 : 2);
    if (s_conv_input == "0" || s_conv_input.toDouble() == 0.0) s_conv_input = "";
    conv_refresh();
}

static void conv_key(const char *key)
{
    if (!strcmp(key, "C")) s_conv_input = "";
    else if (!strcmp(key, LV_SYMBOL_BACKSPACE)) { if (s_conv_input.length()) s_conv_input.remove(s_conv_input.length() - 1); }
    else if (!strcmp(key, ".")) { if (s_conv_input.indexOf('.') < 0) s_conv_input += s_conv_input.isEmpty() ? "0." : "."; }
    else if (s_conv_input.length() < 14) s_conv_input += key;
    conv_refresh();
}

static void conv_build(lv_obj_t *parent)
{
    w_conv_root = page_root(parent);
    s_conv_active = 2;                          // start in EUR like a price lookup
    s_conv_input = "";
    if (cfg.convSaved && cfg.convBtc > 0) {
        s_conv_active = 0;
        s_conv_input = String(cfg.convBtc, 8);
    }

    lv_obj_t *col = page_col(w_conv_root, 10);
    Header h = mk_header(col, &ui_icon_swap, "Converter", "");
    w_conv_saved = h.sub;
    if (cfg.convSaved) set_text(w_conv_saved, "Saved: " + String(cfg.convBtc, 8) + " BTC");
    else set_text(w_conv_saved, "BTC / SAT / EUR / USD");
    lv_obj_t *back = mk_button(h.right, LV_SYMBOL_LEFT, PX_BTN, PX_TEXT, 64, 34);
    lv_obj_add_event_cb(back, [](lv_event_t *) { ui_goto_page(ui_next_page(-1, +1)); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *body = mk_row(col, PX_INNER_W, 10);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *fields = mk_col(body, 214, LV_PCT(100));
    lv_obj_set_flex_align(fields, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(fields, 6, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *f = mk_panel(fields, LV_PCT(100), 10);
        lv_obj_set_flex_grow(f, 1);
        w_conv_field[i] = f;
        lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(f, 8, 0);
        lv_obj_add_event_cb(f, [](lv_event_t *e) { conv_select((int)(intptr_t)lv_event_get_user_data(e)); },
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *name = mk_label(f, CONV_NAMES[i], FONT_XS, PX_DIM);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 4, 0);
        w_conv_value[i] = mk_label(f, "0", FONT_M);
        lv_obj_align(w_conv_value[i], LV_ALIGN_BOTTOM_RIGHT, -4, 0);
    }

    lv_obj_t *right = mk_col(body, 220, LV_PCT(100));
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(right, 8, 0);
    static const char *KEYS[] = { "7", "8", "9", LV_SYMBOL_BACKSPACE, "\n", "4", "5", "6", "C", "\n", "1", "2", "3", ".", "\n", "0", "" };
    lv_obj_t *pad = lv_btnmatrix_create(right);
    lv_btnmatrix_set_map(pad, KEYS);
    lv_obj_set_width(pad, LV_PCT(100));
    lv_obj_set_flex_grow(pad, 1);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 0, 0);
    lv_obj_set_style_pad_row(pad, 6, 0);
    lv_obj_set_style_pad_column(pad, 6, 0);
    lv_obj_set_style_text_font(pad, FONT_M, LV_PART_ITEMS);
    lv_obj_set_style_text_color(pad, PX_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pad, PX_BTN, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(pad, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(pad, PX_EDGE, LV_PART_ITEMS);
    lv_obj_set_style_radius(pad, 12, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(pad, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pad, PX_EDGE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(pad, [](lv_event_t *e) {
        lv_obj_t *m = lv_event_get_target(e);
        uint32_t id = lv_btnmatrix_get_selected_btn(m);
        if (id != LV_BTNMATRIX_BTN_NONE) conv_key(lv_btnmatrix_get_btn_text(m, id));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *save = lv_btn_create(right);
    lv_obj_set_size(save, LV_PCT(100), 44);
    lv_obj_set_style_radius(save, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(save, PX_ORANGE, 0);
    lv_obj_set_style_shadow_width(save, 0, 0);
    lv_obj_center(mk_label(save, "SAVE", FONT_S, lv_color_black()));
    lv_obj_add_event_cb(save, [](lv_event_t *) {
        cfg.convBtc = conv_btc_from_input();
        cfg.convSaved = cfg.convBtc > 0;
        prefs.putBool("conv_saved", cfg.convSaved);
        prefs.putDouble("conv_btc", cfg.convBtc);
        set_text(w_conv_saved, "Saved: " + String(cfg.convBtc, 8) + " BTC");
    }, LV_EVENT_CLICKED, nullptr);

    conv_refresh();
    led_off();
}

static void conv_update() { conv_refresh(); }
static void conv_destroy() { del(w_conv_root); w_conv_saved = nullptr; }

// ================================================================ NEWS

static lv_obj_t *w_news_root, *w_news_head, *w_news_author, *w_news_date, *w_news_idx, *w_news_bar;
static size_t s_news_item = 0;
static uint32_t s_news_switch_ms = 0;
static const uint32_t NEWS_ITEM_MS = 9000;

// The bar under the headline fills over the time the item stays up.
static void news_restart_timer()
{
    if (!valid(w_news_bar)) return;
    lv_anim_del(w_news_bar, nullptr);
    lv_bar_set_value(w_news_bar, 0, LV_ANIM_OFF);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, w_news_bar);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_time(&a, NEWS_ITEM_MS);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_bar_set_value((lv_obj_t *)o, v, LV_ANIM_OFF); });
    lv_anim_start(&a);
}

static void news_build(lv_obj_t *parent)
{
    w_news_root = page_root(parent);
    s_news_item = 0;
    s_news_switch_ms = millis();
    lv_obj_t *col = page_col(w_news_root, 10);

    Header h = mk_header(col, &ui_icon_news, "News", "BLOCKTRAINER.DE");
    w_news_idx = mk_text_pill(h.right, "-- / --");

    lv_obj_t *hero = mk_hero(col);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(hero, 18, 0);
    lv_obj_set_style_pad_row(hero, 8, 0);
    w_news_head = mk_label(hero, "Loading news...", FONT_L, PX_TEXT);
    lv_label_set_long_mode(w_news_head, LV_LABEL_LONG_DOT);
    lv_obj_set_width(w_news_head, LV_PCT(100));
    lv_obj_set_flex_grow(w_news_head, 1);

    lv_obj_t *foot = mk_row(hero, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    w_news_author = mk_label(foot, "", FONT_S, PX_DIM);
    lv_label_set_long_mode(w_news_author, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(w_news_author, 220, 0);
    w_news_date = mk_label(foot, "", FONT_XS, PX_IDLE);
    w_news_bar = mk_progress(hero, LV_PCT(100), 4);
    lv_obj_add_flag(w_news_bar, LV_OBJ_FLAG_HIDDEN);       // only while there is more than one item
    news_restart_timer();
    led_off();
}

static void news_update()
{
    if (!valid(w_news_root)) return;
    NewsItem item;
    size_t count;
    {
        DataLock lock;
        count = g_news.size();
        if (count == 0) return;
        if (count > 1 && millis() - s_news_switch_ms > NEWS_ITEM_MS) {
            s_news_item = (s_news_item + 1) % count;
            s_news_switch_ms = millis();
            news_restart_timer();
        }
        if (s_news_item >= count) s_news_item = 0;
        item = g_news[s_news_item];
    }
    set_text(w_news_head, item.headline);
    set_text(w_news_idx, String(s_news_item + 1) + " / " + String(count));
    set_text(w_news_author, item.author);
    set_text(w_news_date, item.ts > 1700000000 ? fmt_date(item.ts, "%d %b %Y  %H:%M") : String(""));
    show_if(w_news_bar, count > 1);
}

static void news_destroy() { del(w_news_root); w_news_head = w_news_bar = nullptr; }

// ================================================================ DIFFICULTY

static lv_obj_t *w_diff_root, *w_diff_change, *w_diff_bar, *w_diff_note;
static lv_obj_t *w_diff_cap[3], *w_diff_val[3];

static void diff_build(lv_obj_t *parent)
{
    w_diff_root = page_root(parent);
    memset(w_diff_cap, 0, sizeof(w_diff_cap));
    memset(w_diff_val, 0, sizeof(w_diff_val));
    lv_obj_t *col = page_col(w_diff_root, 10);

    Header h = mk_header(col, &ui_icon_bars, "Difficulty", "NEXT ADJUSTMENT");
    mk_text_pill(h.right, "Every 2016 blocks");

    lv_obj_t *hero = mk_hero(col, false);
    lv_obj_set_style_pad_row(hero, 12, 0);
    mk_label(hero, "EXPECTED CHANGE", FONT_XS, PX_DIM);
    w_diff_change = mk_label(hero, "--", FONT_3XL, PX_UP);
    lv_obj_t *prog = mk_col(hero, 340, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(prog, 8, 0);
    w_diff_bar = mk_progress(prog, LV_PCT(100), 8);
    w_diff_note = mk_label(prog, "", FONT_XS, PX_DIM);

    static const char *const caps[3] = { "BLOCKS LEFT", "TIME LEFT", "ADJUSTMENT" };
    card_row3(col, caps, w_diff_cap, w_diff_val);
}

static void diff_update()
{
    if (!valid(w_diff_root)) return;
    LiveData d = snapshot();
    float change = isnan(d.diffChange) ? 0 : d.diffChange;
    bool ok = d.diffBlocks > 0 || d.diffProgress > 0;
    fit_text(w_diff_change, ok ? change_text(change) : String("--"), FONTS_HERO, 5, 400);
    lv_obj_set_style_text_color(w_diff_change, change < 0 ? PX_DOWN : PX_UP, 0);
    set_progress(w_diff_bar, d.diffProgress);
    set_text(w_diff_note, ok ? fmt_num(d.diffProgress, 1) + "% of the epoch" : String(""));
    long minutes = (long)constrain(d.diffBlocks, 0, 100000) * 10;      // time_t is 32 bit on the device: keep the date arithmetic small
    time_t now = time(nullptr);
    fit_text(w_diff_val[0], ok ? fmt_int_sep(d.diffBlocks, true) : String("--"), FONTS_STAT, 2, 124);
    fit_text(w_diff_val[1], ok ? "~" + fmt_duration(minutes) : String("--"), FONTS_STAT, 2, 124);
    fit_text(w_diff_val[2], ok && now > 1700000000 ? fmt_date(now + (time_t)minutes * 60, "%d %b") : String("--"), FONTS_STAT, 2, 124);
    led_for_change(d.diffChange);
}

static void diff_destroy()
{
    del(w_diff_root);
    w_diff_change = w_diff_bar = w_diff_note = nullptr;
    memset(w_diff_cap, 0, sizeof(w_diff_cap));
    memset(w_diff_val, 0, sizeof(w_diff_val));
}

// ================================================================ table

const PageOps PAGE_OPS[PAGE_COUNT] = {
    /* PAGE_PRICE  */ { nullptr,           price_build,  price_update,  price_destroy },
    /* PAGE_CHART  */ { nullptr,           chart_build,  chart_update,  chart_destroy },
    /* PAGE_BLOCK  */ { nullptr,           block_build,  block_update,  block_destroy },
    /* PAGE_CLOCK  */ { nullptr,           clock_build,  clock_update,  clock_destroy },
    /* PAGE_BITAXE */ { bitaxe_available,  bitaxe_build, bitaxe_update, bitaxe_destroy },
    /* PAGE_FEES   */ { nullptr,           fees_build,   fees_update,   fees_destroy },
    /* PAGE_CONV   */ { nullptr,           conv_build,   conv_update,   conv_destroy },
    /* PAGE_NEWS   */ { nullptr,           news_build,   news_update,   news_destroy },
    /* PAGE_DIFF   */ { nullptr,           diff_build,   diff_update,   diff_destroy },
};
