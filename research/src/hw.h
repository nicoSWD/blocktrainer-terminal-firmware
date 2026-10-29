// hw.h: buzzer and status LED strip.
#pragma once
#include <Arduino.h>

void buzzer_init();                  // FUN_42032fb8 = FUN_42032cac (I2S setup) + FUN_42032d1c (click waveform)
void buzzer_click();                 // FUN_42032fcc, "buzzer_click"
void buzzer_alert();                 // FUN_42033000, spawns the "buzzer_alert" task (stack 0x1000)
void led_init();                     // FUN_42033e90
void led_set(uint8_t r, uint8_t g, uint8_t b);   // FUN_4203408c (ignored when cfg.ledEnabled is false)
