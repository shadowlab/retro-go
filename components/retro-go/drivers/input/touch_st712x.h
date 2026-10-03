#pragma once

#include <stdbool.h>
#include <stdint.h>

// Capacitive touch of the ST7123/ST7121 panels (M5Stack Tab5), turned into key presses.
void rg_touch_init(void);
uint32_t rg_touch_read(void);
// The latest touch point in logical coordinates, for UI that wants positions instead of gestures (the on-screen
// keyboard). Returns true while a finger is down. A tap that began and ended between two calls is reported once,
// at its last position, so that short taps are not lost.
bool rg_touch_get_point(int *x, int *y);
