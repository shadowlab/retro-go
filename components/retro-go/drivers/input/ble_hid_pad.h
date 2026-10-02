#pragma once

#include <stdint.h>

// Bluetooth LE game controllers (HID over GATT) through the Wi-Fi/Bluetooth co-processor. The co-processor of the
// M5Stack Tab5 is an ESP32-C6, which only does Bluetooth LE: controllers that use Bluetooth Classic (DualShock 4,
// DualSense, Switch Pro, Wii) can't be used this way, those need USB.
//
// Opt-in, the radio and the host stack are only started when the user has enabled it, because bringing up the
// co-processor takes a few seconds.

typedef enum
{
    RG_BLE_PAD_OFF = 0,
    RG_BLE_PAD_ON = 1,      // Reconnect to controllers that have been paired before
    RG_BLE_PAD_PAIRING = 2, // Also accept new controllers for a minute
} rg_ble_pad_mode_t;

void rg_ble_pad_init(void); // Starts the radio if the setting says so
void rg_ble_pad_set_mode(rg_ble_pad_mode_t mode);
rg_ble_pad_mode_t rg_ble_pad_get_mode(void);
const char *rg_ble_pad_status(void);
uint32_t rg_ble_pad_read(void); // RG_KEY_* bits of all connected controllers
