#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    RG_KEY_UP      = (1 << 0),
    RG_KEY_RIGHT   = (1 << 1),
    RG_KEY_DOWN    = (1 << 2),
    RG_KEY_LEFT    = (1 << 3),
    RG_KEY_SELECT  = (1 << 4),
    RG_KEY_START   = (1 << 5),
    RG_KEY_MENU    = (1 << 6),
    RG_KEY_OPTION  = (1 << 7),
    RG_KEY_A       = (1 << 8),
    RG_KEY_B       = (1 << 9),
    RG_KEY_X       = (1 << 10),
    RG_KEY_Y       = (1 << 11),
    RG_KEY_L       = (1 << 12),
    RG_KEY_R       = (1 << 13),
    RG_KEY_COUNT   = 14,
    RG_KEY_ANY     = 0xFFFF,
    RG_KEY_ALL     = 0xFFFF,
    RG_KEY_NONE    = 0,
} rg_key_t;

// #define RG_GAMEPAD_ADC_MAP {{}, ...} to use ADC driver
typedef struct
{
    rg_key_t key;
    int unit;   // adc_unit_t
    int channel;// adc_channel_t
    int atten;  // adc_atten_t
    int min, max;
} rg_keymap_adc_t;

// #define RG_GAMEPAD_GPIO_MAP {{}, ...} to use GPIO driver
typedef struct
{
    rg_key_t key;
    int num;      // gpio_num_t
    int pullup;   // Enable pullup (if supported by pin)
    int pulldown; // Enable pulldown (if supported by pin)
    int level;    // 0-1
} rg_keymap_gpio_t;

// #define RG_GAMEPAD_I2C_MAP {{}, ...} to use I2C driver
typedef struct
{
    rg_key_t key;
    int num;      // pin (or bit) number
    int pullup;   // Enable pullup (if supported by chip, currently MCP23017)
    int pulldown; // Enable pullup (if supported by chip, currently none)
    int level;    // 0-1
} rg_keymap_i2c_t;

// #define RG_GAMEPAD_KBD_MAP {{}, ...} for Keyboard driver
typedef struct
{
    rg_key_t key;
    uint32_t src;
} rg_keymap_kbd_t;

// #define RG_GAMEPAD_SERIAL_MAP {{}, ...} to use Serial (74164, SNES, etc) driver
typedef struct
{
    rg_key_t key;
    int num;    // pin (or bit) number
    int level;  // 0-1
} rg_keymap_serial_t;

// #define RG_GAMEPAD_VIRT_MAP {{}, ...} to add virtual buttons (eg start+select = menu)
typedef struct
{
    rg_key_t key;
    uint32_t src;
} rg_keymap_virt_t;

// FIXME: Create a single unified keymap...
// ...

typedef struct
{
    float level;
    float volts;
    bool present;
    bool charging;
} rg_battery_t;

void rg_input_init(void);
void rg_input_deinit(void);
// Touch gestures only drive the UI while a dialog is open (or in the launcher). Elsewhere touch is
// restricted to the menu hot corner so that stray touches don't press game buttons.
void rg_input_touch_ui_enter(void);
void rg_input_touch_ui_leave(void);
bool rg_input_touch_ui_active(void);
// Raw touch (targets with a touch screen): while enabled, touches are not turned into key presses and the UI reads the
// position with rg_input_read_touch() instead. For on-screen keyboards. Calls nest.
void rg_input_touch_raw_enter(void);
void rg_input_touch_raw_leave(void);
bool rg_input_touch_raw_active(void);
// True while a finger is down (or for a tap that has just ended), x/y in logical screen coordinates. Always false on
// targets without touch.
bool rg_input_read_touch(int *x, int *y);
// Button mapping of external controllers (targets with RG_GAMEPAD_USB_HID). Buttons are numbered from 0 in the order of
// rg_input_pad_button_label(), the key is 0 when a button does nothing. Changes are saved. Layout 0 = positional
// (Nintendo), 1 = Xbox labels.
int rg_input_pad_button_count(void); // 0 if the target has no external controllers
const char *rg_input_pad_button_label(int button);
rg_key_t rg_input_pad_get_key(int button);
void rg_input_pad_cycle_key(int button, int direction);
void rg_input_pad_set_layout(int layout);
void rg_input_pad_reset(void);

// Bluetooth LE controllers (targets with RG_GAMEPAD_BLE_HID): mode 0 = off, 1 = on, 2 = pairing
int rg_input_bt_get_mode(void);
void rg_input_bt_set_mode(int mode);
const char *rg_input_bt_status(void);
bool rg_input_key_is_present(rg_key_t mask);
bool rg_input_key_is_pressed(rg_key_t mask);
bool rg_input_wait_for_key(rg_key_t mask, bool pressed, int timeout_ms);
uint32_t rg_input_read_gamepad(void);
rg_battery_t rg_input_read_battery(void);
bool rg_input_read_gamepad_raw(uint32_t *out);
bool rg_input_read_battery_raw(rg_battery_t *out);
const char *rg_input_get_key_name(rg_key_t key);
