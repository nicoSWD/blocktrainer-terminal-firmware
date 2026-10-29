// Wi-Fi setup over a temporary access point, for devices without a usable screen.
#pragma once
#include <Arduino.h>

// Opens the access point "BTClock-Setup" and serves a form at http://192.168.4.1/ where the
// network and password are entered. On a successful connection the credentials are stored and the
// device restarts, so this function does not return in practice.
void wifi_portal_run();
