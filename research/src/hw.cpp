// hw.cpp: buzzer and LED strip.
//
// VERIFIED: the buzzer sound is a pre-rendered I2S buffer (FUN_42032d1c): 1764 (0x6e4)
// 16-bit stereo samples of a decaying 300 Hz sine, amplitude = buzzerVolume/100 * 32000:
//     s[i] = A * exp(-75.398 * t) * sin(1883.447 * t),  t = i / 22050
// I2S sample rate 22050 (0x5622), 16 bit, 2 channels.
//
// NOT RECONSTRUCTED: the I2S pin assignment and the LED strip driver (FUN_42033e90 builds
// one of two strip configurations depending on ledStripType, brightness =
// ledBrightness*255/100, plus a small animation state machine at 0x3fc9f6f8..0x3fc9f710).
// These functions only keep the state so the rest of the code links.

#include <math.h>
#include "hw.h"
#include "config.h"

static int16_t *s_click;
static const int CLICK_SAMPLES = 0x6e4;

void buzzer_init()
{
    free(s_click);
    s_click = (int16_t *)malloc(CLICK_SAMPLES * 2 * sizeof(int16_t));
    if (!s_click) return;
    float amp = (cfg.buzzerVolume / 100.0f) * 32000.0f;
    for (int i = 0; i < CLICK_SAMPLES; i++) {
        float t = (float)i / 22050.0f;
        float v = expf(t * -75.398224f) * amp * sinf(t * 1883.447f);
        int16_t s = (int16_t)constrain(v, -32768.0f, 32767.0f);
        s_click[i * 2] = s;
        s_click[i * 2 + 1] = s;
    }
}

void buzzer_click() { /* i2s_write(s_click, ...) once the I2S pins are known */ }
void buzzer_alert() { /* "buzzer_alert" task: repeated beeps (pattern not reconstructed) */ }

static uint8_t s_r, s_g, s_b;
void led_init() {}
void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!cfg.ledEnabled) return;
    s_r = r; s_g = g; s_b = b;
}
