// Page metadata shared by the UI and the web configuration page (no LVGL dependency).
#pragma once
#include <vector>
#include "config.h"

struct PageInfo {
    const char *name;       // label shown on the device and in the web page
    const char *prefKey;    // NVS key of the enable flag; also the token used in `pageOrder`
    bool menuOnly;          // reachable from the menu only, never part of the page rotation
};

extern const PageInfo PAGE_INFO[PAGE_COUNT];

// Effective order of the rotating pages (indices into PAGE_INFO, menu-only pages excluded):
// first the pages named in cfg.pageOrder, then any that are missing, in default order.
std::vector<int> page_order();
