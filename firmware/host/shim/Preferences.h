// In-memory Preferences (NVS) for the host tests.
#pragma once
#include <map>
#include <string>
#include <stdlib.h>
#include "Arduino.h"

class Preferences {
    std::map<std::string, std::string> m_;
    template <class T> size_t put_(const char *key, T v)
    {
        m_[key] = std::to_string(v);
        return sizeof(T);
    }
public:
    bool begin(const char *, bool = false) { return true; }
    void end() {}
    bool clear() { m_.clear(); return true; }
    bool remove(const char *key) { return m_.erase(key) > 0; }
    bool isKey(const char *key) { return m_.count(key) > 0; }

    size_t putBool(const char *k, bool v) { return put_(k, (int)v); }
    size_t putInt(const char *k, int32_t v) { return put_(k, v); }
    size_t putUInt(const char *k, uint32_t v) { return put_(k, v); }
    size_t putLong(const char *k, int32_t v) { return put_(k, v); }
    size_t putLong64(const char *k, int64_t v) { return put_(k, v); }
    size_t putULong64(const char *k, uint64_t v) { return put_(k, v); }
    size_t putUChar(const char *k, uint8_t v) { return put_(k, (int)v); }
    size_t putFloat(const char *k, float v) { return put_(k, v); }
    size_t putDouble(const char *k, double v) { return put_(k, v); }
    size_t putString(const char *k, const String &v) { m_[k] = v.c_str(); return v.length(); }
    size_t putString(const char *k, const char *v) { m_[k] = v; return strlen(v); }

    bool getBool(const char *k, bool d = false) { return isKey(k) ? atoi(m_[k].c_str()) != 0 : d; }
    int32_t getInt(const char *k, int32_t d = 0) { return isKey(k) ? (int32_t)atoll(m_[k].c_str()) : d; }
    uint32_t getUInt(const char *k, uint32_t d = 0) { return isKey(k) ? (uint32_t)atoll(m_[k].c_str()) : d; }
    int64_t getLong64(const char *k, int64_t d = 0) { return isKey(k) ? atoll(m_[k].c_str()) : d; }
    uint8_t getUChar(const char *k, uint8_t d = 0) { return isKey(k) ? (uint8_t)atoi(m_[k].c_str()) : d; }
    float getFloat(const char *k, float d = 0) { return isKey(k) ? (float)atof(m_[k].c_str()) : d; }
    double getDouble(const char *k, double d = 0) { return isKey(k) ? atof(m_[k].c_str()) : d; }
    String getString(const char *k, const String &d = String()) { return isKey(k) ? String(m_[k].c_str()) : d; }
};
