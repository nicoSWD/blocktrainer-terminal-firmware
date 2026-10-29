// ota.cpp: firmware update. Verified against FUN_4202233c (check), FUN_42020330
// (version compare), FUN_42020430 (download + flash) and FUN_420209fc (task start).

#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "ota.h"
#include "config.h"

OtaState ota;

// FUN_42020330: sscanf("%d.%d.%d") on both strings, true if remote > current.
bool ota_version_newer(const String &current, const String &remote)
{
    int c[3] = {0, 0, 0}, r[3] = {0, 0, 0};
    sscanf(current.c_str(), "%d.%d.%d", &c[0], &c[1], &c[2]);
    sscanf(remote.c_str(), "%d.%d.%d", &r[0], &r[1], &r[2]);
    for (int i = 0; i < 3; i++) {
        if (r[i] != c[i]) return r[i] > c[i];
    }
    return false;
}

// FUN_4202233c. Fetches the manifest (esp-web-tools style firmware.json):
//   { "version": "x.y.z", "builds": [ { "chipFamily": "ESP32-S3",
//       "parts": [ { "path": ".../firmware.bin", "label"?: ..., "offset": ... } ] } ] }
// picks the ESP32-S3 build, takes the first part whose path contains "firmware.bin"
// and sets stage AVAILABLE when the manifest version is newer than FIRMWARE_VERSION.
void ota_check()
{
    DBG("🔍 OTA: checkForUpdates() gestartet\n");
    if (WiFi.status() != WL_CONNECTED) {
        DBG("⚠️ OTA: WLAN nicht verbunden – Abbruch.\n");
        return;
    }
    String url = URL_UPDATE;
    if (url.isEmpty()) {
        DBG("⚠️ OTA: currentState.updateUrl ist leer – Abbruch.\n");
        return;
    }
    DBG("🌐 OTA: Abruf-URL: %s\n", url.c_str());

    HTTPClient http;
    if (!http.begin(url)) {
        DBG("❌ OTA: http.begin() fehlgeschlagen.\n");
        return;
    }
    int code = http.GET();
    DBG("📡 OTA: HTTP GET -> %d\n", code);
    if (code != 200) {
        DBG("❌ OTA: HTTP-Fehlercode: %d\n", code);
        ota.stage = OTA_IDLE;
        ota.active = false;
        http.end();
        return;
    }
    String payload = http.getString();
    DBG("📦 OTA: Payload-Länge: %d Bytes\n", (int)payload.length());

    DynamicJsonDocument doc(2048);                      // MemoryPool of 0x800 bytes
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        DBG("❌ OTA: JSON-Parse-Fehler: %s\n", err.c_str());
        http.end();
        return;
    }

    ota.remoteVersion = doc["version"] | "";
    DBG("🆕 OTA: Remote-Version: %s\n", ota.remoteVersion.c_str());
    DBG("🏷️  OTA: Aktuelle Version: %s\n", FIRMWARE_VERSION);

    String fw;
    JsonArray builds = doc["builds"];
    if (builds.isNull()) {
        DBG("⚠️ OTA: JSON enthält kein 'builds'-Array.\n");
    } else {
        for (JsonObject b : builds) {
            const char *fam = b["chipFamily"] | "";
            DBG("🔎 OTA: Prüfe Build chipFamily='%s'\n", fam);
            if (strcmp(fam, "ESP32-S3") != 0) continue;
            JsonArray parts = b["parts"];
            if (parts.isNull()) {
                DBG("⚠️ OTA: 'parts' fehlt in passendem Build.\n");
                break;
            }
            for (JsonObject p : parts) {
                const char *path = p["path"] | "";
                const char *label = p["label"] | "";
                DBG("   • Teil: label='%s', path='%s'\n", label, path);
                if (strstr(path, "firmware.bin")) {
                    fw = path;
                    DBG("✅ OTA: Firmware-URL gefunden: %s\n", fw.c_str());
                    break;
                }
            }
            break;
        }
    }
    ota.firmwareUrl = fw;

    if (fw.isEmpty() || !ota_version_newer(FIRMWARE_VERSION, ota.remoteVersion)) {
        DBG("✅ OTA: Bereits auf neuester Version – Stage=Idle.\n");
        ota.stage = OTA_IDLE;
        ota.active = false;
    } else {
        DBG("⬇️ OTA: Neue Version verfügbar – Stage=Available.\n");
        ota.stage = OTA_AVAILABLE;
        ota.active = true;
    }
    http.end();
    DBG("🔚 OTA: checkForUpdates() beendet\n");
}

// FUN_42020430
static void ota_perform(const String &url)
{
    DBG("⬇️ OTA: performOTAUpdate() gestartet\n");
    if (url.isEmpty()) {
        ota.error = "Leere Firmware-URL";
        ota.stage = OTA_ERROR;
        DBG("❌ OTA: Leere Firmware-URL.\n");
        return;
    }
    ota.stage = OTA_DOWNLOADING;
    ota.downloaded = 0;
    ota.total = 0;
    ota.error = "";

    HTTPClient http;
    if (!http.begin(url)) {
        ota.error = "http.begin() fehlgeschlagen";
        ota.stage = OTA_ERROR;
        DBG("❌ OTA: http.begin() fehlgeschlagen.\n");
        return;
    }
    int code = http.GET();
    DBG("📡 OTA: HTTP GET -> %d\n", code);
    if (code != 200) {
        ota.error = "HTTP " + String(code);
        ota.stage = OTA_ERROR;
        DBG("❌ OTA: Unerwarteter HTTP-Code: %d\n", code);
        http.end();
        return;
    }
    int len = http.getSize();
    ota.total = len;
    DBG("📦 OTA: Content-Length: %d\n", len);
    if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
        ota.error = "Update.begin() fehlgeschlagen";
        ota.stage = OTA_ERROR;
        DBG("❌ OTA: Update.begin() fehlgeschlagen (nicht genug Platz?).\n");
        http.end();
        return;
    }
    DBG("⬇️ OTA: Download startet …\n");

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buf[1024];
    uint32_t last_log = millis();
    int written = 0;
    while (http.connected() && (len < 0 || written < len)) {
        size_t avail = stream->available();
        if (!avail) { delay(1); continue; }
        int n = stream->readBytes(buf, min(avail, sizeof(buf)));
        if (n <= 0) break;
        if (Update.write(buf, n) != (size_t)n) break;
        written += n;
        ota.downloaded = written;
        if (millis() - last_log > 750) {                       // 0x2ee
            if (len > 0) DBG("🔄 Download-Fortschritt: %d%%  (%lu/%d Bytes)\n", written * 100 / len, (unsigned long)written, len);
            else         DBG("🔄 Download-Bytes: %lu (unknown total)\n", (unsigned long)written);
            last_log = millis();
        }
    }
    DBG("✅ OTA: Download abgeschlossen, beginne Installation…\n");
    ota.stage = OTA_INSTALLING;
    if (Update.end(true)) {
        ota.stage = OTA_DONE;
        DBG("✅ OTA: Installation abgeschlossen.\n");
    } else {
        ota.error = "Update.end() Fehler " + String(Update.getError());
        ota.stage = OTA_ERROR;
        DBG("❌ OTA: Update.end() Fehlercode: %d\n", Update.getError());
    }
    http.end();
}

// body of OTAUpdateTask (LAB_420209d8): perform, then clear the running flag and exit
static void ota_task(void *)
{
    DBG("⚙️ OTA: Installation läuft …\n");
    ota_perform(ota.firmwareUrl);
    ota.taskRunning = false;
    vTaskDelete(nullptr);
}

void ota_start_task()
{
    if (!ota.taskRunning) {
        ota.taskRunning = true;
        xTaskCreatePinnedToCore(ota_task, "OTAUpdateTask", 0x2000, nullptr, 1, nullptr, 1);
    }
}

int ota_progress_percent()
{
    if (ota.total > 0) return (int)((int64_t)ota.downloaded * 100 / ota.total);
    return 0;
}
