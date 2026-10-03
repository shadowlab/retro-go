#pragma once

// Geometry and hit testing of the on-screen keyboard for touch screens. No ESP-IDF dependencies, so it can
// be tested on a host. Coordinates are logical (RG_SCREEN_WIDTH x RG_SCREEN_HEIGHT).
//
// The key grid is columns x rows keys of key_w x key_h. When button_h is not 0 a row of buttons with the
// width of the grid follows it: layout switch, space, backspace, cancel and OK.

#include <stdbool.h>

typedef enum
{
    TKB_NONE = 0, // outside of the keyboard
    TKB_KEY,
    TKB_LAYOUT,
    TKB_SPACE,
    TKB_BACKSPACE,
    TKB_CANCEL,
    TKB_OK,
} tkb_target_t;

typedef struct
{
    int x, y; // top left corner of the key grid
    int key_w, key_h;
    int columns, rows;
    int button_h; // 0 = no button row
} tkb_geometry_t;

typedef struct
{
    tkb_target_t target;
    int key; // index in the grid for TKB_KEY, otherwise -1
} tkb_hit_t;

tkb_hit_t tkb_hit(const tkb_geometry_t *g, int px, int py);

// Rectangle of a button of the button row. Returns false if there is no such button.
bool tkb_button_rect(const tkb_geometry_t *g, tkb_target_t target, int *x, int *y, int *w, int *h);
