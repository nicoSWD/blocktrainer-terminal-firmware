// Host test for the real wifi_scan.cpp against a scripted radio (wifi_test_stubs.cpp) and a virtual
// clock: scans that fail or come back empty, a radio that is busy joining, results freed by the driver,
// duplicate and hidden networks, ordering and limits, plus randomised checks of the list helpers.
#include <algorithm>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "WiFi.h"
#include "wifi_scan.h"
#include "wifi_test_radio.h"

static int g_checks = 0, g_failed = 0;

#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(cond)) {                                                            \
            g_failed++;                                                           \
            printf("    FAIL  %s:%d  %s   ", __FILE__, __LINE__, #cond);          \
            printf(__VA_ARGS__);                                                  \
            printf("\n");                                                         \
        }                                                                         \
    } while (0)

static int g_ticks = 0;
static void tick() { g_ticks++; }

static RadioAp ap(const char *ssid, int rssi, int ch = 6, uint8_t id = 0)
{
    RadioAp a;
    a.ssid = ssid;
    a.rssi = rssi;
    a.channel = ch;
    for (int i = 0; i < 6; i++) a.bssid[i] = (uint8_t)(id + i);
    return a;
}

static void fresh()
{
    g_radio.reset();
    g_ticks = 0;
}

static std::vector<std::string> names(const std::vector<WifiAp> &v)
{
    std::vector<std::string> out;
    for (const WifiAp &a : v) out.push_back(a.ssid.c_str());
    return out;
}

static bool is(const std::vector<WifiAp> &v, std::vector<std::string> want) { return names(v) == want; }

// ---------------------------------------------------------------- scanning

static void scan_results()
{
    printf("scan: results\n");
    fresh();
    g_radio.env = { ap("Home", -60, 6, 0x10), ap("Cafe", -45, 1, 0x20), ap("Guest", -80, 11, 0x30) };
    WifiScan s = wifi_scan();
    CHECK(s.attempts == 1 && s.code == 3, "attempts %d code %d", s.attempts, s.code);
    CHECK(is(s.aps, { "Cafe", "Home", "Guest" }), "order: strongest first");
    CHECK(s.aps.size() == 3 && s.aps[0].rssi == -45 && s.aps[0].channel == 1 && s.aps[0].bssid[0] == 0x20 && s.aps[0].bssid[5] == 0x25,
          "fields of the first entry were not copied");
    CHECK(s.aps[2].channel == 11 && s.aps[2].bssid[0] == 0x30, "fields of the last entry");
    CHECK(g_radio.scan_calls == 1, "scan calls %d", g_radio.scan_calls);
    CHECK(g_radio.scan_delete_calls == 1, "the driver's copy was not freed (%d)", g_radio.scan_delete_calls);
    CHECK(g_radio.waited_ms == 0, "waited %u ms although the first scan worked", g_radio.waited_ms);
    CHECK(WiFi.SSID(0).isEmpty(), "driver results should be gone after the scan");
    CHECK(s.aps[0].ssid == "Cafe", "the copy must outlive the driver's results");
}

static void scan_ordering_and_stability()
{
    printf("scan: ordering\n");
    fresh();
    g_radio.env = { ap("A", -70), ap("B", -50), ap("C", -70), ap("D", -50), ap("E", -90) };
    WifiScan s = wifi_scan();
    CHECK(is(s.aps, { "B", "D", "A", "C", "E" }), "equal signal strengths keep the driver's order");
}

static void scan_retries()
{
    printf("scan: retries\n");
    for (int fail : { -2, 0 }) {
        fresh();
        g_radio.env = { ap("Home", -50) };
        g_radio.forced = { fail };
        WifiScan s = wifi_scan();
        CHECK(s.attempts == 2 && s.aps.size() == 1, "first scan %d: attempts %d found %zu", fail, s.attempts, s.aps.size());
        CHECK(g_radio.scan_calls == 2, "scan calls %d", g_radio.scan_calls);
        CHECK(g_radio.waited_ms > 0, "no pause before the retry");
    }
    fresh();
    g_radio.env = { ap("Home", -50) };
    g_radio.forced = { -2, 0 };                         // fails, then empty, then works
    WifiScan s = wifi_scan();
    CHECK(s.attempts == 3 && s.aps.size() == 1, "attempts %d found %zu", s.attempts, s.aps.size());

    fresh();
    g_radio.env = { ap("Home", -50) };
    g_radio.forced = { -2, -2, -2 };                    // every try of the default budget fails
    s = wifi_scan();
    CHECK(s.attempts == WIFI_SCAN_ATTEMPTS && s.aps.empty() && s.code == -2, "attempts %d code %d found %zu", s.attempts, s.code, s.aps.size());
    CHECK(g_radio.scan_calls == WIFI_SCAN_ATTEMPTS, "scan calls %d", g_radio.scan_calls);
}

static void scan_gives_up()
{
    printf("scan: gives up\n");
    fresh();
    g_radio.forced = { -2, -2, -2, -2, -2 };
    WifiScan s = wifi_scan(WIFI_SCAN_ATTEMPTS, tick);
    CHECK(s.aps.empty() && s.code == -2 && s.attempts == WIFI_SCAN_ATTEMPTS, "code %d attempts %d", s.code, s.attempts);
    uint32_t total = g_radio.scan_time_ms + g_radio.waited_ms;
    CHECK(total <= 20000, "took %u ms of virtual time, a user would stare at a spinner", total);
    CHECK(g_ticks > 0 && (uint32_t)g_ticks * 100 >= g_radio.waited_ms - 100, "tick called %d times for %u ms of waiting", g_ticks, g_radio.waited_ms);

    fresh();                                            // nothing in range at all
    s = wifi_scan();
    CHECK(s.aps.empty() && s.code == 0 && s.attempts == WIFI_SCAN_ATTEMPTS, "empty air: code %d attempts %d", s.code, s.attempts);

    fresh();                                            // a single attempt never waits
    g_radio.forced = { -2 };
    s = wifi_scan(1);
    CHECK(s.attempts == 1 && g_radio.scan_calls == 1 && g_radio.waited_ms == 0, "attempts %d calls %d waited %u", s.attempts, g_radio.scan_calls, g_radio.waited_ms);

    fresh();                                            // nonsense budgets still scan once
    g_radio.env = { ap("Home", -50) };
    s = wifi_scan(0);
    CHECK(s.attempts == 1 && s.aps.size() == 1, "attempts 0: %d / %zu", s.attempts, s.aps.size());
    s = wifi_scan(-5);
    CHECK(s.attempts == 1 && s.aps.size() == 1, "attempts -5: %d / %zu", s.attempts, s.aps.size());
}

static void scan_busy_radio()
{
    printf("scan: radio busy joining\n");
    fresh();                                            // a saved network that rejects us keeps the radio busy
    g_radio.env = { ap("Home", -50) };
    g_radio.busy_joining = true;
    WifiScan s = wifi_scan();
    CHECK(g_radio.disconnect_calls >= 1, "the pending join was not stopped");
    CHECK(s.aps.size() == 1 && s.attempts == 1, "found %zu after %d attempts (disconnect should clear the busy state first)", s.aps.size(), s.attempts);

    fresh();                                            // the same, but the first disconnect() does not help
    g_radio.env = { ap("Home", -50) };
    g_radio.busy_joining = true;
    g_radio.forced = { -2 };
    s = wifi_scan();
    CHECK(s.aps.size() == 1 && s.attempts == 2, "found %zu after %d attempts", s.aps.size(), s.attempts);

    fresh();                                            // an established connection is left alone
    g_host_wifi_connected = true;
    g_radio.env = { ap("Home", -50) };
    s = wifi_scan();
    CHECK(g_radio.disconnect_calls == 0, "disconnected a working connection (%d)", g_radio.disconnect_calls);
    CHECK(s.aps.size() == 1, "found %zu", s.aps.size());
    g_host_wifi_connected = false;
}

static void scan_data()
{
    printf("scan: data\n");
    fresh();
    std::string max32(32, 'W');
    g_radio.env = { ap("Caf\xC3\xA9 M\xC3\xBCller Stra\xC3\x9F" "e", -50), ap(max32.c_str(), -51), ap("\xE2\x82\xAC uro \xF0\x9F\x93\xB6", -52),
                    ap("with space ", -53), ap("quote\"and<tag>&", -54), ap("", -55) };
    WifiScan s = wifi_scan();
    CHECK(s.aps.size() == 6, "found %zu", s.aps.size());
    CHECK(s.aps[0].ssid == "Caf\xC3\xA9 M\xC3\xBCller Stra\xC3\x9F" "e", "UTF-8 name changed: '%s'", s.aps[0].ssid.c_str());
    CHECK(s.aps[1].ssid.length() == 32, "32 character name has %u", (unsigned)s.aps[1].ssid.length());
    CHECK(s.aps[2].ssid == "\xE2\x82\xAC uro \xF0\x9F\x93\xB6", "emoji name changed");
    CHECK(s.aps[3].ssid == "with space ", "trailing space dropped");
    CHECK(s.aps[4].ssid == "quote\"and<tag>&", "special characters changed");
    CHECK(s.aps[5].ssid.isEmpty(), "hidden network gets a name");

    fresh();                                            // a crowded band
    for (int i = 0; i < 80; i++) g_radio.env.push_back(ap(("Net" + std::to_string(i)).c_str(), -30 - i));
    s = wifi_scan();
    CHECK(s.aps.size() == 80 && s.aps[0].ssid == "Net0" && s.aps[79].ssid == "Net79", "found %zu", s.aps.size());
}

// ---------------------------------------------------------------- list helpers

static WifiAp make(const char *ssid, int rssi, int ch = 1, uint8_t id = 0)
{
    WifiAp a;
    a.ssid = ssid;
    a.rssi = rssi;
    a.channel = ch;
    for (int i = 0; i < 6; i++) a.bssid[i] = (uint8_t)(id + i);
    return a;
}

static void networks_list()
{
    printf("networks\n");
    CHECK(wifi_networks({}, 15).empty(), "empty input");
    CHECK(wifi_networks({ make("A", -50) }, 0).empty(), "max 0");

    std::vector<WifiAp> aps = { make("Fritz", -70, 1, 0x10), make("Vodafone", -60), make("Fritz", -45, 11, 0x20), make("Fritz", -80, 6, 0x30) };
    std::vector<WifiAp> n = wifi_networks(aps, 15);
    CHECK(is(n, { "Fritz", "Vodafone" }), "duplicates collapse, strongest first");
    CHECK(n.size() == 2 && n[0].rssi == -45 && n[0].channel == 11 && n[0].bssid[0] == 0x20, "the entry kept for a duplicate is its strongest access point");

    aps = { make("", -30), make("A", -50), make("", -40) };
    CHECK(is(wifi_networks(aps, 15), { "A" }), "hidden networks are left out");
    CHECK(wifi_networks({ make("", -30) }, 15).empty(), "only hidden networks");

    aps = { make("home", -50), make("Home", -60), make("HOME", -70), make("Home ", -80) };
    CHECK(wifi_networks(aps, 15).size() == 4, "names are case and whitespace sensitive");

    aps.clear();                                        // the limit counts networks, not access points
    for (int i = 0; i < 40; i++) aps.push_back(make("Same", -30 - i));
    for (int i = 0; i < 20; i++) aps.push_back(make(("Other" + std::to_string(i)).c_str(), -90 + i / 2));
    n = wifi_networks(aps, 15);
    CHECK(n.size() == 15 && n[0].ssid == "Same", "40 access points of one network must not use up the limit: %zu entries", n.size());

    aps = { make("B", -60), make("A", -40), make("C", -50) };
    CHECK(is(wifi_networks(aps, 2), { "A", "C" }), "the limit keeps the strongest, whatever the input order");
    CHECK(is(wifi_networks(aps, 15), { "A", "C", "B" }), "unsorted input is sorted");
}

static void find_ap()
{
    printf("find_ap\n");
    std::vector<WifiAp> aps = { make("Home", -70, 1, 0x10), make("Home", -50, 11, 0x20), make("Home2", -30), make("", -20), make("home", -10) };
    const WifiAp *a = wifi_find_ap(aps, "Home");
    CHECK(a && a->rssi == -50 && a->channel == 11 && a->bssid[0] == 0x20, "strongest access point of the name");
    CHECK(wifi_find_ap(aps, "Hom") == nullptr, "a prefix is not a match");
    CHECK(wifi_find_ap(aps, "Home22") == nullptr, "a longer name is not a match");
    CHECK(wifi_find_ap(aps, "") == nullptr, "an empty name must not match a hidden network");
    CHECK(wifi_find_ap(aps, "nope") == nullptr, "unknown name");
    CHECK(wifi_find_ap({}, "Home") == nullptr, "empty list");
    a = wifi_find_ap(aps, "home");
    CHECK(a && a->rssi == -10, "case sensitive match");
}

// ---------------------------------------------------------------- joining

static void join_pins_strongest_ap()
{
    printf("join: pins the strongest access point\n");
    fresh();
    g_radio.env = { ap("Lab", -70, 11, 0x40), ap("Other", -40, 1, 0x50), ap("Lab", -50, 6, 0x60) };
    g_radio.accept["Lab"] = "pw";
    WifiJoin j = wifi_join("Lab", "pw", 20000);
    CHECK(j.ok && j.found, "ok=%d found=%d", j.ok, j.found);
    CHECK(j.channel == 6 && j.rssi == -50, "channel %d rssi %d", j.channel, j.rssi);
    CHECK(g_radio.begin_calls == 1 && g_radio.begin_pinned && g_radio.begin_channel == 6, "begin calls %d channel %d", g_radio.begin_calls, g_radio.begin_channel);
    CHECK(g_radio.begin_bssid[0] == 0x60, "bssid %02x", g_radio.begin_bssid[0]);
    CHECK(g_radio.disconnect_calls >= 1, "the old link is dropped first");
    CHECK(g_radio.waited_ms < 20000, "returned as soon as connected (%u ms)", g_radio.waited_ms);
}

static void join_not_in_scan()
{
    printf("join: name not in the scan falls back to a plain connect\n");
    fresh();
    g_radio.env = { ap("Other", -40, 1, 0x50) };
    g_radio.accept["Hidden"] = "pw";
    WifiJoin j = wifi_join("Hidden", "pw", 20000);
    CHECK(j.ok && !j.found, "ok=%d found=%d", j.ok, j.found);
    CHECK(g_radio.begin_calls == 1 && !g_radio.begin_pinned && g_radio.begin_channel == 0, "plain begin expected");
    fresh();                                        // empty air and a failing scan: still tries to connect
    g_radio.forced = { -2, -2 };
    g_radio.accept["Home"] = "pw";
    j = wifi_join("Home", "pw", 20000);
    CHECK(j.ok && !j.found && j.scan_code == -2, "ok=%d found=%d code=%d", j.ok, j.found, j.scan_code);
}

static void join_wrong_password()
{
    printf("join: wrong password waits the full timeout\n");
    fresh();
    g_radio.env = { ap("Lab", -50, 6, 0x60) };
    g_radio.accept["Lab"] = "right";
    uint32_t t0 = millis();
    WifiJoin j = wifi_join("Lab", "wrong", 20000);
    uint32_t took = millis() - t0;
    CHECK(!j.ok && j.found, "ok=%d found=%d", j.ok, j.found);
    CHECK(g_radio.begin_calls == 1, "one begin");
    CHECK(took >= 20000 && took < 20000 + 4000 + 2 * 3500 + 1000, "took %u ms", took);
    // the next attempt starts clean: the pending (failed) join must not block the scan
    g_radio.env = { ap("Lab", -50, 6, 0x60) };
    j = wifi_join("Lab", "right", 20000);
    CHECK(j.ok && j.scan_count == 1, "second attempt ok=%d scan_count=%d", j.ok, j.scan_count);
}

static void join_slow_network()
{
    printf("join: a network slower than the timeout fails, a faster one works\n");
    fresh();
    g_radio.env = { ap("Lab", -50) };
    g_radio.accept["Lab"] = "pw";
    g_radio.join_ms = 25000;
    CHECK(!wifi_join("Lab", "pw", 20000).ok, "joined after the timeout");
    fresh();
    g_radio.env = { ap("Lab", -50) };
    g_radio.accept["Lab"] = "pw";
    g_radio.join_ms = 19000;
    CHECK(wifi_join("Lab", "pw", 20000).ok, "joined within the timeout");
}

static void join_scan_recovers()
{
    printf("join: a scan that fails once is retried\n");
    fresh();
    g_radio.env = { ap("Lab", -50, 3, 0x70) };
    g_radio.accept["Lab"] = "pw";
    g_radio.forced = { -2, -1 };
    WifiJoin j = wifi_join("Lab", "pw", 20000);
    CHECK(j.ok && j.found && g_radio.scan_calls == 2, "ok=%d found=%d scans=%d", j.ok, j.found, g_radio.scan_calls);
}

static void randomised()
{
    printf("randomised: 2000 lists against a reference\n");
    srand(12345);
    const char *pool[] = { "A", "B", "Home", "home", "Guest", "", "Fritz!Box", "Caf\xC3\xA9", "x", "y", "WW", "" };
    int bad = 0;
    for (int round = 0; round < 2000 && bad < 5; round++) {
        std::vector<WifiAp> aps;
        int n = rand() % 40;
        for (int i = 0; i < n; i++) aps.push_back(make(pool[rand() % (sizeof(pool) / sizeof(pool[0]))], -20 - rand() % 75, 1 + rand() % 13, (uint8_t)i));
        size_t max = (size_t)(rand() % 17);

        std::map<std::string, int> best;                // reference: strongest rssi per visible name
        for (const WifiAp &a : aps) {
            if (a.ssid.isEmpty()) continue;
            auto it = best.find(a.ssid.c_str());
            if (it == best.end() || a.rssi > it->second) best[a.ssid.c_str()] = a.rssi;
        }
        std::vector<WifiAp> out = wifi_networks(aps, max);
        bool ok = out.size() == std::min(max, best.size());
        std::map<std::string, int> seen;
        for (size_t i = 0; ok && i < out.size(); i++) {
            const std::string name = out[i].ssid.c_str();
            ok = !name.empty() && seen[name]++ == 0 && best.count(name) && out[i].rssi == best[name] && (i == 0 || out[i - 1].rssi >= out[i].rssi);
        }
        if (ok && !out.empty()) {                       // nothing weaker than the last entry was left out while a stronger name is missing
            for (auto &kv : best) if (!seen.count(kv.first) && kv.second > out.back().rssi && out.size() >= max) ok = false;
        }
        for (auto &kv : best) {                         // find_ap agrees with the reference
            const WifiAp *f = wifi_find_ap(aps, kv.first.c_str());
            if (!f || f->rssi != kv.second) ok = false;
        }
        if (!ok) { bad++; CHECK(false, "round %d (n=%d max=%zu) disagrees with the reference", round, n, max); }
        else g_checks++;
    }
}

int main()
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    scan_results();
    scan_ordering_and_stability();
    scan_retries();
    scan_gives_up();
    scan_busy_radio();
    scan_data();
    networks_list();
    find_ap();
    join_pins_strongest_ap();
    join_not_in_scan();
    join_wrong_password();
    join_slow_network();
    join_scan_recovers();
    randomised();
    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
