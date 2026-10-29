// Host stand-in for the Arduino Update library: records what the OTA code does to it.
#pragma once
#include "Arduino.h"

#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#define UPDATE_ERROR_OK 0
#define UPDATE_ERROR_SIZE 11
#define UPDATE_ERROR_WRITE 1
#define UPDATE_ERROR_MAGIC_BYTE 12
#define UPDATE_ERROR_ABORT 9

class UpdateClass {
public:
    // what a test can inspect or inject
    bool begun = false, ended = false, aborted = false;
    size_t expected = 0, written = 0;
    size_t capacity = 3342336;          // one app slot
    size_t fail_write_after = (size_t)-1;
    std::vector<uint8_t> data;
    uint8_t err = UPDATE_ERROR_OK;

    void reset()
    {
        begun = ended = aborted = false;
        expected = written = 0;
        fail_write_after = (size_t)-1;
        data.clear();
        err = UPDATE_ERROR_OK;
    }
    bool begin(size_t size = UPDATE_SIZE_UNKNOWN)
    {
        if (size != UPDATE_SIZE_UNKNOWN && size > capacity) { err = UPDATE_ERROR_SIZE; return false; }
        begun = true;
        expected = size;
        written = 0;
        return true;
    }
    size_t write(uint8_t *buf, size_t len)
    {
        if (err || !begun || aborted) return 0;
        if (written + len > fail_write_after) { err = UPDATE_ERROR_WRITE; return 0; }
        if (expected != UPDATE_SIZE_UNKNOWN && written + len > expected) { err = UPDATE_ERROR_SIZE; return 0; }
        data.insert(data.end(), buf, buf + len);
        written += len;
        return len;
    }
    bool end(bool evenIfRemaining = false)
    {
        if (err || !begun || aborted) return false;
        if (expected != UPDATE_SIZE_UNKNOWN && written != expected && !evenIfRemaining) { err = UPDATE_ERROR_SIZE; return false; }
        if (data.empty() || data[0] != 0xE9) { err = UPDATE_ERROR_MAGIC_BYTE; return false; }   // image validation
        ended = true;
        return true;
    }
    void abort() { aborted = true; err = UPDATE_ERROR_ABORT; }
    bool hasError() const { return err != 0; }
    uint8_t getError() const { return err; }
    const char *errorString() const { return err ? "error" : "no error"; }
};
extern UpdateClass Update;
