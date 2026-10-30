#include <math.h>
#include <driver/i2s.h>
#include <FastLED.h>
#include "hw.h"
#include "config.h"

// ---------------------------------------------------------------- pins (see HARDWARE.md)

static const int PIN_I2S_BCK = 36;
static const int PIN_I2S_WS = 35;
static const int PIN_I2S_DOUT = 37;
static const int PIN_LED_DATA = 10;

static const int LED_COUNT = 8;
static const uint32_t LED_MAX_MILLIWATTS = 1500;

// ---------------------------------------------------------------- buzzer

static const i2s_port_t I2S_PORT = I2S_NUM_0;
static const int SAMPLE_RATE = 22050;

static int16_t *s_click = nullptr;
static const int CLICK_FRAMES = 1764;
static volatile bool s_playing = false;
static bool s_hw_ok = false; // set once hw_init() ran; every function is a no-op before

static void i2s_start()
{
    i2s_config_t config = {};
    config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = SAMPLE_RATE;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 4;
    config.dma_buf_len = 256;
    config.use_apll = false;
    config.tx_desc_auto_clear = true;
    i2s_driver_install(I2S_PORT, &config, 0, nullptr);

    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = PIN_I2S_BCK;
    pins.ws_io_num = PIN_I2S_WS;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_PORT, &pins);
}

// Decaying 300 Hz sine, amplitude proportional to the volume setting.
static void render_click()
{
    free(s_click);
    s_click = (int16_t *)malloc(CLICK_FRAMES * 2 * sizeof(int16_t));
    if (!s_click) {
        return;
    }
    float amplitude = cfg.buzzerVolume / 100.0f * 32000.0f;
    for (int i = 0; i < CLICK_FRAMES; i++) {
        float t = (float)i / SAMPLE_RATE;
        float v = expf(-75.398224f * t) * amplitude * sinf(1883.447f * t);
        int16_t s = (int16_t)constrain(v, -32768.0f, 32767.0f);
        s_click[i * 2] = s;
        s_click[i * 2 + 1] = s;
    }
}

static void write_samples(const int16_t *data, size_t bytes)
{
    size_t written = 0;
    i2s_write(I2S_PORT, data, bytes, &written, portMAX_DELAY);
}

static void click_task(void *)
{
    if (cfg.buzzerVolume != 0 && s_click) {
        write_samples(s_click, CLICK_FRAMES * 4);
        i2s_zero_dma_buffer(I2S_PORT);
    }
    s_playing = false;
    vTaskDelete(nullptr);
}

// Three tones (A4, E5, A5), 150 ms each: full level for 100 ms, then a linear fade,
// followed by 40 ms of silence.
static void alert_task(void *)
{
    static const int TONES_HZ[3] = {440, 660, 880};
    static const int TONE_FRAMES = 3307;
    static const int FULL_FRAMES = 2204;
    static const int GAP_FRAMES = 882;
    const int frames = TONE_FRAMES + GAP_FRAMES;

    if (cfg.buzzerVolume != 0) {
        int16_t *buf = (int16_t *)malloc(frames * 2 * sizeof(int16_t));
        if (buf) {
            float amplitude = cfg.buzzerVolume * 280.0f;
            for (int tone = 0; tone < 3; tone++) {
                float w = 2.0f * (float)M_PI * TONES_HZ[tone] / SAMPLE_RATE;
                for (int i = 0; i < TONE_FRAMES; i++) {
                    float env =
                        i < FULL_FRAMES ? 1.0f : (float)(TONE_FRAMES - i) / (TONE_FRAMES - FULL_FRAMES);
                    int16_t s = (int16_t)(amplitude * env * sinf(i * w));
                    buf[i * 2] = s;
                    buf[i * 2 + 1] = s;
                }
                memset(buf + TONE_FRAMES * 2, 0, GAP_FRAMES * 2 * sizeof(int16_t));
                write_samples(buf, frames * 4);
            }
            free(buf);
        }
        i2s_zero_dma_buffer(I2S_PORT);
    }
    s_playing = false;
    vTaskDelete(nullptr);
}

static void start_sound(TaskFunction_t fn, const char *name)
{
    if (!s_hw_ok || s_playing) {
        return;
    }
    s_playing = true;
    xTaskCreatePinnedToCore(fn, name, 0x1000, nullptr, 1, nullptr, 0);
}

void buzzer_click()
{
    start_sound(click_task, "buzzer_click");
}
void buzzer_alert()
{
    start_sound(alert_task, "buzzer_alert");
}

// ---------------------------------------------------------------- LED strip

static CRGB s_leds[LED_COUNT];

enum LedMode { LED_OFF = 0, LED_PULSE = 1, LED_SOLID = 2 };

struct LedState {
    LedMode mode = LED_OFF;
    CRGB color = CRGB::Black;
};

static LedState s_led;
static LedState s_saved;
static uint8_t s_level = 0;  // current fade level, 0..255
static uint8_t s_target = 0; // level the fade is heading to
static uint8_t s_phase = 0;  // breathing phase
static uint32_t s_last_tick = 0;
static const uint32_t LED_FRAME_MS = 20;

static void led_set(LedMode mode, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_hw_ok || !cfg.ledEnabled) {
        return;
    }
    s_led.mode = mode;
    s_led.color = CRGB(r, g, b);
    s_phase = 0;
    s_target = 255;
}

void led_pulse(uint8_t r, uint8_t g, uint8_t b)
{
    led_set(LED_PULSE, r, g, b);
}
void led_solid(uint8_t r, uint8_t g, uint8_t b)
{
    led_set(LED_SOLID, r, g, b);
}
void led_off()
{
    s_target = 0;
}
void led_save()
{
    s_saved = s_led;
}

void led_restore()
{
    if (!s_hw_ok) {
        return;
    }
    if (!cfg.ledEnabled || s_saved.mode == LED_OFF) {
        s_target = 0;
        return;
    }
    s_led = s_saved;
    s_target = 255;
}

// Fades up in eighths of the remaining distance and down in halves.
void led_tick()
{
    if (!s_hw_ok) {
        return;
    }
    uint32_t now = millis();
    if (now - s_last_tick < LED_FRAME_MS) {
        return;
    }
    s_last_tick = now;

    if (!cfg.ledEnabled) {
        s_led.mode = LED_OFF;
        s_target = 0;
    }
    if (s_level != s_target) {
        int diff = (int)s_target - (int)s_level;
        int step = diff / (diff > 0 ? 8 : 2);
        if (step == 0) {
            step = diff > 0 ? 1 : -1;
        }
        s_level += step;
    }

    if (s_led.mode == LED_OFF || (s_level == 0 && s_target == 0)) {
        if (s_led.mode != LED_OFF) {
            s_led.mode = LED_OFF;
        }
        FastLED.clear();
    } else if (s_led.mode == LED_PULSE) {
        s_phase += 2;
        // sine breathing between ~0 and 86 % of the colour, scaled by the fade level
        uint16_t scale = ((uint16_t)sin8(s_phase) * 0xdd >> 8);
        scale = ((scale * (s_level + 1) >> 8) & 0xff) + 1;
        CRGB c = s_led.color;
        c.nscale8_video(scale > 255 ? 255 : scale);
        fill_solid(s_leds, LED_COUNT, c);
    } else {
        CRGB c = s_led.color;
        c.nscale8_video(s_level);
        fill_solid(s_leds, LED_COUNT, c);
    }
    FastLED.show();
}

static void led_begin()
{
    if (cfg.ledStripType == 0) {
        FastLED.addLeds<WS2812, PIN_LED_DATA, GRB>(s_leds, LED_COUNT);
    } else {
        FastLED.addLeds<WS2811, PIN_LED_DATA, RGB>(s_leds, LED_COUNT);
    }
    FastLED.setCorrection(CRGB(255, 50, 255));
    FastLED.setDither(BINARY_DITHER);
    FastLED.setMaxPowerInMilliWatts(LED_MAX_MILLIWATTS);
    FastLED.setBrightness(cfg.ledBrightness * 255 / 100);
    FastLED.clear(true);
}

// ---------------------------------------------------------------- init

bool hw_is_ready()
{
    return s_hw_ok;
}

void hw_init()
{
    i2s_start();
    render_click();
    led_begin();
    s_hw_ok = true;
}

void hw_settings_changed()
{
    if (!s_hw_ok) {
        return;
    }
    if (!s_playing) {
        render_click(); // the click task reads the buffer while playing
    }
    FastLED.setBrightness(cfg.ledBrightness * 255 / 100);
}
