#pragma once

#include <stdint.h>

// USB host driver for HID gamepads, joysticks and boot-protocol keyboards.
// Reports are decoded into RG_KEY_* bits. All connected devices are merged.
void rg_usb_gamepad_init(void);
uint32_t rg_usb_gamepad_read(void);
