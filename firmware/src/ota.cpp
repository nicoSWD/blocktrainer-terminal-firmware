#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "ota.h"
#include "config.h"

OtaState ota;

bool ota_version_newer(const String &current, const String &remote)
{
    int c[3] = {0, 0, 0}, r[3] = {0, 0, 0};
    sscanf(current.c_str(), "%d.%d.%d", &c[0], &c[1], &c[2]);
    sscanf(remote.c_str(), "%d.%d.%d", &r[0], &r[1], &r[2]);
    for (int i = 0; i < 3; i++) {
        if (r[i] != c[i]) {
            return r[i] > c[i];
        }
    }
    return false;
}

// Asks GitHub for the latest release of this project (GET /releases/latest skips drafts and
// prereleases). The response is a large object; only the tag and the assets are kept:
//   { "tag_name": "v0.1.1", "assets": [ { "name": "bt-terminal-firmware-v0.1.1.bin",
//       "browser_download_url": "https://github.com/.../bt-terminal-firmware-v0.1.1.bin" }, ... ] }
// The firmware is the asset named OTA_ASSET_PREFIX + version + OTA_ASSET_SUFFIX; the -debug and -safe
// builds and the checksum/license files next to it are ignored.
// `stage` becomes OTA_AVAILABLE when that version is newer than FIRMWARE_VERSION. Any other outcome
// (no network answer, HTTP error, unreadable JSON, no matching asset, not newer) leaves OTA_IDLE with
// no offer and no version from an earlier check, so a failed lookup never keeps a stale offer alive.
void ota_check()
{
    DBG("ota: checking %s\n", URL_UPDATE);
    if (WiFi.status() != WL_CONNECTED) {
        DBG("ota: Wi-Fi not connected\n");
        return;
    }
    ota.stage = OTA_IDLE;
    ota.active = false;
    ota.remoteVersion = "";
    ota.firmwareUrl = "";

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(URL_UPDATE)) {
        DBG("ota: http.begin() failed\n");
        return;
    }
    http.setUserAgent("bt-terminal-firmware/" FIRMWARE_VERSION); // GitHub rejects requests without one
    http.addHeader("Accept", "application/vnd.github+json");
    int code = http.GET();
    if (code != 200) {
        DBG("ota: release lookup HTTP %d\n", code);
        http.end();
        return;
    }
    String payload = http.getString();
    http.end();

    StaticJsonDocument<256> filter; // 5 slots (80 bytes on the device); roomy on purpose, an overflowing
                                    // filter silently drops keys
    filter["tag_name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    DynamicJsonDocument doc(16384); // ~200 bytes per asset: room for dozens, the filter drops everything else
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (err) {
        DBG("ota: release JSON error %s\n", err.c_str());
        return;
    }

    String version = doc["tag_name"] | "";
    if (version.startsWith("v") || version.startsWith("V")) {
        version = version.substring(1);
    }
    ota.remoteVersion = version;
    DBG("ota: remote %s, current %s\n", ota.remoteVersion.c_str(), FIRMWARE_VERSION);

    String url;
    if (!version.isEmpty()) {
        String wanted = String(OTA_ASSET_PREFIX) + version + OTA_ASSET_SUFFIX;
        for (JsonObject asset : doc["assets"].as<JsonArray>()) {
            if (wanted != (asset["name"] | "")) {
                continue;
            }
            url = asset["browser_download_url"] | "";
            if (!url.isEmpty()) {
                break; // a matching entry without a URL is not an offer
            }
        }
    }
    if (url.isEmpty() || !ota_version_newer(FIRMWARE_VERSION, ota.remoteVersion)) {
        return;
    }
    ota.firmwareUrl = url;
    ota.stage = OTA_AVAILABLE;
    ota.active = true;
}

static void fail(const String &message)
{
    ota.error = message;
    ota.stage = OTA_ERROR;
    DBG("ota: %s\n", message.c_str());
}

static void download_and_flash(const String &url)
{
    if (url.isEmpty()) {
        fail("empty firmware URL");
        return;
    }
    ota.stage = OTA_DOWNLOADING;
    ota.downloaded = 0;
    ota.total = 0;
    ota.error = "";

    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(url)) {
        fail("http.begin() failed");
        return;
    }
    int code = http.GET();
    if (code != 200) {
        fail("HTTP " + String(code));
        http.end();
        return;
    }
    int len = http.getSize(); // -1 when the server sends no length
    if (len == 0) {           // an empty body is never a firmware image
        fail("empty download");
        http.end();
        return;
    }
    ota.total = len > 0 ? len : 0;
    if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
        fail("Update.begin() failed (not enough space?)");
        http.end();
        return;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buf[1024];
    int written = 0;
    uint32_t last_log = millis(), last_progress = millis();
#ifndef OTA_STALL_MS
#define OTA_STALL_MS 20000 // the host test shortens this
#endif
    static const uint32_t STALL_MS = OTA_STALL_MS; // no byte for this long: give up
    bool failed = false;
    while (http.connected() && (len < 0 || written < len)) {
        size_t avail = stream->available();
        if (!avail) {
            if (millis() - last_progress > STALL_MS) {
                failed = true;
                break;
            }
            delay(1);
            continue;
        }
        size_t want = min(avail, sizeof(buf));
        if (len > 0) {
            want = min(want, (size_t)(len - written)); // never read or write past the announced size
        }
        int n = stream->readBytes(buf, want);
        if (n <= 0) {
            break;
        }
        if (Update.write(buf, n) != (size_t)n) {
            failed = true;
            break;
        }
        written += n;
        ota.downloaded = written;
        last_progress = millis();
        if (millis() - last_log > 750) {
            DBG("ota: %d / %d bytes\n", written, len);
            last_log = millis();
        }
    }
    http.end();

    // A short download must not be finalised: the partial image would be accepted
    // by Update.end(true) and then fail validation at boot.
    if (failed || (len > 0 && written != len)) {
        Update.abort();
        fail(String(failed ? "download stalled or write failed" : "download incomplete") + " (" +
             String(written) + "/" + String(len) + ")");
        return;
    }

    ota.stage = OTA_INSTALLING;
    if (Update.end(true)) {
        ota.stage = OTA_DONE;
        DBG("ota: installed\n");
    } else {
        fail("Update.end() error " + String(Update.getError()));
    }
}

static void ota_task(void *)
{
    download_and_flash(ota.firmwareUrl);
    ota.taskRunning = false;
    vTaskDelete(nullptr);
}

void ota_start_task()
{
    if (ota.taskRunning) {
        return;
    }
    ota.taskRunning = true;
    xTaskCreatePinnedToCore(ota_task, "OTAUpdateTask", 0x3000, nullptr, 1, nullptr, 1);
}

int ota_progress_percent()
{
    if (ota.total > 0) {
        return (int)((int64_t)ota.downloaded * 100 / ota.total);
    }
    return 0;
}
