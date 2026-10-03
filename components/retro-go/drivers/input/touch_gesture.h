#pragma once

// Turns a single touch point into d-pad/button presses so the existing menus can be driven by touch.
// No ESP-IDF dependencies, so it can be tested on a host.
//
//   drag up/down/left/right ... UP/DOWN/LEFT/RIGHT (one press per STEP pixels, content follows the finger)
//   tap ........................ A
//   long press ................. B
//
// When the UI isn't active (in game) only a tap in the top-left corner is reported, as MENU.

#include <stdbool.h>
#include <stdint.h>

#define TOUCH_STEP_X 48     // logical pixels per LEFT/RIGHT press
#define TOUCH_STEP_Y 24     // logical pixels per UP/DOWN press (about a menu row)
#define TOUCH_TAP_MAX_US 400000
#define TOUCH_LONG_US 600000
#define TOUCH_PULSE_US 60000
#define TOUCH_GAP_US 40000
#define TOUCH_CORNER_W 56
#define TOUCH_CORNER_H 40

typedef struct
{
    bool down;
    bool moved;
    bool long_fired;
    int axis; // 0 undecided, 1 horizontal, 2 vertical
    int start_x, start_y, anchor_x, anchor_y;
    int64_t down_time;
    uint32_t pulse_keys;
    int64_t pulse_end;
    uint32_t queued; // keys waiting for the current pulse to finish
} touch_gesture_t;

// x/y are logical coordinates (RG_SCREEN_WIDTH x RG_SCREEN_HEIGHT). The returned value is a mask in
// the key bit order of rg_key_t, which the caller passes in as the five outputs it needs.
typedef struct
{
    uint32_t up, down, left, right, a, b, menu;
} touch_keys_t;

uint32_t touch_gesture_update(touch_gesture_t *g, const touch_keys_t *keys, bool ui_active, bool touching, int x,
                              int y, int64_t now_us);
