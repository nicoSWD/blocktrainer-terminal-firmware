// Configuration web server (port 80).
//
//   GET  /             settings page
//   GET  /status.json  live values
//   POST /save         stores the form fields
//   GET  /reset        clears all stored settings
//   any  /restart      restarts the device
#pragma once
#include <Arduino.h>

void web_begin();
void web_loop();                            // call from loop()
extern volatile bool g_restart_requested;   // set by /restart, silences the Wi-Fi reconnect logic
