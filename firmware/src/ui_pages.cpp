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
    return String(pct >= 0 ? "+" : "") + fmt_de(pct, decimals) + " %";
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

// ================================================================ PREIS

static lv_obj_t *w_price_root;
static lv_obj_t *w_price_val[2], *w_price_chg[2], *w_price_cur[2], *w_price_tf[2], *w_price_sats;
static bool s_price_split;

static int secondary_currency()
{
    if (!cfg.splitSecCur.isEmpty()) return currency_index(cfg.splitSecCur);
    int main = currency_index(cfg.currency);          // automatic: EUR -> USD -> CHF -> EUR
    return main == CUR_EUR ? CUR_USD : main == CUR_USD ? CUR_CHF : CUR_EUR;
}

static void price_build(lv_obj_t *parent)
{
    w_price_root = page_root(parent);
    s_price_split = cfg.priceSplit;
    int rows = s_price_split ? 2 : 1;

    lv_obj_t *col = mk_col(w_price_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < rows; i++) {
        lv_obj_t *block = mk_col(col, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(block, 2, 0);
        lv_obj_t *head = mk_row(block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(head, 8, 0);
        w_price_cur[i] = mk_label(head, "BTC / USD", s_price_split ? FONT_S : FONT_M, COL_LABEL);
        w_price_val[i] = mk_label(block, "--", s_price_split ? FONT_2XL : FONT_HUGE);
        lv_obj_t *foot = mk_row(block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(foot, 10, 0);
        w_price_chg[i] = mk_label(foot, "--", s_price_split ? FONT_S : FONT_L, COL_GREEN);
        w_price_tf[i] = mk_label(foot, "24h", s_price_split ? FONT_XS : FONT_S, COL_GRAY);
    }
    w_price_sats = s_price_split ? nullptr : mk_label(col, "", FONT_S, COL_GRAY);
}

static void price_update()
{
    if (!valid(w_price_root)) return;
    net_update_chart_change(3000);              // the chart task only runs hourly; prices move more often
    LiveData d = snapshot();
    int cur[2] = { currency_index(cfg.currency), secondary_currency() };
    int rows = s_price_split ? 2 : 1;
    for (int i = 0; i < rows; i++) {
        float price = d.price[cur[i]];
        float chg = d.chartChange[cur[i]];
        String value = price > 0 ? fmt_int_sep(to_long(price + 0.5f), cfg.priceSeparator) : String("--");
        if (cur[i] == CUR_USD && price > 0) value = "$ " + value;
        set_text(w_price_cur[i], String("BTC / ") + currency_code(cur[i]));
        set_text(w_price_val[i], cur[i] == CUR_USD ? value : (price > 0 ? value + " " + currency_code(cur[i]) : value));
        set_text(w_price_chg[i], change_text(chg));
        lv_obj_set_style_text_color(w_price_chg[i], chg < 0 ? COL_RED : COL_GREEN, 0);
        set_text(w_price_tf[i], timeframe_label());
    }
    if (w_price_sats) {
        float price = d.price[cur[0]];
        set_text(w_price_sats, price > 0 ? String("1 ") + currency_code(cur[0]) + " = " +
                                           fmt_int_sep(to_long(1e8f / price), cfg.priceSeparator) + " sats" : String(""));
    }
    led_for_change(d.chartChange[cur[0]]);
}

static void price_destroy() { del(w_price_root); }

// ================================================================ CHART

static lv_obj_t *w_chart_root, *w_chart, *w_chart_price, *w_chart_chg, *w_chart_tf, *w_chart_time, *w_chart_date;
static lv_chart_series_t *w_chart_series;
static uint32_t s_chart_version = 0;
static int s_chart_cur = -1, s_chart_tf = -1;
static const int CHART_SCALE = 1000;

static void chart_build(lv_obj_t *parent)
{
    w_chart_root = page_root(parent);
    s_chart_version = 0;
    s_chart_cur = s_chart_tf = -1;

    lv_obj_t *top = mk_row(w_chart_root, LV_PCT(100), 74);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(top, 12, 0);
    lv_obj_set_style_pad_right(top, 12, 0);

    // both side cards get the same width, otherwise the price is not centred ("24h" vs "5d")
    lv_obj_t *tf = mk_card(top, 80, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(tf, 8, 0);
    w_chart_tf = mk_label(tf, "24h", FONT_S);
    lv_obj_set_width(w_chart_tf, LV_PCT(100));
    lv_obj_set_style_text_align(w_chart_tf, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *price = mk_col(top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(price, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    w_chart_price = mk_label(price, "--", FONT_2XL);
    w_chart_chg = mk_label(price, "--", FONT_S, COL_GREEN);

    lv_obj_t *clock = mk_card(top, 80, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(clock, 6, 0);
    w_chart_time = mk_label(clock, "--:--", FONT_S);
    w_chart_date = mk_label(clock, "--.--.----", FONT_XS);

    w_chart = lv_chart_create(w_chart_root);
    lv_obj_set_size(w_chart, SCREEN_W - 20, LV_PCT(100));
    lv_obj_set_height(w_chart, lv_obj_get_height(parent) - 84);
    lv_obj_align(w_chart, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_chart_set_type(w_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(w_chart, 4, 0);
    lv_obj_set_style_bg_opa(w_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w_chart, 0, 0);
    lv_obj_set_style_line_color(w_chart, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_size(w_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(w_chart, 3, LV_PART_ITEMS);
    lv_chart_set_range(w_chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_SCALE);
    lv_chart_set_point_count(w_chart, 2);
    w_chart_series = lv_chart_add_series(w_chart, COL_ORANGE, LV_CHART_AXIS_PRIMARY_Y);
}

// Fills the chart from live.chartJson: {"tf":{"24h":{"p":[[ts,EUR,USD,CHF],...]},"5d":{...}}}.
static void chart_load(int cur)
{
    String json;
    {
        DataLock lock;
        json = live.chartJson;
        s_chart_version = live.chartVersion;
    }
    s_chart_cur = cur;
    s_chart_tf = cfg.chartTf;
    if (json.isEmpty()) return;

    DynamicJsonDocument doc(24576);
    if (deserializeJson(doc, json)) return;
    JsonArray pts = doc["tf"][cfg.chartTf >= 2 ? "5d" : "24h"]["p"];
    if (pts.isNull() || pts.size() < 2) return;

    std::vector<float> values;
    size_t first = 0;
    if (cfg.chartTf == 2 && pts.size() > 72) first = pts.size() - 72;      // "3d" shows the last 72 points
    int field = cur == CUR_EUR ? 1 : cur == CUR_USD ? 2 : 3;
    for (size_t i = first; i < pts.size(); i++) {
        float v = pts[i][field] | 0.0f;
        values.push_back(isfinite(v) ? v : 0.0f);       // 1e39 in the JSON parses to inf
    }

    float lo = values[0], hi = values[0];
    for (float v : values) { if (v < lo) lo = v; if (v > hi) hi = v; }
    double range = (double)hi - (double)lo;             // double: hi - lo overflows a float for +-3e38
    if (range < 1e-3) range = 1;
    lv_chart_set_point_count(w_chart, values.size());
    for (size_t i = 0; i < values.size(); i++)
        lv_chart_set_value_by_id(w_chart, w_chart_series, i, (lv_coord_t)(((double)values[i] - lo) / range * CHART_SCALE));
    lv_chart_refresh(w_chart);
}

static void chart_update()
{
    if (!valid(w_chart_root)) return;
    net_update_chart_change(3000);
    LiveData d = snapshot();
    int cur = currency_index(cfg.currency);
    if (d.chartVersion != s_chart_version || cur != s_chart_cur || cfg.chartTf != s_chart_tf) chart_load(cur);

    float price = d.price[cur];
    set_text(w_chart_price, price > 0 ? (cur == CUR_USD ? "$ " : "") + fmt_int_sep(to_long(price + 0.5f), cfg.priceSeparator) +
                                        (cur == CUR_USD ? "" : String(" ") + currency_code(cur)) : String("--"));
    set_text(w_chart_chg, change_text(d.chartChange[cur]));
    lv_obj_set_style_text_color(w_chart_chg, d.chartChange[cur] < 0 ? COL_RED : COL_GREEN, 0);
    set_text(w_chart_tf, timeframe_label());

    time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm t;
        localtime_r(&now, &t);
        char tb[8], db[12];
        strftime(tb, sizeof(tb), "%H:%M", &t);
        strftime(db, sizeof(db), "%d.%m.%Y", &t);
        set_text(w_chart_time, tb);
        set_text(w_chart_date, db);
    }
    led_for_change(d.chartChange[cur]);
}

static void chart_destroy() { del(w_chart_root); w_chart = nullptr; }

// ================================================================ BLOCK

static lv_obj_t *w_block_root, *w_block_label;

static void block_build(lv_obj_t *parent)
{
    w_block_root = page_root(parent);
    lv_obj_t *col = mk_col(w_block_root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_row(col, 6, 0);
    mk_label(col, "BLOCKHOEHE", FONT_S, COL_LABEL);
    w_block_label = mk_label(col, "000000", FONT_HUGE);
    led_solid(255, 120, 0);
}

static void block_update()
{
    if (!valid(w_block_label)) return;
    DataLock lock;
    set_text(w_block_label, live.blockHeight >= 1 ? fmt_int_sep(live.blockHeight, false) : String("000000"));
}

static void block_destroy() { del(w_block_root); w_block_label = nullptr; }

// ================================================================ UHR

static lv_obj_t *w_clock_root, *w_clock_time, *w_clock_weekday, *w_clock_date;

static void clock_build(lv_obj_t *parent)
{
    w_clock_root = page_root(parent);
    lv_obj_t *col = mk_col(w_clock_root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_row(col, 8, 0);
    w_clock_time = mk_label(col, "00:00", FONT_HUGE);
    lv_obj_t *row = mk_row(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(row, 16, 0);
    w_clock_weekday = mk_label(row, "", FONT_L);
    lv_obj_t *sep = mk_box(row, 3, 28);
    lv_obj_set_style_radius(sep, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sep, COL_WHITE, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    w_clock_date = mk_label(row, "", FONT_L);
    led_off();
}

static void clock_update()
{
    if (!valid(w_clock_time)) return;
    static const char *DAYS[7] = { "SONNTAG", "MONTAG", "DIENSTAG", "MITTWOCH", "DONNERSTAG", "FREITAG", "SAMSTAG" };
    time_t now = time(nullptr);
    if (now < 1700000000) {                       // no NTP time yet
        set_text(w_clock_time, "00:00");
        set_text(w_clock_date, "01.01.1970");
        set_text(w_clock_weekday, "UNBEKANNT");
        return;
    }
    struct tm t;
    localtime_r(&now, &t);
    char tb[8], db[12];
    strftime(tb, sizeof(tb), "%H:%M", &t);
    strftime(db, sizeof(db), "%d.%m.%Y", &t);
    set_text(w_clock_time, tb);
    set_text(w_clock_date, db);
    set_text(w_clock_weekday, DAYS[t.tm_wday]);
}

static void clock_destroy() { del(w_clock_root); w_clock_time = nullptr; }

// ================================================================ FEES

static lv_obj_t *w_fees_root, *w_fees_height, *w_fees_sats, *w_fees_cur;
static lv_obj_t *w_fee_val[3], *w_fee_cost[3];

static void fees_build(lv_obj_t *parent)
{
    w_fees_root = page_root(parent);
    lv_obj_set_flex_flow(w_fees_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(w_fees_root, 8, 0);
    lv_obj_set_style_pad_row(w_fees_root, 8, 0);

    lv_obj_t *top = mk_box(w_fees_root, LV_PCT(100), LV_PCT(45));
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(top, 8, 0);

    lv_obj_t *left = mk_card(top, LV_PCT(60), LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    mk_label(left, "BLOCKZEIT", FONT_S, COL_LABEL);
    w_fees_height = mk_label(left, "", FONT_2XL);

    lv_obj_t *right = mk_card(top, LV_PCT(38), LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *cap = mk_row(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(cap, 6, 0);
    mk_label(cap, "SATS / 1", FONT_XS, COL_LABEL);
    w_fees_cur = mk_label(cap, "", FONT_XS, COL_LABEL);
    w_fees_sats = mk_label(right, "", FONT_2XL);

    lv_obj_t *bottom = mk_card(w_fees_root, LV_PCT(100), LV_PCT(50));
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(bottom, 8, 0);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    mk_label(bottom, "TRANSAKTIONSGEBUEHREN", FONT_S, COL_LABEL);
    lv_obj_t *cols = mk_row(bottom, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(cols, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    static const char *NAMES[3] = { "NIEDRIGE\nPRIORITAET", "MITTLERE\nPRIORITAET", "HOHE\nPRIORITAET" };   // two lines: the column is too narrow for one
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = mk_col(cols, LV_PCT(32), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(c, 3, 0);
        lv_obj_t *cap = mk_label(c, NAMES[i], FONT_XS, COL_GRAY);
        lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *line = mk_row(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(line, 4, 0);
        w_fee_val[i] = mk_label(line, "", FONT_L);
        mk_label(line, "sat/vB", FONT_XS, COL_GRAY);
        w_fee_cost[i] = mk_label(c, "", FONT_S, COL_GREEN);
    }
}

static void fees_update()
{
    if (!valid(w_fees_root)) return;
    LiveData d = snapshot();
    int cur = currency_index(cfg.currency);
    float price = d.price[cur];
    set_text(w_fees_height, fmt_int_sep(d.blockHeight, false));
    set_text(w_fees_cur, cfg.currency);
    set_text(w_fees_sats, price > 0 ? fmt_int_sep(to_long(1e8f / price), false) : String("--"));
    float fee[3] = { d.feeLow, d.feeMid, d.feeHigh };
    for (int i = 0; i < 3; i++) {
        set_text(w_fee_val[i], fmt_de(fee[i], 1));
        set_text(w_fee_cost[i], fmt_de(d.feeCost[cur][i], 2) + " " + currency_symbol(cur));
    }
}

static void fees_destroy() { del(w_fees_root); w_fees_height = nullptr; }

// ================================================================ BITAXE

static lv_obj_t *w_bx_root, *w_bx_name, *w_bx_hash, *w_bx_unit, *w_bx_asic, *w_bx_vr, *w_bx_fan, *w_bx_diff, *w_bx_idx;
static uint32_t s_bx_device = 0, s_bx_switch_ms = 0;

static bool bitaxe_available()
{
    DataLock lock;
    return !g_bitaxe.empty();
}

static lv_obj_t *mk_stat(lv_obj_t *parent, const char *caption, const char *unit, lv_obj_t **value)
{
    lv_obj_t *c = mk_card(parent, LV_PCT(32), LV_PCT(100));
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    mk_label(c, caption, FONT_XS, COL_LABEL);
    lv_obj_t *row = mk_row(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(row, 4, 0);
    *value = mk_label(row, "--", FONT_XL);
    mk_label(row, unit, FONT_S, COL_GRAY);
    return c;
}

static void bitaxe_build(lv_obj_t *parent)
{
    w_bx_root = page_root(parent);
    s_bx_device = 0;
    s_bx_switch_ms = millis();
    lv_obj_set_flex_flow(w_bx_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w_bx_root, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(w_bx_root, 8, 0);

    lv_obj_t *head = mk_row(w_bx_root, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    w_bx_name = mk_label(head, "BITAXE", FONT_S, COL_LABEL);
    w_bx_idx = mk_label(head, "", FONT_S, COL_GRAY);

    lv_obj_t *hash = mk_row(w_bx_root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(hash, 8, 0);
    w_bx_hash = mk_label(hash, "--.--", FONT_HUGE);
    w_bx_unit = mk_label(hash, "TH/s", FONT_M, COL_GRAY);

    lv_obj_t *stats = mk_row(w_bx_root, LV_PCT(100), LV_PCT(28));
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    mk_stat(stats, "ASIC TEMP.", "\xC2\xB0" "C", &w_bx_asic);
    mk_stat(stats, "VOLT REG. TEMP.", "\xC2\xB0" "C", &w_bx_vr);
    mk_stat(stats, "LUEFTER", "%", &w_bx_fan);

    w_bx_diff = mk_label(w_bx_root, "", FONT_XS, COL_GRAY);
    lv_label_set_recolor(w_bx_diff, true);
}

static void bitaxe_update()
{
    if (!valid(w_bx_root)) return;
    BitaxeDevice d;
    size_t count;
    {
        DataLock lock;
        count = g_bitaxe.size();
        if (count == 0) { set_text(w_bx_name, "Kein Geraet"); return; }
        if (count > 1 && millis() - s_bx_switch_ms > 6000) {          // rotate through the miners
            s_bx_device = (s_bx_device + 1) % count;
            s_bx_switch_ms = millis();
        }
        if (s_bx_device >= count) s_bx_device = 0;
        d = g_bitaxe[s_bx_device];
    }
    set_text(w_bx_name, d.hostname.isEmpty() ? d.ip : d.hostname);
    set_text(w_bx_idx, count > 1 ? String(s_bx_device + 1) + " / " + String(count) : String(""));
    if (d.hashrate.isEmpty()) {
        set_text(w_bx_hash, "--.--");
        set_text(w_bx_unit, "TH/s");
    } else if (d.hashGh >= 1000) {
        set_text(w_bx_hash, fmt_de(d.hashGh / 1000.0f, 2));
        set_text(w_bx_unit, "TH/s");
    } else {
        set_text(w_bx_hash, fmt_de(d.hashGh, 0));
        set_text(w_bx_unit, "GH/s");
    }
    set_text(w_bx_asic, d.temp.isEmpty() ? String("--") : d.temp);
    set_text(w_bx_vr, d.vrTemp.isEmpty() ? String("--") : d.vrTemp);
    set_text(w_bx_fan, d.fan.isEmpty() ? String("--") : d.fan);
    set_text(w_bx_diff, "#7d7d7d Sess:# " + (d.sessionDiff.isEmpty() ? String("--") : d.sessionDiff) +
                        "   |   #7d7d7d Best:# " + (d.bestDiff.isEmpty() ? String("--") : d.bestDiff) +
                        "   |   #7d7d7d Net:# " + (d.networkDiff.isEmpty() ? String("--") : d.networkDiff));
}

static void bitaxe_destroy() { del(w_bx_root); w_bx_hash = nullptr; }

// ================================================================ RECHNER (converter, menu only)

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

static String conv_format(double v, int decimals)
{
    String s = String(v, decimals);
    s.replace('.', ',');
    return s;
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
    String shown[4] = { conv_format(btc, 8), conv_format(btc * 1e8, 0), conv_format(eur, 2), conv_format(usd, 2) };
    for (int i = 0; i < 4; i++) {
        String text = i == s_conv_active ? (s_conv_input.isEmpty() ? String("0") : s_conv_input) : shown[i];
        text.replace('.', ',');
        set_text(w_conv_value[i], text);
        lv_obj_set_style_bg_color(w_conv_field[i], i == s_conv_active ? COL_ORANGE : COL_PANEL, 0);
        lv_obj_set_style_text_color(w_conv_value[i], i == s_conv_active ? lv_color_black() : COL_WHITE, 0);
        lv_obj_t *name = lv_obj_get_child(w_conv_field[i], 0);      // caption: gray on orange is unreadable
        if (name) lv_obj_set_style_text_color(name, i == s_conv_active ? lv_color_black() : COL_LABEL, 0);
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
    else if (!strcmp(key, "<")) { if (s_conv_input.length()) s_conv_input.remove(s_conv_input.length() - 1); }
    else if (!strcmp(key, ",")) { if (s_conv_input.indexOf('.') < 0) s_conv_input += s_conv_input.isEmpty() ? "0." : "."; }
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

    lv_obj_t *title = mk_label(w_conv_root, "RECHNER", FONT_S, COL_LABEL);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    lv_obj_t *back = lv_btn_create(w_conv_root);
    lv_obj_set_size(back, 60, 32);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_set_style_bg_color(back, COL_BTN, 0);
    lv_obj_center(mk_label(back, LV_SYMBOL_LEFT, FONT_S));
    lv_obj_add_event_cb(back, [](lv_event_t *) { ui_goto_page(ui_next_page(-1, +1)); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *fields = mk_col(w_conv_root, 220, LV_PCT(100));
    lv_obj_set_pos(fields, 8, 40);
    lv_obj_set_height(fields, lv_obj_get_height(parent) - 46);
    lv_obj_set_flex_align(fields, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(fields, 6, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *f = mk_card(fields, LV_PCT(100), 52);
        w_conv_field[i] = f;
        lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_left(f, 10, 0);
        lv_obj_add_event_cb(f, [](lv_event_t *e) { conv_select((int)(intptr_t)lv_event_get_user_data(e)); },
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *name = mk_label(f, CONV_NAMES[i], FONT_XS, COL_LABEL);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 4);
        w_conv_value[i] = mk_label(f, "0", FONT_M);
        lv_obj_align(w_conv_value[i], LV_ALIGN_BOTTOM_RIGHT, -10, -2);
    }

    static const char *KEYS[] = { "7", "8", "9", "<", "\n", "4", "5", "6", "C", "\n", "1", "2", "3", ",", "\n", "0", "" };
    lv_obj_t *pad = lv_btnmatrix_create(w_conv_root);
    lv_btnmatrix_set_map(pad, KEYS);
    lv_obj_set_size(pad, 230, lv_obj_get_height(parent) - 100);
    lv_obj_set_pos(pad, 242, 44);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 0, 0);
    lv_obj_set_style_text_font(pad, FONT_M, LV_PART_ITEMS);
    lv_obj_set_style_text_color(pad, COL_WHITE, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pad, COL_BTN, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_add_event_cb(pad, [](lv_event_t *e) {
        lv_obj_t *m = lv_event_get_target(e);
        uint32_t id = lv_btnmatrix_get_selected_btn(m);
        if (id != LV_BTNMATRIX_BTN_NONE) conv_key(lv_btnmatrix_get_btn_text(m, id));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *save = lv_btn_create(w_conv_root);
    lv_obj_set_size(save, 230, 44);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    lv_obj_set_style_bg_color(save, COL_ORANGE, 0);
    lv_obj_center(mk_label(save, "SPEICHERN", FONT_S));
    lv_obj_add_event_cb(save, [](lv_event_t *) {
        cfg.convBtc = conv_btc_from_input();
        cfg.convSaved = cfg.convBtc > 0;
        prefs.putBool("conv_saved", cfg.convSaved);
        prefs.putDouble("conv_btc", cfg.convBtc);
        set_text(w_conv_saved, "gespeichert: " + conv_format(cfg.convBtc, 8) + " BTC");
    }, LV_EVENT_CLICKED, nullptr);

    w_conv_saved = mk_label(w_conv_root, cfg.convSaved ? ("gespeichert: " + conv_format(cfg.convBtc, 8) + " BTC").c_str() : "", FONT_XS, COL_GRAY);
    lv_obj_align(w_conv_saved, LV_ALIGN_TOP_MID, 20, 12);
    conv_refresh();
    led_off();
}

static void conv_update() { conv_refresh(); }
static void conv_destroy() { del(w_conv_root); }

// ================================================================ NEWS

static lv_obj_t *w_news_root, *w_news_head, *w_news_author, *w_news_idx;
static size_t s_news_item = 0;
static uint32_t s_news_switch_ms = 0;

static void news_build(lv_obj_t *parent)
{
    w_news_root = page_root(parent);
    s_news_item = 0;
    s_news_switch_ms = millis();
    lv_obj_t *title = mk_label(w_news_root, "NEWS AUF BLOCKTRAINER.DE", FONT_S, COL_ORANGE);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 12);
    w_news_idx = mk_label(w_news_root, "/ ---", FONT_S, COL_GRAY);
    lv_obj_align(w_news_idx, LV_ALIGN_TOP_RIGHT, -14, 12);

    w_news_head = mk_label(w_news_root, "Lade News...", FONT_L);
    lv_label_set_long_mode(w_news_head, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(w_news_head, SCREEN_W - 40);
    lv_obj_align(w_news_head, LV_ALIGN_LEFT_MID, 20, 0);

    w_news_author = mk_label(w_news_root, "", FONT_XS, COL_GRAY);
    lv_obj_align(w_news_author, LV_ALIGN_BOTTOM_LEFT, 20, -24);       // clear of the Wi-Fi banner
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
        if (millis() - s_news_switch_ms > 9000) {
            s_news_item = (s_news_item + 1) % count;
            s_news_switch_ms = millis();
        }
        if (s_news_item >= count) s_news_item = 0;
        item = g_news[s_news_item];
    }
    set_text(w_news_head, item.headline);
    set_text(w_news_idx, String(s_news_item + 1) + " / " + String(count));
    String by = item.author;
    if (item.ts > 1700000000) {
        time_t t = item.ts;
        struct tm tmv;
        localtime_r(&t, &tmv);
        char b[24];
        strftime(b, sizeof(b), "%d.%m.%Y %H:%M", &tmv);
        by += (by.isEmpty() ? "" : "  |  ") + String(b);
    }
    set_text(w_news_author, by);
}

static void news_destroy() { del(w_news_root); w_news_head = nullptr; }

// ================================================================ DIFFICULTY

static lv_obj_t *w_diff_root, *w_diff_change, *w_diff_fill, *w_diff_blocks, *w_diff_progress;

static void diff_build(lv_obj_t *parent)
{
    w_diff_root = page_root(parent);
    lv_obj_t *title = mk_label(w_diff_root, "DIFFICULTY-ANPASSUNG", FONT_S);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    lv_obj_t *col = mk_col(w_diff_root, 400, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_pad_row(col, 14, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    mk_label(col, "ERWARTETE AENDERUNG", FONT_S, COL_LABEL);
    w_diff_change = mk_label(col, "-", FONT_3XL, COL_GREEN);

    lv_obj_t *track = mk_box(col, 400, 8);
    lv_obj_set_style_bg_color(track, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, 4, 0);
    w_diff_fill = mk_box(track, 0, 8);
    lv_obj_set_pos(w_diff_fill, 0, 0);
    lv_obj_set_style_bg_color(w_diff_fill, COL_ORANGE, 0);
    lv_obj_set_style_bg_opa(w_diff_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w_diff_fill, 4, 0);

    lv_obj_t *row = mk_row(col, 400, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *a = mk_col(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(a, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    mk_label(a, "NAECHSTER RETARGET", FONT_XS, COL_LABEL);
    w_diff_blocks = mk_label(a, "-", FONT_L);
    mk_label(a, "Bloecke bis Retarget", FONT_XS, COL_GRAY);
    lv_obj_t *b = mk_col(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    mk_label(b, "FORTSCHRITT", FONT_XS, COL_LABEL);
    w_diff_progress = mk_label(b, "-", FONT_L);
    mk_label(b, "bis Anpassung", FONT_XS, COL_GRAY);
}

static void diff_update()
{
    if (!valid(w_diff_root)) return;
    LiveData d = snapshot();
    set_text(w_diff_change, change_text(d.diffChange));
    lv_obj_set_style_text_color(w_diff_change, d.diffChange < 0 ? COL_RED : COL_GREEN, 0);
    set_text(w_diff_blocks, String(d.diffBlocks));
    set_text(w_diff_progress, fmt_de(d.diffProgress, 1) + " %");
    lv_obj_set_width(w_diff_fill, isnan(d.diffProgress) ? 0 : (lv_coord_t)constrain(d.diffProgress * 4.0f, 0.0f, 400.0f));
    led_for_change(d.diffChange);
}

static void diff_destroy() { del(w_diff_root); w_diff_change = nullptr; }

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
