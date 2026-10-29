#include "pages.h"

const PageInfo PAGE_INFO[PAGE_COUNT] = {
    { "PREIS",      "pg_price",  false },
    { "CHART",      "pg_chart",  false },
    { "BLOCK",      "pg_block",  false },
    { "UHR",        "pg_clock",  false },
    { "BITAXE",     "pg_bitaxe", false },
    { "FEES",       "pg_fees",   false },
    { "RECHNER",    "pg_conv",   true  },
    { "NEWS",       "pg_news",   false },
    { "DIFFICULTY", "pg_diff",   false },
};

std::vector<int> page_order()
{
    std::vector<int> order;
    auto listed = [&](int page) {
        for (int p : order) if (p == page) return true;
        return false;
    };

    const String &s = cfg.pageOrder;
    for (int start = 0; start < (int)s.length();) {
        int comma = s.indexOf(',', start);
        String token = comma < 0 ? s.substring(start) : s.substring(start, comma);
        token.trim();
        for (int i = 0; i < PAGE_COUNT; i++) {
            if (!PAGE_INFO[i].menuOnly && token == PAGE_INFO[i].prefKey && !listed(i)) {
                order.push_back(i);
                break;
            }
        }
        if (comma < 0) break;
        start = comma + 1;
    }
    for (int i = 0; i < PAGE_COUNT; i++)
        if (!PAGE_INFO[i].menuOnly && !listed(i)) order.push_back(i);
    return order;
}
