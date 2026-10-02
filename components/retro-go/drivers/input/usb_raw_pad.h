#pragma once

#include <stdint.h>

// USB gamepads that need their own driver instead of the HID class driver: Xbox 360 and Xbox One class controllers
// (vendor specific interfaces) and the Switch Pro Controller (needs a handshake before it sends real reports).
// Call after usb_host_install(). The state is a d-pad (HID_PAD_*) and button mask as described in gamepad_protocols.h.
void rg_usb_raw_pad_init(void);
void rg_usb_raw_pad_read(uint8_t *dpad, uint32_t *buttons);

// True for devices that this driver takes over, so the HID driver leaves them alone
int rg_usb_raw_pad_wants_device(uint16_t vid, uint16_t pid);
