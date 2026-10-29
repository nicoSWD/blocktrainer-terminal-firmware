// Copy to secrets.h (gitignored) to compile a Wi-Fi network into the image. The device tries it
// first, then the credentials stored in NVS, then the setup wizard or access point. The built-in
// network is never written to NVS, so the stock credentials stay as they were.
// Note: the password ends up in clear text in firmware.bin; do not share images built with it.
#pragma once
#define BUILTIN_WIFI_SSID "my-network"
#define BUILTIN_WIFI_PASS "my-password"
