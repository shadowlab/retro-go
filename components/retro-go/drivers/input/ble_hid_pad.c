#include "rg_system.h"
#include "ble_hid_pad.h"

#if defined(RG_GAMEPAD_BLE_HID)

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <store/config/ble_store_config.h>
#include <nvs_flash.h>
#include <esp_hidh.h>
#include <esp_hosted_bt_host_stack.h>
#include <string.h>

#include "hid_parser.h"
#include "usb_gamepad.h"

#define MAX_PADS 2
#define PAIRING_TIME_US (60 * 1000000LL)
#define CONNECT_TIMEOUT_US (15 * 1000000LL)
#define BLE_APPEARANCE_JOYSTICK 0x03C3
#define BLE_APPEARANCE_GAMEPAD 0x03C4
#define BLE_UUID_HID_SERVICE 0x1812

typedef struct
{
    esp_hidh_dev_t *dev;
    hid_gamepad_desc_t desc;
    volatile uint8_t dpad;
    volatile uint32_t buttons;
} ble_pad_t;

static const char *SETTING_BT_PADS = "BtPads";

static ble_pad_t pads[MAX_PADS];
static volatile rg_ble_pad_mode_t mode = RG_BLE_PAD_OFF;
static volatile int64_t pairing_until = 0;
static volatile bool synced = false, scanning = false;
static volatile int64_t connecting_since = 0;
static uint8_t own_addr_type;
static const char *status = "Off";
static bool task_started = false;

// Persistent bonding keys storage of the NimBLE host (NVS), not declared in a public header
void ble_store_config_init(void);

static int gap_event(struct ble_gap_event *event, void *arg);

static bool pairing_active(void)
{
    return mode == RG_BLE_PAD_PAIRING && rg_system_timer() < pairing_until;
}

static ble_pad_t *find_pad(esp_hidh_dev_t *dev)
{
    for (int i = 0; i < MAX_PADS; ++i)
    {
        if (pads[i].dev == dev)
            return &pads[i];
    }
    return NULL;
}

static int free_slots(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PADS; ++i)
        n += pads[i].dev == NULL;
    return n;
}

static bool is_bonded(const ble_addr_t *addr)
{
    ble_addr_t peers[8];
    int count = 0;
    if (ble_store_util_bonded_peers(peers, &count, 8) != 0)
        return false;
    for (int i = 0; i < count; ++i)
    {
        if (peers[i].type == addr->type && memcmp(peers[i].val, addr->val, 6) == 0)
            return true;
    }
    return false;
}

static bool looks_like_gamepad(const struct ble_hs_adv_fields *f)
{
    if (f->appearance_is_present && (f->appearance == BLE_APPEARANCE_GAMEPAD || f->appearance == BLE_APPEARANCE_JOYSTICK))
        return true;
    for (int i = 0; i < f->num_uuids16; ++i)
    {
        if (f->uuids16[i].value == BLE_UUID_HID_SERVICE)
            return true;
    }
    return false;
}

static void start_scan(void)
{
    if (!synced || scanning || mode == RG_BLE_PAD_OFF || free_slots() == 0 || connecting_since)
        return;

    struct ble_gap_disc_params params = {
        .itvl = 0, // defaults
        .window = 0,
        .filter_policy = 0,
        .limited = 0,
        .passive = 1,
        .filter_duplicates = 1,
    };
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc == 0)
        scanning = true;
    else
        RG_LOGW("BLE scan failed: %d", rc);
}

static void stop_scan(void)
{
    if (scanning)
    {
        ble_gap_disc_cancel();
        scanning = false;
    }
}

static void on_disc(const struct ble_gap_disc_desc *d)
{
    if (connecting_since || free_slots() == 0)
        return;

    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, d->data, d->length_data) != 0)
        return;

    // Controllers that were paired before reconnect when they are switched on. New ones only while pairing.
    const bool bonded = is_bonded(&d->addr);
    if (!(bonded || (pairing_active() && looks_like_gamepad(&fields))))
        return;
    if (!looks_like_gamepad(&fields) && !bonded)
        return;

    RG_LOGI("BLE controller found (%02X:%02X:%02X:%02X:%02X:%02X, %s)", d->addr.val[5], d->addr.val[4], d->addr.val[3],
            d->addr.val[2], d->addr.val[1], d->addr.val[0], bonded ? "paired" : "new");

    stop_scan();
    connecting_since = rg_system_timer();
    uint8_t bda[6];
    memcpy(bda, d->addr.val, 6);
    if (!esp_hidh_dev_open(bda, ESP_HID_TRANSPORT_BLE, d->addr.type))
        connecting_since = 0; // The supervisor starts scanning again
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        on_disc(&event->disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        scanning = false;
        break;
    default:
        break;
    }
    return 0;
}

static void hidh_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidh_event_data_t *p = event_data;

    switch ((esp_hidh_event_t)id)
    {
    case ESP_HIDH_OPEN_EVENT: {
        connecting_since = 0;
        if (p->open.status != ESP_OK)
        {
            RG_LOGW("BLE controller connection failed");
            break;
        }
        ble_pad_t *pad = find_pad(NULL);
        const esp_hid_device_config_t *cfg = esp_hidh_dev_config_get(p->open.dev);
        if (!pad || !cfg || cfg->report_maps_len < 1
            || !hid_parse_gamepad_descriptor(cfg->report_maps[0].data, cfg->report_maps[0].len, &pad->desc))
        {
            RG_LOGI("BLE HID device is not a game controller, disconnecting");
            esp_hidh_dev_close(p->open.dev);
            break;
        }
        pad->dpad = 0;
        pad->buttons = 0;
        pad->dev = p->open.dev;
        RG_LOGI("BLE controller connected: %s", esp_hidh_dev_name_get(p->open.dev) ?: "?");
        break;
    }
    case ESP_HIDH_INPUT_EVENT: {
        ble_pad_t *pad = find_pad(p->input.dev);
        uint8_t buf[64];
        if (!pad || p->input.length + 1 > sizeof(buf))
            break;
        size_t len = 0;
        // The decoder expects the report ID in front of the data if the device uses them
        if (pad->desc.report_id != 0)
        {
            if (p->input.report_id != pad->desc.report_id)
                break;
            buf[len++] = pad->desc.report_id;
        }
        memcpy(buf + len, p->input.data, p->input.length);
        len += p->input.length;

        uint8_t dpad;
        uint32_t buttons;
        if (hid_decode_gamepad_report(&pad->desc, buf, len, &dpad, &buttons))
            pad->dpad = dpad, pad->buttons = buttons;
        break;
    }
    case ESP_HIDH_CLOSE_EVENT: {
        connecting_since = 0;
        ble_pad_t *pad = find_pad(p->close.dev);
        if (pad)
        {
            RG_LOGI("BLE controller disconnected");
            pad->dpad = 0;
            pad->buttons = 0;
            pad->dev = NULL;
        }
        esp_hidh_dev_free(p->close.dev);
        break;
    }
    default:
        break;
    }
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &own_addr_type) != 0)
    {
        RG_LOGE("BLE address setup failed");
        return;
    }
    synced = true;
}

static void on_reset(int reason)
{
    RG_LOGW("BLE host reset (%d)", reason);
    synced = false;
    scanning = false;
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool bring_up(void)
{
#ifdef RG_TARGET_NETWORK_PREPARE
    // Powers the co-processor and connects to its ESP-Hosted firmware (shared with Wi-Fi)
    if (!RG_TARGET_NETWORK_PREPARE())
    {
        status = "No radio";
        return false;
    }
#endif
    // Bonding keys are stored in NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_hosted_bt_host_stack_cfg_t bt = ESP_HOSTED_BT_HOST_STACK_CONFIG_DEFAULT();
    if (esp_hosted_bt_host_stack_setup(&bt) != ESP_OK)
    {
        RG_LOGE("Bluetooth controller setup failed");
        status = "No radio";
        return false;
    }
    if (nimble_port_init() != ESP_OK)
    {
        status = "Failed";
        return false;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // Game controllers pair with "just works" and are bonded so that they reconnect
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();

    const esp_hidh_config_t hidh_cfg = {.callback = hidh_event, .event_stack_size = 4096, .callback_arg = NULL};
    if (esp_hidh_init(&hidh_cfg) != ESP_OK)
    {
        status = "Failed";
        return false;
    }
    nimble_port_freertos_init(host_task);
    return true;
}

static void ble_pad_task(void *arg)
{
    status = "Starting";
    if (!bring_up())
        vTaskDelete(NULL);

    while (1)
    {
        const int64_t now = rg_system_timer();

        if (mode == RG_BLE_PAD_OFF)
        {
            stop_scan();
            for (int i = 0; i < MAX_PADS; ++i)
            {
                if (pads[i].dev)
                    esp_hidh_dev_close(pads[i].dev);
            }
            status = "Off";
        }
        else
        {
            if (connecting_since && now - connecting_since > CONNECT_TIMEOUT_US)
                connecting_since = 0; // gave up
            if (mode == RG_BLE_PAD_PAIRING && now >= pairing_until)
                mode = RG_BLE_PAD_ON;

            start_scan();
            status = free_slots() < MAX_PADS ? "Connected" : pairing_active() ? "Pairing" : "Searching";
            if (!synced)
                status = "Starting";
        }
        rg_task_delay(1000);
    }
}

static void ensure_task(void)
{
    if (task_started)
        return;
    task_started = true;
    // On core 1 with the USB, display, input and audio tasks
    rg_task_create("rg_ble_pad", &ble_pad_task, NULL, 6 * 1024, RG_TASK_PRIORITY_3, 1);
}

void rg_ble_pad_init(void)
{
    mode = rg_settings_get_number(NS_GLOBAL, SETTING_BT_PADS, 0) ? RG_BLE_PAD_ON : RG_BLE_PAD_OFF;
    if (mode != RG_BLE_PAD_OFF)
        ensure_task();
}

void rg_ble_pad_set_mode(rg_ble_pad_mode_t new_mode)
{
    mode = new_mode;
    pairing_until = new_mode == RG_BLE_PAD_PAIRING ? rg_system_timer() + PAIRING_TIME_US : 0;
    rg_settings_set_number(NS_GLOBAL, SETTING_BT_PADS, new_mode != RG_BLE_PAD_OFF);
    rg_settings_commit();
    if (new_mode != RG_BLE_PAD_OFF)
        ensure_task();
}

rg_ble_pad_mode_t rg_ble_pad_get_mode(void)
{
    return mode;
}

const char *rg_ble_pad_status(void)
{
    return status;
}

uint32_t rg_ble_pad_read(void)
{
    uint32_t keys = 0;
    for (int i = 0; i < MAX_PADS; ++i)
    {
        if (pads[i].dev)
            keys |= rg_usb_gamepad_translate(pads[i].dpad, pads[i].buttons);
    }
    return keys;
}

#else
void rg_ble_pad_init(void) {}
void rg_ble_pad_set_mode(rg_ble_pad_mode_t new_mode) {}
rg_ble_pad_mode_t rg_ble_pad_get_mode(void) { return RG_BLE_PAD_OFF; }
const char *rg_ble_pad_status(void) { return "Off"; }
uint32_t rg_ble_pad_read(void) { return 0; }
#endif
