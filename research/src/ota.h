// ota.h: OTA update (manifest check + download/flash task).
#pragma once
#include <Arduino.h>

// Stage values written to 0x3fc9bb24 by FUN_4202233c / FUN_42020430.
enum OtaStage : uint8_t {
    OTA_IDLE        = 0,
    OTA_AVAILABLE   = 1,   // newer version found, waiting for the user to tap INSTALLIEREN
    OTA_CONFIRMED   = 2,   // UNVERIFIED: value set by the INSTALLIEREN callback (0x42020308)
    OTA_DOWNLOADING = 3,
    OTA_INSTALLING  = 4,   // download finished, Update.end() running
    OTA_DONE        = 5,
    OTA_ERROR       = 6,
};

struct OtaState {
    volatile OtaStage stage = OTA_IDLE;        // 0x3fc9bb24
    volatile int downloaded = 0;               // 0x3fc9bb28, bytes
    volatile int total = 0;                    // 0x3fc9bb2c
    String error;                              // 0x3fc9bb34
    String remoteVersion;                      // 0x3fc9bae8
    String firmwareUrl;                        // 0x3fc9baf8
    volatile bool active = false;              // 0x3fc9bb08, "OTA screen is up"
    volatile bool taskRunning = false;         // 0x3fc9bb32
};
extern OtaState ota;

void ota_check();                       // FUN_4202233c (checkForUpdates)
bool ota_version_newer(const String &current, const String &remote);   // FUN_42020330
void ota_start_task();                  // FUN_420209fc: spawns OTAUpdateTask once
int  ota_progress_percent();            // 0..100 for the download/installation bars
