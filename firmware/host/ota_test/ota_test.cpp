// Host test for the real ota.cpp: GitHub release lookup, version comparison and the download/flash loop,
// against a scripted server (server.py) that stalls, trickles, closes early, redirects and fails.
#include <chrono>
#include <string>
#include <vector>
#include <unistd.h>
#include "Update.h"
#include "ota.h"
#include "config.h"

String g_manifest_url;
static int g_port;
extern bool g_host_wifi_connected;
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

static std::vector<uint8_t> image(size_t n)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = i == 0 ? 0xE9 : (uint8_t)((i * 31 + 7) & 0xFF);
    return v;
}

static void reset()
{
    ota.stage = OTA_IDLE;
    ota.downloaded = 0;
    ota.total = 0;
    ota.error = "";
    ota.remoteVersion = "";
    ota.firmwareUrl = "";
    ota.active = false;
    ota.taskRunning = false;
    Update.reset();
}

static void use_manifest(const std::string &name)
{
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/m/%s.json", g_port, name.c_str());
    g_manifest_url = url;
}

static double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// ---------------------------------------------------------------- release lookup

// An IDLE result must carry no offer: no firmware URL, not active. Only OTA_AVAILABLE has a URL.
static void release_case(const char *name, OtaStage stage, const char *version, const char *url_part)
{
    printf("release  %-18s", name);
    reset();
    use_manifest(name);
    auto t0 = std::chrono::steady_clock::now();
    ota_check();
    double dt = seconds_since(t0);
    CHECK(ota.stage == stage, "stage %d expected %d", (int)ota.stage, (int)stage);
    CHECK(ota.active == (stage == OTA_AVAILABLE), "active=%d", (int)ota.active);
    if (version) CHECK(ota.remoteVersion == version, "remote '%s' expected '%s'", ota.remoteVersion.c_str(), version);
    if (url_part) CHECK(ota.firmwareUrl.indexOf(url_part) >= 0, "url '%s'", ota.firmwareUrl.c_str());
    else CHECK(ota.firmwareUrl.isEmpty(), "url '%s'", ota.firmwareUrl.c_str());
    CHECK(dt < 5.0, "took %.1f s", dt);
    printf("  ok (%.1f s)\n", dt);
}

// A failed or empty lookup must not leave the previous result behind (a stale offer would later be
// installed, or reported by /ota).
static void stale_case(const char *then_name)
{
    printf("stale    after %-12s", then_name);
    reset();
    use_manifest("m_newer");
    ota_check();
    CHECK(ota.stage == OTA_AVAILABLE, "setup: stage %d", (int)ota.stage);
    CHECK(!ota.firmwareUrl.isEmpty(), "setup: no url");
    use_manifest(then_name);
    ota_check();
    CHECK(ota.stage == OTA_IDLE, "stage %d", (int)ota.stage);
    CHECK(!ota.active, "still active");
    CHECK(ota.firmwareUrl.isEmpty(), "stale url '%s'", ota.firmwareUrl.c_str());
    CHECK(ota.remoteVersion != "1.9.0", "stale version '%s'", ota.remoteVersion.c_str());
    printf("  ok\n");
}

static void lookup_state_cases()
{
    stale_case("m_garbage");
    stale_case("m_500");
    stale_case("m_404");
    stale_case("m_403_ratelimit");
    stale_case("m_hang");
    stale_case("m_truncated");
    stale_case("m_empty");
    stale_case("m_early_close");
    stale_case("m_same");

    printf("repeat   5 checks in a row");
    reset();
    use_manifest("m_newer");
    for (int i = 0; i < 5; i++) {
        ota_check();
        CHECK(ota.stage == OTA_AVAILABLE && ota.remoteVersion == "1.9.0", "run %d: stage %d '%s'", i, (int)ota.stage, ota.remoteVersion.c_str());
        CHECK(ota.firmwareUrl.indexOf("/fw/ok/firmware.bin") >= 0, "run %d: url '%s'", i, ota.firmwareUrl.c_str());
    }
    printf("  ok\n");

    printf("replace  newer offer then older release");
    reset();
    use_manifest("m_newer");
    ota_check();
    use_manifest("m_older");
    ota_check();
    CHECK(ota.stage == OTA_IDLE && !ota.active && ota.firmwareUrl.isEmpty(), "stage %d url '%s'", (int)ota.stage, ota.firmwareUrl.c_str());
    printf("  ok\n");

    printf("wifi     down: no request, no offer");
    reset();
    use_manifest("m_newer");
    g_host_wifi_connected = false;
    ota_check();
    g_host_wifi_connected = true;
    CHECK(ota.stage == OTA_IDLE && !ota.active && ota.firmwareUrl.isEmpty(), "stage %d url '%s'", (int)ota.stage, ota.firmwareUrl.c_str());
    printf("  ok\n");

    printf("server   unreachable");
    reset();
    g_manifest_url = "http://127.0.0.1:1/m/m_newer.json";        // nothing listens on port 1
    ota_check();
    CHECK(ota.stage == OTA_IDLE && !ota.active && ota.firmwareUrl.isEmpty(), "stage %d", (int)ota.stage);
    printf("  ok\n");
}

// ---------------------------------------------------------------- firmware download

struct Expect {
    OtaStage stage;
    const char *error;          // substring of ota.error, nullptr for none
    bool begun, ended, aborted;
    bool data_matches;          // Update received exactly the served image
    double min_s, max_s;        // wall-clock window of the whole download
};

static void firmware_case(const char *name, const Expect &e, size_t fail_write_after = (size_t)-1, bool fail_end = false)
{
    printf("download %-18s", name);
    reset();
    use_manifest(name);
    ota_check();
    CHECK(ota.stage == OTA_AVAILABLE, "manifest stage %d", (int)ota.stage);
    if (ota.stage != OTA_AVAILABLE) { printf("\n"); return; }
    Update.fail_write_after = fail_write_after;
    Update.fail_end = fail_end;

    ota.stage = OTA_CONFIRMED;
    auto t0 = std::chrono::steady_clock::now();
    ota_start_task();
    while (ota.taskRunning && seconds_since(t0) < 40) usleep(20000);
    double dt = seconds_since(t0);

    CHECK(!ota.taskRunning, "task still running after %.0f s", dt);
    CHECK(ota.stage == e.stage, "stage %d expected %d (error '%s')", (int)ota.stage, (int)e.stage, ota.error.c_str());
    if (e.error) CHECK(ota.error.indexOf(e.error) >= 0, "error '%s' expected to contain '%s'", ota.error.c_str(), e.error);
    else CHECK(ota.error.isEmpty(), "unexpected error '%s'", ota.error.c_str());
    CHECK(Update.begun == e.begun, "begun=%d", (int)Update.begun);
    CHECK(Update.begin_calls == (e.begun ? 1 : 0), "begin() called %d times", Update.begin_calls);
    CHECK(Update.ended == e.ended, "ended=%d", (int)Update.ended);
    CHECK(Update.aborted == e.aborted, "aborted=%d", (int)Update.aborted);
    if (e.data_matches) {
        std::vector<uint8_t> want = image(40000);
        CHECK(Update.data == want, "image mismatch: got %zu bytes, want %zu", Update.data.size(), want.size());
    }
    CHECK(!(e.ended && !e.data_matches), "bad expectation");
    CHECK(dt >= e.min_s && dt <= e.max_s, "took %.2f s, window %.1f..%.1f", dt, e.min_s, e.max_s);
    // an install that did not finish must never have been finalised
    if (e.stage == OTA_ERROR) CHECK(!Update.ended, "flash finalised after an error");
    printf("  ok (%.1f s, %d bytes, stage %d%s%s)\n", dt, (int)ota.downloaded, (int)ota.stage,
           ota.error.isEmpty() ? "" : ", ", ota.error.c_str());
}

// start requested twice while a download runs: still one download, one flash session
static void single_task_case()
{
    printf("download one_task            ");
    reset();
    use_manifest("one_task");
    ota_check();
    CHECK(ota.stage == OTA_AVAILABLE, "stage %d", (int)ota.stage);
    ota.stage = OTA_CONFIRMED;
    auto t0 = std::chrono::steady_clock::now();
    ota_start_task();
    usleep(150000);
    CHECK(ota.taskRunning, "task not running after start");
    ota_start_task();                       // second request, e.g. a double tap or /ota?install=1 on top of the prompt
    ota_start_task();
    while (ota.taskRunning && seconds_since(t0) < 40) usleep(20000);
    CHECK(!ota.taskRunning, "task still running");
    CHECK(ota.stage == OTA_DONE, "stage %d (error '%s')", (int)ota.stage, ota.error.c_str());
    CHECK(Update.begin_calls == 1, "begin() called %d times", Update.begin_calls);
    CHECK(Update.data == image(40000), "image mismatch: %zu bytes", Update.data.size());
    printf("  ok (%.1f s)\n", seconds_since(t0));
}

static void version_table()
{
    printf("version compare\n");
    CHECK(ota_version_newer("0.1.0", "0.1.1"), "patch bump");
    CHECK(ota_version_newer("0.1.0", "0.10.0"), "0.10.0 is newer than 0.1.0 (numeric, not text)");
    CHECK(ota_version_newer("0.1.9", "0.1.10"), "0.1.10 is newer than 0.1.9");
    CHECK(ota_version_newer("0.9.9", "0.10.0"), "0.10.0 is newer than 0.9.9");
    CHECK(ota_version_newer("0.1.0", "1.0.0"), "major bump");
    CHECK(ota_version_newer("0.99.99", "1.0.0"), "major beats minor and patch");
    CHECK(ota_version_newer("9.9.9", "10.0.0"), "10.0.0 is newer than 9.9.9");
    CHECK(!ota_version_newer("0.1.0", "0.1.0"), "equal");
    CHECK(!ota_version_newer("1.0.0", "0.99.99"), "older major");
    CHECK(!ota_version_newer("0.2.0", "0.1.99"), "older minor");
    CHECK(!ota_version_newer("0.1.0", "0.0.9"), "older");
    CHECK(!ota_version_newer("0.1.0", ""), "empty remote");
    CHECK(!ota_version_newer("0.1.0", "abc"), "garbage remote");
    CHECK(!ota_version_newer("0.1.0", "v0.2.0"), "a leading v is not a number (the lookup strips it)");
    CHECK(!ota_version_newer("0.1.0", "-1.0.0"), "negative major");
    CHECK(!ota_version_newer("0.1.0", "0.-5.0"), "negative minor");
    CHECK(ota_version_newer("0.1.0", "0.2"), "0.2 is 0.2.0");
    CHECK(!ota_version_newer("0.1.0", "0.1"), "0.1 is 0.1.0, equal");
    CHECK(ota_version_newer("0.1", "0.1.1"), "current 0.1 is 0.1.0");
    CHECK(!ota_version_newer("0.1.0", "0.1.0.9"), "a fourth component is ignored");
    CHECK(!ota_version_newer("0.1.0", "0.1.0-rc1"), "a pre-release suffix is ignored, not newer");
    CHECK(ota_version_newer("0.1.0", "0.2.0-rc1"), "a pre-release of a newer version counts as that version");
    CHECK(ota_version_newer("0.1.0", "00.01.01"), "leading zeros are decimal");
    CHECK(!ota_version_newer("0.1.0", "00.01.00"), "leading zeros, equal");
    CHECK(ota_version_newer("0.1.0", " 0.1.1"), "leading space");
    CHECK(!ota_version_newer("", ""), "both empty");
    CHECK(ota_version_newer("", "0.0.1"), "empty current is 0.0.0");
    CHECK(!ota_version_newer("0.1.0", "0.x.9"), "a non-numeric component stops the parse at 0.0.0");
    // transitivity over a set: sorting by the compare gives the expected order
    const char *order[] = {"0.0.1", "0.0.9", "0.0.10", "0.1.0", "0.1.1", "0.9.0", "0.10.0", "1.0.0", "1.0.1", "2.0.0", "10.0.0"};
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++)
        for (size_t j = 0; j < sizeof(order) / sizeof(order[0]); j++)
            CHECK(ota_version_newer(order[i], order[j]) == (j > i), "%s -> %s", order[i], order[j]);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: ota_test <port>\n"); return 2; }
    g_port = atoi(argv[1]);
    setvbuf(stdout, nullptr, _IOLBF, 0);

    version_table();

    release_case("m_newer", OTA_AVAILABLE, "1.9.0", "/fw/ok/firmware.bin");
    release_case("m_double_digit", OTA_AVAILABLE, "0.10.0", "firmware.bin");
    release_case("m_no_v", OTA_AVAILABLE, "2.0.0", "firmware.bin");               // tag without the leading v
    release_case("m_tag_upper", OTA_AVAILABLE, "2.0.0", "firmware.bin");          // V2.0.0
    release_case("m_asset_order", OTA_AVAILABLE, "3.0.0", "/fw/ok/firmware.bin"); // decoys listed first
    release_case("m_lookalikes", OTA_AVAILABLE, "5.0.0", "/fw/ok/firmware.bin");  // .sig, prefix, case, zero-padded
    release_case("m_many_assets", OTA_AVAILABLE, "4.0.0", "/fw/ok/firmware.bin"); // 60 extra assets before it
    release_case("m_huge_body", OTA_AVAILABLE, "6.0.0", "/fw/ok/firmware.bin");   // 300 KB of release notes
    release_case("m_null_entries", OTA_AVAILABLE, "7.0.0", "/fw/ok/firmware.bin");
    release_case("m_redirect", OTA_AVAILABLE, "1.9.0", "/fw/ok/firmware.bin");    // repository renamed: 301
    release_case("m_no_length", OTA_AVAILABLE, "1.9.0", "/fw/ok/firmware.bin");   // body ends at close
    release_case("m_slow_body", OTA_AVAILABLE, "1.9.0", "/fw/ok/firmware.bin");
    release_case("m_same", OTA_IDLE, "0.1.0", nullptr);
    release_case("m_older", OTA_IDLE, "0.0.9", nullptr);
    release_case("m_wrongasset", OTA_IDLE, "9.9.9", nullptr);
    release_case("m_onlydecoys", OTA_IDLE, "9.9.9", nullptr);                     // -debug/-safe/checksums only
    release_case("m_lookalikes_only", OTA_IDLE, "5.0.0", nullptr);
    release_case("m_noassets", OTA_IDLE, "9.9.9", nullptr);
    release_case("m_assets_null", OTA_IDLE, "9.9.9", nullptr);
    release_case("m_assets_object", OTA_IDLE, "9.9.9", nullptr);
    release_case("m_no_url", OTA_IDLE, "9.9.9", nullptr);
    release_case("m_empty_url", OTA_IDLE, "9.9.9", nullptr);
    release_case("m_notag", OTA_IDLE, "", nullptr);
    release_case("m_tag_number", OTA_IDLE, "", nullptr);
    release_case("m_tag_null", OTA_IDLE, "", nullptr);
    release_case("m_empty_object", OTA_IDLE, "", nullptr);
    release_case("m_json_array", OTA_IDLE, "", nullptr);
    release_case("m_json_null", OTA_IDLE, "", nullptr);
    release_case("m_empty", OTA_IDLE, "", nullptr);
    release_case("m_garbage", OTA_IDLE, "", nullptr);
    release_case("m_truncated", OTA_IDLE, "", nullptr);                           // cut off mid-body
    release_case("m_stall_body", OTA_IDLE, "", nullptr);                          // silence mid-body
    release_case("m_early_close", OTA_IDLE, "", nullptr);                         // hung up before answering
    release_case("m_redirect_loop", OTA_IDLE, "", nullptr);
    release_case("m_404", OTA_IDLE, "", nullptr);                                 // no release yet
    release_case("m_403_ratelimit", OTA_IDLE, "", nullptr);
    release_case("m_429", OTA_IDLE, "", nullptr);
    release_case("m_500", OTA_IDLE, "", nullptr);
    release_case("m_503", OTA_IDLE, "", nullptr);
    release_case("m_hang", OTA_IDLE, "", nullptr);
    lookup_state_cases();

    //                                      stage      error                 begun ended  abort data   window s
    firmware_case("ok",           { OTA_DONE,  nullptr,              true,  true,  false, true,  0.0, 3.0 });
    firmware_case("slow",         { OTA_DONE,  nullptr,              true,  true,  false, true,  2.5, 8.0 });
    firmware_case("redirect",     { OTA_DONE,  nullptr,              true,  true,  false, true,  0.0, 3.0 });
    firmware_case("redirect_abs", { OTA_DONE,  nullptr,              true,  true,  false, true,  0.0, 3.0 });
    firmware_case("redirect_chain", { OTA_DONE, nullptr,             true,  true,  false, true,  0.0, 3.0 });
    firmware_case("no_length",    { OTA_DONE,  nullptr,              true,  true,  false, true,  0.0, 3.0 });
    firmware_case("extra_bytes",  { OTA_DONE,  nullptr,              true,  true,  false, true,  0.0, 3.0 });
    firmware_case("stall_mid",    { OTA_ERROR, "stalled",            true,  false, true,  false, 1.8, 6.0 });
    firmware_case("stall_start",  { OTA_ERROR, "stalled",            true,  false, true,  false, 1.8, 6.0 });
    firmware_case("short_close",  { OTA_ERROR, "incomplete",         true,  false, true,  false, 0.0, 3.0 });
    firmware_case("http404",      { OTA_ERROR, "HTTP 404",           false, false, false, false, 0.0, 3.0 });
    firmware_case("no_headers",   { OTA_ERROR, "HTTP -11",           false, false, false, false, 1.0, 5.0 });
    firmware_case("oversize",     { OTA_ERROR, "Update.begin",       false, false, false, false, 0.0, 3.0 });
    firmware_case("redirect_loop", { OTA_ERROR, "HTTP",              false, false, false, false, 0.0, 3.0 });
    firmware_case("empty_body",   { OTA_ERROR, "empty",              false, false, false, false, 0.0, 3.0 });
    firmware_case("html_page",    { OTA_ERROR, "write failed",       true,  false, true,  false, 0.0, 3.0 });
    firmware_case("write_fail",   { OTA_ERROR, "write failed",       true,  false, true,  false, 0.0, 3.0 }, 10000);
    firmware_case("end_fail",     { OTA_ERROR, "Update.end",         true,  false, false, false, 0.0, 3.0 }, (size_t)-1, true);
    single_task_case();

    // the slow trickle also has to keep the progress numbers moving
    printf("progress after the last scenario: downloaded=%d total=%d\n", (int)ota.downloaded, (int)ota.total);

    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
