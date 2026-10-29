// Stand-in for the LovyanGFX `Display` class of ui_core.cpp (selected by UI_HOST_TEST).
// The flush callback of the firmware writes into a 480x320 RGB565 framebuffer through it.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

namespace lgfx {
struct rgb565_t { uint16_t v; };
}

static const int HOST_W = 480, HOST_H = 320;
extern uint16_t g_host_fb[HOST_W * HOST_H];
extern bool g_host_touch_down;
extern int g_host_touch_x, g_host_touch_y;
extern int g_host_brightness;

class Display {
    int rot_ = 1;
    int x0_ = 0, y0_ = 0, w_ = 0, h_ = 0, pos_ = 0;
public:
    bool init() { return true; }
    void setRotation(int r) { rot_ = r; }
    int getRotation() const { return rot_; }
    void setBrightness(uint8_t b) { g_host_brightness = b; }
    bool getTouch(uint16_t *x, uint16_t *y)
    {
        if (!g_host_touch_down) return false;
        *x = (uint16_t)g_host_touch_x;
        *y = (uint16_t)g_host_touch_y;
        return true;
    }
    void startWrite() {}
    void endWrite() {}
    void setAddrWindow(int x, int y, int w, int h)
    {
        if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > HOST_W || y + h > HOST_H) {
            fprintf(stderr, "HOST: flush window outside the panel: x=%d y=%d w=%d h=%d\n", x, y, w, h);
            abort();
        }
        x0_ = x; y0_ = y; w_ = w; h_ = h; pos_ = 0;
    }
    void writePixels(const lgfx::rgb565_t *px, uint32_t len)
    {
        if ((int)len != w_ * h_) { fprintf(stderr, "HOST: flush length mismatch\n"); abort(); }
        for (uint32_t i = 0; i < len; i++) {
            int x = x0_ + (int)(i % (uint32_t)w_), y = y0_ + (int)(i / (uint32_t)w_);
            g_host_fb[y * HOST_W + x] = px[i].v;
        }
    }
};
