// Host test for the real ota.cpp: manifest parsing, version comparison and the download/flash loop,
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

// ---------------------------------------------------------------- manifest

static void manifest_case(const char *name, OtaStage stage, const char *version, const char *url_part)
{
    printf("manifest %-18s", name);
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

// ---------------------------------------------------------------- firmware download

struct Expect {
    OtaStage stage;
    const char *error;          // substring of ota.error, nullptr for none
    bool begun, ended, aborted;
    bool data_matches;          // Update received exactly the served image
    double min_s, max_s;        // wall-clock window of the whole download
};

static void firmware_case(const char *name, const Expect &e, size_t fail_write_after = (size_t)-1)
{
    printf("download %-18s", name);
    reset();
    use_manifest(name);
    ota_check();
    CHECK(ota.stage == OTA_AVAILABLE, "manifest stage %d", (int)ota.stage);
    if (ota.stage != OTA_AVAILABLE) { printf("\n"); return; }
    Update.fail_write_after = fail_write_after;

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

static void version_table()
{
    printf("version compare\n");
    CHECK(ota_version_newer("1.8.3", "1.8.4"), "patch bump");
    CHECK(ota_version_newer("1.8.3", "1.8.10"), "1.8.10 is newer than 1.8.3 (numeric, not text)");
    CHECK(ota_version_newer("1.8.3", "1.9.0"), "minor bump");
    CHECK(ota_version_newer("1.8.3", "2.0.0"), "major bump");
    CHECK(!ota_version_newer("1.8.3", "1.8.3"), "equal");
    CHECK(!ota_version_newer("1.8.3", "1.8.2"), "older");
    CHECK(!ota_version_newer("1.8.3", ""), "empty");
    CHECK(!ota_version_newer("1.8.3", "abc"), "garbage");
    CHECK(ota_version_newer("1.8.2", "1.8.3"), "stock to custom");
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: ota_test <port>\n"); return 2; }
    g_port = atoi(argv[1]);
    setvbuf(stdout, nullptr, _IOLBF, 0);

    version_table();

    manifest_case("m_stock", OTA_AVAILABLE, "1.9.0", "/fw/ok/firmware.bin");
    manifest_case("m_double_digit", OTA_AVAILABLE, "1.8.10", "firmware.bin");
    manifest_case("m_multi", OTA_AVAILABLE, "9.9.9", "/fw/ok/firmware.bin");      // picks the ESP32-S3 build
    manifest_case("m_same", OTA_IDLE, "1.8.3", "firmware.bin");
    manifest_case("m_older", OTA_IDLE, "1.8.2", "firmware.bin");
    manifest_case("m_wrongchip", OTA_IDLE, "9.9.9", nullptr);
    manifest_case("m_nobuilds", OTA_IDLE, "9.9.9", nullptr);
    manifest_case("m_nofirmwarepart", OTA_IDLE, "9.9.9", nullptr);
    manifest_case("m_noversion", OTA_IDLE, "", "firmware.bin");
    manifest_case("m_garbage", OTA_IDLE, nullptr, nullptr);
    manifest_case("m_500", OTA_IDLE, nullptr, nullptr);
    manifest_case("m_hang", OTA_IDLE, nullptr, nullptr);

    //                                      stage      error                 begun ended  abort data   window s
    firmware_case("ok",           { OTA_DONE,  nullptr,              true,  true,  false, true,  0.0, 3.0 });
    firmware_case("slow",         { OTA_DONE,  nullptr,              true,  true,  false, true,  2.5, 8.0 });
    firmware_case("redirect",     { OTA_DONE,  nullptr,              true,  true,  false, true,  0.0, 3.0 });
    firmware_case("no_length",    { OTA_DONE,  nullptr,              true,  true,  false, true,  0.0, 3.0 });
    firmware_case("stall_mid",    { OTA_ERROR, "stalled",            true,  false, true,  false, 1.8, 6.0 });
    firmware_case("stall_start",  { OTA_ERROR, "stalled",            true,  false, true,  false, 1.8, 6.0 });
    firmware_case("short_close",  { OTA_ERROR, "incomplete",         true,  false, true,  false, 0.0, 3.0 });
    firmware_case("http404",      { OTA_ERROR, "HTTP 404",           false, false, false, false, 0.0, 3.0 });
    firmware_case("no_headers",   { OTA_ERROR, "HTTP -11",           false, false, false, false, 1.0, 5.0 });
    firmware_case("oversize",     { OTA_ERROR, "Update.begin",       false, false, false, false, 0.0, 3.0 });
    firmware_case("write_fail",   { OTA_ERROR, "write failed",       true,  false, true,  false, 0.0, 3.0 }, 10000);

    // the slow trickle also has to keep the progress numbers moving
    printf("progress after the last scenario: downloaded=%d total=%d\n", (int)ota.downloaded, (int)ota.total);

    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
