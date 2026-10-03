#pragma once

// Minimal HID report descriptor parser for gamepads and joysticks. It has no dependencies on ESP-IDF
// so that it can be built and tested on a host. Only what is needed to turn an input report into a
// d-pad and button bitmask is extracted: buttons, X/Y axes and the hat switch.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HID_MAX_FIELDS 48

typedef enum
{
    HID_FIELD_BUTTON, // usage = button number (1-based)
    HID_FIELD_AXIS,   // usage = generic desktop usage (0x30 X, 0x31 Y, ...)
    HID_FIELD_HAT,
} hid_field_type_t;

typedef struct
{
    uint8_t type;
    uint8_t size;      // in bits
    uint16_t usage;
    uint32_t bit_offset; // relative to the first byte after the report ID
    int32_t min, max;
} hid_field_t;

typedef struct
{
    hid_field_t fields[HID_MAX_FIELDS];
    size_t count;
    uint8_t report_id; // 0 if the device doesn't use report IDs
    bool valid;        // a gamepad/joystick collection with at least a button or a d-pad was found
} hid_gamepad_desc_t;

// Generic desktop usages
#define HID_USAGE_X 0x30
#define HID_USAGE_Y 0x31

// Normalized state, bits match the "HID_PAD_*" constants below.
enum
{
    HID_PAD_UP = 1 << 0,
    HID_PAD_RIGHT = 1 << 1,
    HID_PAD_DOWN = 1 << 2,
    HID_PAD_LEFT = 1 << 3,
};

bool hid_parse_gamepad_descriptor(const uint8_t *desc, size_t len, hid_gamepad_desc_t *out);

// Decodes an input report (including its report ID byte if the device uses them).
// dpad receives HID_PAD_* bits, buttons receives bit N-1 for button N.
bool hid_decode_gamepad_report(const hid_gamepad_desc_t *desc, const uint8_t *report, size_t len,
                               uint8_t *dpad, uint32_t *buttons);
