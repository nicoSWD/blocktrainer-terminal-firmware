// Host shim for the parts of Arduino.h the UI sources use. Must also compile as C: lv_conf.h
// includes this header into LVGL's C sources (LV_TICK_CUSTOM_INCLUDE).
#pragma once
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);             // virtual clock, advanced by the driver (also LVGL's tick)
void delay(uint32_t ms);
time_t host_time(time_t *t);       // fixed wall clock + virtual milliseconds
#ifdef __cplusplus
}
#endif

#define PROGMEM
#define PSTR(s) (s)
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

#ifdef __cplusplus
#include <algorithm>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

using std::min;
using std::max;

typedef uint8_t byte;
class __FlashStringHelper;
#define FPSTR(p) (reinterpret_cast<const __FlashStringHelper *>(p))
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(PSTR(s)))

class StringSumHelper;

// Subset of Arduino's String; explicit constructors match the real class so code that would not
// compile on the device does not compile here either.
class String {
protected:
    std::string s_;
    static std::string fmt_(const char *f, ...) __attribute__((format(printf, 1, 2)))
    {
        char buf[512];
        va_list ap;
        va_start(ap, f);
        vsnprintf(buf, sizeof(buf), f, ap);
        va_end(ap);
        return buf;
    }
    static std::string num_(unsigned long long v, int base)
    {
        if (base < 2 || base > 36) base = 10;
        if (v == 0) return "0";
        std::string r;
        while (v) { r.insert(r.begin(), "0123456789abcdefghijklmnopqrstuvwxyz"[v % base]); v /= base; }
        return r;
    }
    static std::string snum_(long long v, int base)
    {
        if (base == 10 && v < 0) return "-" + num_((unsigned long long)(-(v + 1)) + 1ULL, 10);
        return num_((unsigned long long)v, base);
    }
public:
    String(const char *c = "") { if (c) s_ = c; }
    String(const String &o) = default;
    String(String &&o) = default;
    String(const __FlashStringHelper *p) { if (p) s_ = (const char *)p; }
    explicit String(char c) : s_(1, c) {}
    explicit String(unsigned char v, unsigned char base = 10) : s_(num_(v, base)) {}
    explicit String(int v, unsigned char base = 10) : s_(snum_(v, base)) {}
    explicit String(unsigned int v, unsigned char base = 10) : s_(num_(v, base)) {}
    explicit String(long v, unsigned char base = 10) : s_(snum_(v, base)) {}
    explicit String(unsigned long v, unsigned char base = 10) : s_(num_(v, base)) {}
    explicit String(long long v, unsigned char base = 10) : s_(snum_(v, base)) {}
    explicit String(unsigned long long v, unsigned char base = 10) : s_(num_(v, base)) {}
    explicit String(float v, unsigned char dec = 2) : s_(fmt_("%.*f", (int)dec, (double)v)) {}
    explicit String(double v, unsigned char dec = 2) : s_(fmt_("%.*f", (int)dec, v)) {}

    String &operator=(const String &o) = default;
    String &operator=(String &&o) = default;
    String &operator=(const char *c) { s_ = c ? c : ""; return *this; }

    unsigned int length() const { return (unsigned int)s_.size(); }
    const char *c_str() const { return s_.c_str(); }
    bool isEmpty() const { return s_.empty(); }
    bool reserve(unsigned int n) { s_.reserve(n); return true; }

    String &operator+=(const String &o) { s_ += o.s_; return *this; }
    String &operator+=(const char *c) { if (c) s_ += c; return *this; }
    String &operator+=(char c) { s_ += c; return *this; }
    String &operator+=(int v) { s_ += snum_(v, 10); return *this; }
    String &operator+=(unsigned int v) { s_ += num_(v, 10); return *this; }
    String &operator+=(long v) { s_ += snum_(v, 10); return *this; }
    String &operator+=(unsigned long v) { s_ += num_(v, 10); return *this; }
    bool concat(const char *c, unsigned int n) { s_.append(c, n); return true; }
    bool concat(const char *c) { *this += c; return true; }
    bool concat(const String &o) { s_ += o.s_; return true; }
    bool concat(char c) { s_ += c; return true; }

    bool operator==(const String &o) const { return s_ == o.s_; }
    bool operator==(const char *c) const { return s_ == (c ? c : ""); }
    bool operator!=(const String &o) const { return s_ != o.s_; }
    bool operator!=(const char *c) const { return !(*this == c); }
    bool operator<(const String &o) const { return s_ < o.s_; }
    bool equals(const String &o) const { return s_ == o.s_; }
    bool equals(const char *c) const { return *this == c; }

    char operator[](unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
    char &operator[](unsigned int i) { static char dummy; dummy = 0; return i < s_.size() ? s_[i] : dummy; }
    char charAt(unsigned int i) const { return (*this)[i]; }

    int indexOf(char c, unsigned int from = 0) const
    {
        size_t p = s_.find(c, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const String &t, unsigned int from = 0) const
    {
        size_t p = s_.find(t.s_, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int lastIndexOf(char c) const
    {
        size_t p = s_.rfind(c);
        return p == std::string::npos ? -1 : (int)p;
    }
    String substring(unsigned int from) const { return from >= s_.size() ? String() : String(s_.substr(from).c_str()); }
    String substring(unsigned int from, unsigned int to) const
    {
        if (to < from) std::swap(to, from);
        if (from >= s_.size()) return String();
        return String(s_.substr(from, to - from).c_str());
    }
    void replace(char a, char b) { for (char &c : s_) if (c == a) c = b; }
    void replace(const String &a, const String &b)
    {
        if (a.s_.empty()) return;
        size_t p = 0;
        while ((p = s_.find(a.s_, p)) != std::string::npos) { s_.replace(p, a.s_.size(), b.s_); p += b.s_.size(); }
    }
    void remove(unsigned int idx) { if (idx < s_.size()) s_.erase(idx); }
    void remove(unsigned int idx, unsigned int count) { if (idx < s_.size()) s_.erase(idx, count); }
    void trim()
    {
        size_t b = s_.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) { s_.clear(); return; }
        size_t e = s_.find_last_not_of(" \t\r\n");
        s_ = s_.substr(b, e - b + 1);
    }
    void toLowerCase() { for (char &c : s_) c = (char)tolower((unsigned char)c); }
    void toUpperCase() { for (char &c : s_) c = (char)toupper((unsigned char)c); }
    bool startsWith(const String &p) const { return s_.compare(0, p.s_.size(), p.s_) == 0 && s_.size() >= p.s_.size(); }
    bool endsWith(const String &p) const
    {
        return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
    }
    long toInt() const { return atol(s_.c_str()); }
    float toFloat() const { return (float)atof(s_.c_str()); }
    double toDouble() const { return atof(s_.c_str()); }
};

class StringSumHelper : public String {
public:
    StringSumHelper(const String &s) : String(s) {}
};

inline String operator+(const String &a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, const char *b) { String r(a); r += b; return r; }
inline String operator+(const char *a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, char b) { String r(a); r += b; return r; }
inline String operator+(const String &a, int b) { String r(a); r += b; return r; }
inline String operator+(const String &a, unsigned int b) { String r(a); r += b; return r; }
inline String operator+(const String &a, long b) { String r(a); r += b; return r; }
inline String operator+(const String &a, unsigned long b) { String r(a); r += b; return r; }
inline bool operator==(const char *a, const String &b) { return b == a; }

class HardwareSerial {
public:
    void begin(unsigned long) {}
    int printf(const char *f, ...) __attribute__((format(printf, 2, 3)))
    {
        va_list ap;
        va_start(ap, f);
        int n = 0;
        if (getenv("HOST_VERBOSE")) n = vfprintf(stderr, f, ap);
        va_end(ap);
        return n;
    }
    void println(const char *s = "") { if (getenv("HOST_VERBOSE")) fprintf(stderr, "%s\n", s); }
    void println(const String &s) { println(s.c_str()); }
    void print(const char *s) { if (getenv("HOST_VERBOSE")) fprintf(stderr, "%s", s); }
    void print(const String &s) { print(s.c_str()); }
    size_t write(const uint8_t *b, size_t n) { if (getenv("HOST_VERBOSE")) fwrite(b, 1, n, stderr); return n; }
    size_t write(uint8_t c) { return write(&c, 1); }
    void flush() {}
};
extern HardwareSerial Serial;

#ifdef UI_HOST_TEST
#define time(x) host_time(x)       // fixed, reproducible clock for the screenshots (after all includes)
#endif

#endif
