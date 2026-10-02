#pragma once

#include <stdint.h>

// Capacitive touch of the ST7123/ST7121 panels (M5Stack Tab5), turned into key presses.
void rg_touch_init(void);
uint32_t rg_touch_read(void);
