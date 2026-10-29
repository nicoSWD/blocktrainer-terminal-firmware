// Host wrapper around the real ArduinoJson. A JSON slot is 16 bytes on the ESP32 but 32 bytes on a
// 64-bit host, so `DynamicJsonDocument doc(24576)` would run out of memory here for documents that
// fit on the device. The wrapper scales every capacity by the slot size ratio.
#pragma once
#include_next <ArduinoJson.h>

class HostDynamicJsonDocument : public ArduinoJson::DynamicJsonDocument {
public:
    explicit HostDynamicJsonDocument(size_t capacity)
        : ArduinoJson::DynamicJsonDocument(capacity * (JSON_ARRAY_SIZE(1) / 16)) {}
};
#define DynamicJsonDocument HostDynamicJsonDocument
