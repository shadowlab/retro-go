#ifndef PAD_MAP_STANDALONE
#include "rg_system.h" // The target configuration (RG_GAMEPAD_USB_HID)
#endif
#include "pad_map.h"
#include "rg_input.h"

// Only built for targets with external controllers, define PAD_MAP_STANDALONE to build it on a host
#if defined(RG_GAMEPAD_USB_HID) || defined(PAD_MAP_STANDALONE)

pad_map_t pad_map = {{
    RG_KEY_Y,      // 1  left face button
    RG_KEY_B,      // 2  bottom
    RG_KEY_A,      // 3  right
    RG_KEY_X,      // 4  top
    RG_KEY_L,      // 5  L1
    RG_KEY_R,      // 6  R1
    RG_KEY_L,      // 7  L2 / ZL
    RG_KEY_R,      // 8  R2 / ZR
    RG_KEY_SELECT, // 9  Share / Minus / Select
    RG_KEY_START,  // 10 Options / Plus / Start
    0,             // 11 L3
    0,             // 12 R3
    RG_KEY_MENU,   // 13 PS / Home
    RG_KEY_OPTION, // 14 Touchpad / Capture
}};

static const uint32_t targets[] = {
    0, RG_KEY_A, RG_KEY_B, RG_KEY_X, RG_KEY_Y, RG_KEY_L, RG_KEY_R, RG_KEY_SELECT, RG_KEY_START, RG_KEY_MENU, RG_KEY_OPTION,
};
#define NUM_TARGETS (int)(sizeof(targets) / sizeof(targets[0]))

void pad_map_defaults(pad_map_t *map)
{
    static const uint32_t defaults[PAD_MAP_BUTTONS] = {
        RG_KEY_Y, RG_KEY_B, RG_KEY_A, RG_KEY_X, RG_KEY_L, RG_KEY_R, RG_KEY_L, RG_KEY_R,
        RG_KEY_SELECT, RG_KEY_START, 0, 0, RG_KEY_MENU, RG_KEY_OPTION,
    };
    for (int i = 0; i < PAD_MAP_BUTTONS; ++i)
        map->keys[i] = defaults[i];
}

void pad_map_set_layout(pad_map_t *map, int layout)
{
    if (layout == PAD_LAYOUT_XBOX)
    {
        // Same positions, Xbox labels: the bottom button is A, the right one B, the left one X, the top one Y
        map->keys[0] = RG_KEY_X;
        map->keys[1] = RG_KEY_A;
        map->keys[2] = RG_KEY_B;
        map->keys[3] = RG_KEY_Y;
    }
    else
    {
        map->keys[0] = RG_KEY_Y;
        map->keys[1] = RG_KEY_B;
        map->keys[2] = RG_KEY_A;
        map->keys[3] = RG_KEY_X;
    }
}

uint32_t pad_map_translate(const pad_map_t *map, uint32_t buttons)
{
    uint32_t keys = 0;
    for (int i = 0; i < PAD_MAP_BUTTONS; ++i)
    {
        if (buttons & (1u << i))
            keys |= map->keys[i];
    }
    return keys;
}

int pad_map_key_is_valid(uint32_t key)
{
    for (int i = 0; i < NUM_TARGETS; ++i)
    {
        if (targets[i] == key)
            return 1;
    }
    return 0;
}

uint32_t pad_map_next_key(uint32_t key, int direction)
{
    int index = 0;
    for (int i = 0; i < NUM_TARGETS; ++i)
    {
        if (targets[i] == key)
            index = i;
    }
    index = (index + (direction < 0 ? NUM_TARGETS - 1 : 1)) % NUM_TARGETS;
    return targets[index];
}

const char *pad_map_button_label(int button)
{
    static const char *labels[PAD_MAP_BUTTONS] = {
        "Left face button", "Bottom face button", "Right face button", "Top face button", "L1 / LB", "R1 / RB",
        "L2 / LT", "R2 / RT", "Select / Back", "Start / Menu", "Left stick press", "Right stick press", "Home / Guide",
        "Share / Capture",
    };
    return (button >= 0 && button < PAD_MAP_BUTTONS) ? labels[button] : "";
}

#endif
