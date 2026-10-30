// Over-the-air update: manifest check and the download/flash task.
#pragma once
#include <Arduino.h>

enum OtaStage : uint8_t {
    OTA_IDLE = 0,
    OTA_AVAILABLE = 1, // newer version found, waiting for the user
    OTA_CONFIRMED = 2, // user accepted
    OTA_DOWNLOADING = 3,
    OTA_INSTALLING = 4, // download finished, finalising the image
    OTA_DONE = 5,
    OTA_ERROR = 6,
};

struct OtaState {
    volatile OtaStage stage = OTA_IDLE;
    volatile int downloaded = 0; // bytes
    volatile int total = 0;      // bytes, 0 when unknown
    String error;
    String remoteVersion;
    String firmwareUrl;
    volatile bool active = false; // the OTA prompt/progress screen is up
    volatile bool taskRunning = false;
};
extern OtaState ota;

void ota_check();                                                    // reads the manifest, sets `stage`
bool ota_version_newer(const String &current, const String &remote); // "%d.%d.%d" comparison
void ota_start_task();                                               // starts the update task once
int ota_progress_percent();                                          // 0..100
