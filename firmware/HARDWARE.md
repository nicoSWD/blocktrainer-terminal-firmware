# Hardware configuration

Read from the original image (`extracted/firmware.elf`) in Ghidra. Struct layouts decoded
against LovyanGFX 1.2.35 (`Panel_Device::config_t`, `ITouch::config_t`, `Bus_Parallel8::config_t`,
`Light_PWM::config_t`). Addresses are the original image's.

## Display (LovyanGFX), constructor at `4200af00`

| item | value |
|---|---|
| panel driver | `Panel_ST7796` (init table `3c1b164c` matches the ST7796 command list; the README's ST7701S is wrong) |
| bus | `Bus_Parallel8` (on the S3 this is the LCD_CAM peripheral, `platforms/esp32s3/`, so it does not share I2S0 with the audio), `freq_write` 48 MHz, `freq_read` default 8 MHz |
| WR / RD / RS | 47 / -1 / 0 |
| D0..D7 | 9, 46, 3, 8, 18, 17, 16, 15 |
| RST / CS / BUSY | 4 / -1 / -1 |
| geometry | memory 320x480, panel 320x480, offset 0,0, offset_rotation 0 |
| flags | readable true, invert true, rgb_order false, dlen_16bit false, bus_shared true |
| dummy read | pixel 8, bits 1 |
| backlight | `Light_PWM`, pin 45, 44100 Hz, PWM channel **0** (the byte at offset 7 is cleared in `4200af00`), not inverted |
| display_init (`4200b9c8`) | `lcd.init()` (vtable +0x1c, arg 1), then `lcd.setRotation(cfg.rotation)` (`42002f00`), then initial `setBrightness(60)` |

`cfg.rotation` (`3fc9baa8`) is a stored setting: NVS key `rotation`, int, default 1 (480x320
landscape). The web page's "Rotation 180" checkbox stores **3** when ticked and **1** when not
(`/save` at `420373f8`, page renderer `4203587c` shows it ticked when the value is 3), then
applies it live through `4202d814` (`setRotation`, redraw). Touch coordinates follow the panel
rotation inside LovyanGFX.

## Touch, `Touch_FT5x06`

| item | value |
|---|---|
| I2C | port **0**, address 0x38, SDA 6, SCL 5, 400 kHz |
| INT / RST | 7 / -1 |
| range | x 0..319, y 0..479 |
| bus_shared | **false** (public board config uses true) |

## Audio (I2S, `42032cac`)

Legacy `i2s_driver_install` on port 0, TX master, 22050 Hz, 16-bit, stereo (`RIGHT_LEFT`),
standard I2S format, 4 DMA buffers x 256 samples, `tx_desc_auto_clear` on, no APLL.
Pins (`3c21b4d4`): MCK -1, BCK 36, WS 35, DOUT 37, DIN -1.

## LED strip (FastLED, RMT, `42033e90`)

| item | value |
|---|---|
| data pin | 10 |
| LED count | 8 (`FUN_4206ac28(.., leds, 8, 0)`) |
| strip type 0 | `addLeds<WS2812, 10, GRB>`: T1/T2/T3 = 60/150/90 ticks (250/625/375 ns at 240 MHz); `showPixels` `420334cc` loads (G, R, B) |
| strip type 1 | `addLeds<WS2811, 10, RGB>`: 77/77/154 ticks (320/320/640 ns); `showPixels` `4203347c` loads (R, G, B). Selected when `ledStripType` != 0; the default is 1 |
| brightness | `ledBrightness * 255 / 100` (FastLED scale) |
| power limit | `setMaxPowerInMilliWatts(1500)` (`3fc9fb44` = 0x5dc, callback `4206c098`; USB budget safeguard) |
| correction | `setCorrection(CRGB(255, 50, 255))`, dither mode 1 |
| enable | `show` calls are skipped unless `ledEnabled` is set |

Animation state (globals `3fc9f6f8..3fc9f714`): `led_set` stores the colour and starts a
fade/pulse (`4203408c` sets `enabled`, colour, `fade=255`, mode 1); `42034150` restores the
previous state when the animation ends. Exact pulse waveform is not reconstructed.

## Open items

* FastLED version: pin one that still uses the legacy IDF 4.4 RMT driver (3.6.x to 3.7.x).
* Buzzer alert pattern (`buzzer_alert` task) is not read yet.
