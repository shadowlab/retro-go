#pragma once

#include <stdint.h>

// USB host driver for HID gamepads, joysticks and boot-protocol keyboards.
// Reports are decoded into RG_KEY_* bits. All connected devices are merged.
void rg_usb_gamepad_init(void);
uint32_t rg_usb_gamepad_read(void);

// Turns a d-pad (HID_PAD_*) and the generic button mask of gamepad_protocols.h into RG_KEY_* bits
uint32_t rg_usb_gamepad_translate(uint8_t dpad, uint32_t buttons);
