#pragma once

// Mapping of the buttons of external controllers (USB and Bluetooth) to Retro-Go keys. All the controller drivers
// report buttons in the same generic order (see gamepad_protocols.h): 1 left face button, 2 bottom, 3 right, 4 top,
// 5 L, 6 R, 7 L2, 8 R2, 9 select, 10 start, 11 L3, 12 R3, 13 home, 14 share/capture. The d-pad isn't remappable.

#include <stdint.h>

#define PAD_MAP_BUTTONS 14

typedef struct
{
    uint32_t keys[PAD_MAP_BUTTONS]; // RG_KEY_* of each button, 0 for none
} pad_map_t;

extern pad_map_t pad_map; // The active mapping, used by usb_gamepad.c

enum
{
    PAD_LAYOUT_NINTENDO = 0, // By position: the right button is A, the bottom one B (the default)
    PAD_LAYOUT_XBOX = 1,     // A and B, X and Y swapped so the labels of an Xbox pad match
};

void pad_map_defaults(pad_map_t *map);
void pad_map_set_layout(pad_map_t *map, int layout);
uint32_t pad_map_translate(const pad_map_t *map, uint32_t buttons); // generic button mask to RG_KEY_* bits

// Selectable targets for a button: none, A, B, X, Y, L, R, Select, Start, Menu, Option
uint32_t pad_map_next_key(uint32_t key, int direction); // direction is 1 or -1, wraps around
int pad_map_key_is_valid(uint32_t key);
const char *pad_map_button_label(int button); // 0-based
