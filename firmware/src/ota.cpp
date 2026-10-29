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
    for (int i = 0; i < 3; i++)
        if (r[i] != c[i]) return r[i] > c[i];
    return false;
}

// Reads the manifest (esp-web-tools style):
//   { "version": "x.y.z", "builds": [ { "chipFamily": "ESP32-S3",
//       "parts": [ { "path": ".../firmware.bin", "offset": ... } ] } ] }
// The ESP32-S3 build's first part whose path contains "firmware.bin" is the app image.
// `stage` becomes OTA_AVAILABLE when the manifest version is newer than FIRMWARE_VERSION.
void ota_check()
{
    DBG("ota: checking %s\n", URL_UPDATE);
    if (WiFi.status() != WL_CONNECTED) {
        DBG("ota: Wi-Fi not connected\n");
        return;
    }

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(URL_UPDATE)) {
        DBG("ota: http.begin() failed\n");
        return;
    }
    int code = http.GET();
    if (code != 200) {
        DBG("ota: manifest HTTP %d\n", code);
        ota.stage = OTA_IDLE;
        ota.active = false;
        http.end();
        return;
    }
    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        DBG("ota: manifest JSON error %s\n", err.c_str());
        return;
    }

    ota.remoteVersion = doc["version"] | "";
    DBG("ota: remote %s, current %s\n", ota.remoteVersion.c_str(), FIRMWARE_VERSION);

    String url;
    for (JsonObject build : doc["builds"].as<JsonArray>()) {
        if (strcmp(build["chipFamily"] | "", "ESP32-S3") != 0) continue;
        for (JsonObject part : build["parts"].as<JsonArray>()) {
            const char *path = part["path"] | "";
            if (strstr(path, "firmware.bin")) {
                url = path;
                break;
            }
        }
        break;
    }
    ota.firmwareUrl = url;

    if (url.isEmpty() || !ota_version_newer(FIRMWARE_VERSION, ota.remoteVersion)) {
        ota.stage = OTA_IDLE;
        ota.active = false;
    } else {
        ota.stage = OTA_AVAILABLE;
        ota.active = true;
    }
}

static void fail(const String &message)
{
    ota.error = message;
    ota.stage = OTA_ERROR;
    DBG("ota: %s\n", message.c_str());
}

static void download_and_flash(const String &url)
{
    if (url.isEmpty()) { fail("empty firmware URL"); return; }
    ota.stage = OTA_DOWNLOADING;
    ota.downloaded = 0;
    ota.total = 0;
    ota.error = "";

    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(url)) { fail("http.begin() failed"); return; }
    int code = http.GET();
    if (code != 200) {
        fail("HTTP " + String(code));
        http.end();
        return;
    }
    int len = http.getSize();            // -1 when the server sends no length
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
#define OTA_STALL_MS 20000                          // the host test shortens this
#endif
    static const uint32_t STALL_MS = OTA_STALL_MS;  // no byte for this long: give up
    bool failed = false;
    while (http.connected() && (len < 0 || written < len)) {
        size_t avail = stream->available();
        if (!avail) {
            if (millis() - last_progress > STALL_MS) { failed = true; break; }
            delay(1);
            continue;
        }
        int n = stream->readBytes(buf, min(avail, sizeof(buf)));
        if (n <= 0) break;
        if (Update.write(buf, n) != (size_t)n) { failed = true; break; }
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
    if (ota.taskRunning) return;
    ota.taskRunning = true;
    xTaskCreatePinnedToCore(ota_task, "OTAUpdateTask", 0x3000, nullptr, 1, nullptr, 1);
}

int ota_progress_percent()
{
    if (ota.total > 0) return (int)((int64_t)ota.downloaded * 100 / ota.total);
    return 0;
}
