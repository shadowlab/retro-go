#include "rg_system.h"
#include "rg_input.h"
#include "usb_gamepad.h"

#if defined(RG_GAMEPAD_USB_HID)

#include <usb/usb_host.h>
#include <usb/hid_host.h>
#include <string.h>
#include <stdlib.h>

#include "hid_parser.h"
#include "usb_raw_pad.h"
#if defined(RG_TARGET_M5STACK_TAB5)
#include "drivers/board/m5stack_tab5.h"
#endif

#define MAX_DEVICES 4

typedef struct
{
    hid_host_device_handle_t handle;
    bool keyboard;
    hid_gamepad_desc_t desc;
    volatile uint32_t keys;
} usb_pad_t;

static usb_pad_t pads[MAX_DEVICES];
static bool started = false;

// Button N of a HID gamepad (1-based). Most pads (DualShock/DualSense, Switch Pro, DInput pads from
// 8BitDo, Logitech, Retrolink...) list west, south, east, north first, then the shoulders.
static const uint32_t button_map[] = {
    RG_KEY_Y,      // 1  west
    RG_KEY_B,      // 2  south
    RG_KEY_A,      // 3  east
    RG_KEY_X,      // 4  north
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
};

uint32_t rg_usb_gamepad_translate(uint8_t dpad, uint32_t buttons)
{
    uint32_t keys = 0;
    if (dpad & HID_PAD_UP) keys |= RG_KEY_UP;
    if (dpad & HID_PAD_DOWN) keys |= RG_KEY_DOWN;
    if (dpad & HID_PAD_LEFT) keys |= RG_KEY_LEFT;
    if (dpad & HID_PAD_RIGHT) keys |= RG_KEY_RIGHT;
    for (size_t i = 0; i < RG_COUNT(button_map); ++i)
    {
        if (buttons & (1u << i))
            keys |= button_map[i];
    }
    return keys;
}

static uint32_t translate_keyboard(const uint8_t *r, size_t len)
{
    if (len < 8)
        return 0;
    uint32_t keys = 0;
    for (int i = 2; i < 8; ++i)
    {
        switch (r[i])
        {
        case 0x52: keys |= RG_KEY_UP; break;
        case 0x51: keys |= RG_KEY_DOWN; break;
        case 0x50: keys |= RG_KEY_LEFT; break;
        case 0x4F: keys |= RG_KEY_RIGHT; break;
        case 0x1B: keys |= RG_KEY_A; break;      // X
        case 0x1D: keys |= RG_KEY_B; break;      // Z
        case 0x16: keys |= RG_KEY_X; break;      // S
        case 0x04: keys |= RG_KEY_Y; break;      // A
        case 0x14: keys |= RG_KEY_L; break;      // Q
        case 0x1A: keys |= RG_KEY_R; break;      // W
        case 0x28: keys |= RG_KEY_START; break;  // Enter
        case 0x2A: keys |= RG_KEY_SELECT; break; // Backspace
        case 0x29: keys |= RG_KEY_MENU; break;   // Esc
        case 0x2B: keys |= RG_KEY_OPTION; break; // Tab
        }
    }
    return keys;
}

static void release_pad(usb_pad_t *pad)
{
    pad->keys = 0;
    pad->handle = NULL;
}

static void interface_event_cb(hid_host_device_handle_t handle, const hid_host_interface_event_t event, void *arg)
{
    usb_pad_t *pad = arg;

    switch (event)
    {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        uint8_t data[64];
        size_t len = 0;
        if (hid_host_device_get_raw_input_report_data(handle, data, sizeof(data), &len) != ESP_OK)
            break;
        if (pad->keyboard)
        {
            pad->keys = translate_keyboard(data, len);
        }
        else
        {
            uint8_t dpad;
            uint32_t buttons;
            if (hid_decode_gamepad_report(&pad->desc, data, len, &dpad, &buttons))
                pad->keys = rg_usb_gamepad_translate(dpad, buttons);
        }
        break;
    }
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        RG_LOGI("USB HID device disconnected");
        hid_host_device_close(handle);
        release_pad(pad);
        break;
    default:
        RG_LOGW("USB HID transfer error");
        break;
    }
}

static void driver_event_cb(hid_host_device_handle_t handle, const hid_host_driver_event_t event, void *arg)
{
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED)
        return;

    hid_host_dev_params_t params;
    hid_host_dev_info_t info = {0};
    if (hid_host_device_get_params(handle, &params) != ESP_OK)
        return;
    hid_host_get_device_info(handle, &info);

    // Mice are of no use to us
    if (params.sub_class == 1 && params.proto == 2)
        return;

    // The Switch Pro Controller needs a handshake first, usb_raw_pad.c drives it
    if (rg_usb_raw_pad_wants_device(info.VID, info.PID))
        return;

    usb_pad_t *pad = NULL;
    for (int i = 0; i < MAX_DEVICES && !pad; ++i)
        pad = pads[i].handle ? NULL : &pads[i];
    if (!pad)
    {
        RG_LOGW("Too many USB HID devices, ignoring");
        return;
    }

    memset(pad, 0, sizeof(*pad));
    pad->keyboard = params.sub_class == 1 && params.proto == 1;

    const hid_host_device_config_t cfg = {.callback = interface_event_cb, .callback_arg = pad};
    if (hid_host_device_open(handle, &cfg) != ESP_OK)
        return;

    if (pad->keyboard)
    {
        hid_class_request_set_protocol(handle, HID_REPORT_PROTOCOL_BOOT);
        hid_class_request_set_idle(handle, 0, 0);
    }
    else
    {
        size_t desc_len = 0;
        uint8_t *desc = hid_host_get_report_descriptor(handle, &desc_len);
        if (!desc || !hid_parse_gamepad_descriptor(desc, desc_len, &pad->desc))
        {
            RG_LOGI("USB HID device %04X:%04X is not a gamepad, ignoring", info.VID, info.PID);
            hid_host_device_close(handle);
            return;
        }
        RG_LOGI("USB gamepad %04X:%04X: %d fields, report id %d", info.VID, info.PID, (int)pad->desc.count,
                pad->desc.report_id);
    }

    pad->handle = handle;
    if (hid_host_device_start(handle) != ESP_OK)
    {
        hid_host_device_close(handle);
        release_pad(pad);
    }
}

static void usb_lib_task(void *arg)
{
    while (1)
    {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
            usb_host_device_free_all();
    }
}

void rg_usb_gamepad_init(void)
{
    if (started)
        return;

#if defined(RG_TARGET_M5STACK_TAB5)
    rg_tab5_set_usb_5v(true);
#endif

    const usb_host_config_t host_cfg = {.skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1};
    if (usb_host_install(&host_cfg) != ESP_OK)
    {
        RG_LOGE("USB host install failed");
        return;
    }
    // The USB tasks are light, keep them on core 1 with the display, input and audio tasks so that core 0 is left to the
    // emulator
    rg_task_create("rg_usb_host", &usb_lib_task, NULL, 4 * 1024, RG_TASK_PRIORITY_5, 1);

    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 1,
        .callback = driver_event_cb,
        .callback_arg = NULL,
    };
    if (hid_host_install(&hid_cfg) != ESP_OK)
    {
        RG_LOGE("USB HID host install failed");
        return;
    }
    rg_usb_raw_pad_init();
    started = true;
    RG_LOGI("USB HID gamepad host ready");
}

uint32_t rg_usb_gamepad_read(void)
{
    uint32_t keys = 0;
    for (int i = 0; i < MAX_DEVICES; ++i)
        keys |= pads[i].keys;

    uint8_t dpad;
    uint32_t buttons;
    rg_usb_raw_pad_read(&dpad, &buttons);
    return keys | rg_usb_gamepad_translate(dpad, buttons);
}

#else
uint32_t rg_usb_gamepad_translate(uint8_t dpad, uint32_t buttons) { return 0; }
void rg_usb_gamepad_init(void) {}
uint32_t rg_usb_gamepad_read(void) { return 0; }
#endif
