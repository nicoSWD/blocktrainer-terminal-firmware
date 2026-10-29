// Buzzer (I2S speaker) and the 8-LED status strip. Pin numbers are in HARDWARE.md.
#pragma once
#include <Arduino.h>

bool hw_is_ready();             // hw_init() ran
void hw_init();                 // I2S driver, click waveform, LED strip
void hw_settings_changed();     // re-reads volume and LED brightness from cfg

// Buzzer: both play asynchronously and are ignored while a sound is still playing.
void buzzer_click();            // short tick on touch
void buzzer_alert();            // three rising tones (price alert)

// LED strip. Colours fade in and out; led_tick() must be called regularly (every few ms).
void led_pulse(uint8_t r, uint8_t g, uint8_t b);   // breathing in the given colour
void led_solid(uint8_t r, uint8_t g, uint8_t b);   // steady colour
void led_off();                                    // fade out
void led_save();                                   // remember the current state ...
void led_restore();                                // ... and bring it back later
void led_tick();
