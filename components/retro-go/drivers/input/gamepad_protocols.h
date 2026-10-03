#pragma once

// Input report decoders for the USB gamepads that don't use a standard HID report descriptor (Xbox 360 and Xbox One
// class controllers, the Switch Pro Controller). No ESP-IDF dependencies so that they can be tested on a host.
// The layouts are those documented by the Linux xpad and hid-nintendo drivers.
//
// All of them produce the same thing as the HID path, a d-pad mask (HID_PAD_*) and a button mask where bit N-1 is
// button N of the layout in usb_gamepad.c: 1 west, 2 south, 3 east, 4 north, 5 L, 6 R, 7 L2, 8 R2, 9 select,
// 10 start, 13 menu, 14 option. Buttons are placed by their position on the pad, not by their label.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hid_parser.h"

#define GP_BTN(n) (1u << ((n) - 1))

// Xbox 360 wired (vendor class 0xFF, subclass 0x5D, protocol 1) and compatible pads. 20 byte reports on the IN endpoint.
bool gp_decode_xbox360(const uint8_t *data, size_t len, uint8_t *dpad, uint32_t *buttons);

// Xbox One (vendor class 0xFF, subclass 0x47, protocol 0xD0), GIP input packets (command 0x20).
bool gp_decode_xboxone(const uint8_t *data, size_t len, uint8_t *dpad, uint32_t *buttons);
// Packets to send to the OUT endpoint once the interface is claimed. Returns the length of the packet.
size_t gp_xboxone_init_packet(uint16_t vid, uint16_t pid, int index, uint8_t out[8]);

// Switch Pro Controller (USB HID 057E:2009), full report mode (0x30), the reports start after the USB handshake.
#define GP_SWITCH_VID 0x057E
#define GP_SWITCH_PID_PRO 0x2009
bool gp_decode_switchpro(const uint8_t *data, size_t len, uint8_t *dpad, uint32_t *buttons);

// Output reports of the handshake: 0x80 <cmd>
#define GP_SWITCH_USB_HANDSHAKE 0x02
#define GP_SWITCH_USB_BAUD_3M 0x03
#define GP_SWITCH_USB_NO_TIMEOUT 0x04
// Output report selecting the full input report mode (subcommand 0x03 with data 0x30). Returns its length.
size_t gp_switch_report_mode_packet(uint8_t packet_number, uint8_t out[12]);
