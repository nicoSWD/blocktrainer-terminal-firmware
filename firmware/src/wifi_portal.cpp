#include <WiFi.h>
#include <WebServer.h>
#include "wifi_portal.h"
#include "wifi_link.h"
#include "config.h"
#include "bootguard.h"
#include "wifi_scan.h"

static const char *AP_NAME = "BTClock-Setup";

static String html_escape(const String &s)
{
    String out;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '&') {
            out += "&amp;";
        } else if (c == '<') {
            out += "&lt;";
        } else if (c == '>') {
            out += "&gt;";
        } else if (c == '"') {
            out += "&quot;";
        } else {
            out += c;
        }
    }
    return out;
}

static String form_page(const String &message, const String &options)
{
    String h =
        F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>BT Terminal Wi-Fi</title><style>body{font-family:sans-serif;background:#111;color:#eee;"
          "max-width:420px;margin:30px auto;padding:0 16px}input,button{width:100%;padding:12px;"
          "margin:6px 0;font-size:16px;box-sizing:border-box}button{background:#fa0;border:0}"
          ".msg{color:#fa0}</style></head><body><h2>Wi-Fi setup</h2>");
    if (message.length()) {
        h += "<p class='msg'>" + message + "</p>";
    }
    h += F("<form method='post' action='/connect'><label>Network</label>"
           "<input name='ssid' list='nets' autocomplete='off' required><datalist id='nets'>");
    h += options;
    h += F("</datalist><label>Password</label><input name='pass' type='password'>"
           "<button type='submit'>Connect</button></form></body></html>");
    return h;
}

void wifi_portal_run()
{
    Serial.printf("portal: starting access point %s\n", AP_NAME);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_NAME);

    String options;
    WifiScan scan = wifi_scan(WIFI_SCAN_ATTEMPTS, [] {
        boot_guard_feed();
    });
    DBG("portal: scan found %d access points (code %d, %d tries)\n", (int)scan.aps.size(), scan.code,
        scan.attempts);
    for (const WifiAp &ap : wifi_networks(scan.aps, 30)) {
        options += "<option value=\"" + html_escape(ap.ssid) + "\">";
    }

    WebServer server(80);
    server.on("/", HTTP_GET, [&] {
        server.send(200, "text/html", form_page("", options));
    });
    server.on("/connect", HTTP_POST, [&] {
        String ssid = server.arg("ssid"), pass = server.arg("pass");
        if (ssid.isEmpty()) {
            server.send(200, "text/html", form_page("Please enter a network name.", options));
            return;
        }

        WiFi.begin(ssid.c_str(), pass.c_str());
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
            delay(250);
        }
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.disconnect(false, false);
            server.send(200, "text/html",
                        form_page("Connection failed. Check the name and the password.", options));
            return;
        }
        g_ssid = ssid;
        g_pass = pass;
        wifi_save_credentials();
        server.send(200, "text/html", form_page("Connected. The device is restarting.", ""));
        delay(1500);
        ESP.restart();
    });
    server.on("/diag", HTTP_GET, [&] {
        server.send(200, "text/plain", diag_report());
    });
    server.begin();

    // While nobody is using the form, keep trying the stored network: the router may simply
    // have been slower to boot than this device. A restart gives a clean station state.
    uint32_t last_try = millis();
    for (;;) {
        server.handleClient();
        delay(2);
        boot_guard_feed();
        if (g_wifi_ok && WiFi.status() == WL_CONNECTED) { // the on-screen wizard got there first
            wifi_save_credentials();
            ESP.restart();
        }
        if (g_ssid.isEmpty() || millis() - last_try < 30000 || WiFi.softAPgetStationNum() > 0) {
            continue;
        }
        last_try = millis();
        WiFi.begin(g_ssid.c_str(), g_pass.c_str());
        for (uint32_t t0 = millis(); WiFi.status() != WL_CONNECTED && millis() - t0 < 15000; delay(100)) {
            boot_guard_feed();
            server.handleClient();
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("portal: stored network is back, restarting");
            ESP.restart();
        }
        WiFi.disconnect(false, false);
    }
}
