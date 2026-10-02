#include "gamepad_protocols.h"

#define STICK_X360 8000 // of +-32768

static int16_t s16(const uint8_t *p)
{
    return (int16_t)(p[0] | (p[1] << 8));
}

static uint8_t stick_to_dpad(int x, int y, int threshold)
{
    uint8_t pad = 0;
    pad |= x < -threshold ? HID_PAD_LEFT : x > threshold ? HID_PAD_RIGHT : 0;
    pad |= y > threshold ? HID_PAD_UP : y < -threshold ? HID_PAD_DOWN : 0; // up is positive on Xbox pads
    return pad;
}

bool gp_decode_xbox360(const uint8_t *d, size_t len, uint8_t *dpad, uint32_t *buttons)
{
    if (len < 14 || d[0] != 0x00)
        return false;

    uint8_t pad = 0;
    uint32_t btn = 0;
    pad |= (d[2] & 0x01) ? HID_PAD_UP : 0;
    pad |= (d[2] & 0x02) ? HID_PAD_DOWN : 0;
    pad |= (d[2] & 0x04) ? HID_PAD_LEFT : 0;
    pad |= (d[2] & 0x08) ? HID_PAD_RIGHT : 0;
    btn |= (d[2] & 0x10) ? GP_BTN(10) : 0; // Start
    btn |= (d[2] & 0x20) ? GP_BTN(9) : 0;  // Back
    btn |= (d[3] & 0x01) ? GP_BTN(5) : 0;  // LB
    btn |= (d[3] & 0x02) ? GP_BTN(6) : 0;  // RB
    btn |= (d[3] & 0x04) ? GP_BTN(13) : 0; // Guide
    btn |= (d[3] & 0x10) ? GP_BTN(2) : 0;  // A, south
    btn |= (d[3] & 0x20) ? GP_BTN(3) : 0;  // B, east
    btn |= (d[3] & 0x40) ? GP_BTN(1) : 0;  // X, west
    btn |= (d[3] & 0x80) ? GP_BTN(4) : 0;  // Y, north
    btn |= d[4] > 100 ? GP_BTN(7) : 0;     // Triggers, 0-255
    btn |= d[5] > 100 ? GP_BTN(8) : 0;
    pad |= stick_to_dpad(s16(d + 6), s16(d + 8), STICK_X360);

    *dpad = pad;
    *buttons = btn;
    return true;
}

bool gp_decode_xboxone(const uint8_t *d, size_t len, uint8_t *dpad, uint32_t *buttons)
{
    if (len < 14 || d[0] != 0x20) // GIP_CMD_INPUT
        return false;

    uint8_t pad = 0;
    uint32_t btn = 0;
    btn |= (d[4] & 0x04) ? GP_BTN(10) : 0; // Menu
    btn |= (d[4] & 0x08) ? GP_BTN(9) : 0;  // View
    btn |= (d[4] & 0x10) ? GP_BTN(2) : 0;  // A
    btn |= (d[4] & 0x20) ? GP_BTN(3) : 0;  // B
    btn |= (d[4] & 0x40) ? GP_BTN(1) : 0;  // X
    btn |= (d[4] & 0x80) ? GP_BTN(4) : 0;  // Y
    pad |= (d[5] & 0x01) ? HID_PAD_UP : 0;
    pad |= (d[5] & 0x02) ? HID_PAD_DOWN : 0;
    pad |= (d[5] & 0x04) ? HID_PAD_LEFT : 0;
    pad |= (d[5] & 0x08) ? HID_PAD_RIGHT : 0;
    btn |= (d[5] & 0x10) ? GP_BTN(5) : 0; // LB
    btn |= (d[5] & 0x20) ? GP_BTN(6) : 0; // RB
    btn |= (uint16_t)(d[6] | (d[7] << 8)) > 300 ? GP_BTN(7) : 0; // Triggers, 0-1023
    btn |= (uint16_t)(d[8] | (d[9] << 8)) > 300 ? GP_BTN(8) : 0;
    pad |= stick_to_dpad(s16(d + 10), s16(d + 12), STICK_X360);

    *dpad = pad;
    *buttons = btn;
    return true;
}

size_t gp_xboxone_init_packet(uint16_t vid, uint16_t pid, int index, uint8_t out[8])
{
    // GIP_CMD_POWER, GIP_OPT_INTERNAL, sequence 0, length, payload
    static const uint8_t power_on[] = {0x05, 0x20, 0x00, 0x01, 0x00};
    static const uint8_t s_init[] = {0x05, 0x20, 0x00, 0x0f, 0x06}; // Xbox One S and Series controllers
    const uint8_t *src = NULL;
    size_t n = 0;

    if (index == 0)
        src = power_on, n = sizeof(power_on);
    else if (index == 1 && vid == 0x045E && (pid == 0x02EA || pid == 0x0B00))
        src = s_init, n = sizeof(s_init);

    for (size_t i = 0; i < n; ++i)
        out[i] = src[i];
    return n;
}

bool gp_decode_switchpro(const uint8_t *d, size_t len, uint8_t *dpad, uint32_t *buttons)
{
    // 0x30: standard full report, 0x21: subcommand reply, both start with the same header
    if (len < 12 || (d[0] != 0x30 && d[0] != 0x21))
        return false;

    const uint32_t b = d[3] | (d[4] << 8) | (d[5] << 16);
    uint8_t pad = 0;
    uint32_t btn = 0;
    pad |= (b & (1u << 17)) ? HID_PAD_UP : 0;
    pad |= (b & (1u << 16)) ? HID_PAD_DOWN : 0;
    pad |= (b & (1u << 19)) ? HID_PAD_LEFT : 0;
    pad |= (b & (1u << 18)) ? HID_PAD_RIGHT : 0;
    btn |= (b & (1u << 0)) ? GP_BTN(1) : 0;  // Y, west
    btn |= (b & (1u << 2)) ? GP_BTN(2) : 0;  // B, south
    btn |= (b & (1u << 3)) ? GP_BTN(3) : 0;  // A, east
    btn |= (b & (1u << 1)) ? GP_BTN(4) : 0;  // X, north
    btn |= (b & (1u << 22)) ? GP_BTN(5) : 0; // L
    btn |= (b & (1u << 6)) ? GP_BTN(6) : 0;  // R
    btn |= (b & (1u << 23)) ? GP_BTN(7) : 0; // ZL
    btn |= (b & (1u << 7)) ? GP_BTN(8) : 0;  // ZR
    btn |= (b & (1u << 8)) ? GP_BTN(9) : 0;  // Minus
    btn |= (b & (1u << 9)) ? GP_BTN(10) : 0; // Plus
    btn |= (b & (1u << 12)) ? GP_BTN(13) : 0; // Home
    btn |= (b & (1u << 13)) ? GP_BTN(14) : 0; // Capture

    // Left stick, two 12 bit values packed in 3 bytes, centered around 2048 (the real calibration is in the
    // controller's flash, a fixed dead zone is good enough to drive a d-pad). Up is a larger Y value.
    const int x = d[6] | ((d[7] & 0x0F) << 8);
    const int y = (d[7] >> 4) | (d[8] << 4);
    pad |= stick_to_dpad(x - 2048, y - 2048, 700);

    *dpad = pad;
    *buttons = btn;
    return true;
}

size_t gp_switch_report_mode_packet(uint8_t packet_number, uint8_t out[12])
{
    // Output report 0x01: packet number, 8 bytes of (neutral) rumble data, subcommand 0x03 (set input report mode)
    static const uint8_t rumble_neutral[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    out[0] = 0x01;
    out[1] = packet_number & 0x0F;
    for (int i = 0; i < 8; ++i)
        out[2 + i] = rumble_neutral[i];
    out[10] = 0x03;
    out[11] = 0x30;
    return 12;
}
