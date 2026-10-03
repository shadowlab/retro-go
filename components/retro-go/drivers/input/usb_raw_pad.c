#include "rg_system.h"
#include "usb_raw_pad.h"

#if defined(RG_GAMEPAD_USB_HID)

#include <usb/usb_host.h>
#include <string.h>

#include "gamepad_protocols.h"

#define MAX_PADS 4
#define SWITCH_RETRY_US 500000
#define SWITCH_MAX_RETRIES 6

typedef enum
{
    PAD_XBOX360,
    PAD_XBOXONE,
    PAD_SWITCHPRO,
} pad_type_t;

typedef struct
{
    bool used;
    bool gone;
    pad_type_t type;
    uint16_t vid, pid;
    usb_device_handle_t dev;
    uint8_t intf;
    uint8_t ep_in, ep_out;
    usb_transfer_t *xfer_in, *xfer_out;
    int pending; // transfers in flight
    // Switch Pro handshake, steps 0-2 wait for the controller's reply: 0 handshake, 1 baud rate, 2 handshake.
    // 3 sends "no timeout", 4 the report mode request, 5 is done.
    int step;
    bool out_busy;
    int retries;
    int64_t step_deadline;
    uint8_t packet_number;
    // Xbox One init packets
    int init_index;
    volatile uint8_t dpad;
    volatile uint32_t buttons;
} pad_t;

// USB commands of the Switch Pro handshake (output report 0x80 <command>), the controller echoes them as 0x81 <command>
static const uint8_t switch_usb_cmds[] = {GP_SWITCH_USB_HANDSHAKE, GP_SWITCH_USB_BAUD_3M, GP_SWITCH_USB_HANDSHAKE,
                                          GP_SWITCH_USB_NO_TIMEOUT};

static pad_t pads[MAX_PADS];
static usb_host_client_handle_t client;
static bool started = false;

int rg_usb_raw_pad_wants_device(uint16_t vid, uint16_t pid)
{
    return vid == GP_SWITCH_VID && pid == GP_SWITCH_PID_PRO;
}

void rg_usb_raw_pad_read(uint8_t *dpad, uint32_t *buttons)
{
    uint8_t d = 0;
    uint32_t b = 0;
    for (int i = 0; i < MAX_PADS; ++i)
    {
        if (pads[i].used && !pads[i].gone)
            d |= pads[i].dpad, b |= pads[i].buttons;
    }
    *dpad = d;
    *buttons = b;
}

static void send_out(pad_t *pad, const uint8_t *data, size_t len);

static void switch_send_step(pad_t *pad)
{
    uint8_t buf[12];

    if (pad->step < 4)
    {
        buf[0] = 0x80;
        buf[1] = switch_usb_cmds[pad->step];
        send_out(pad, buf, 2);
        if (pad->step == 3)
            pad->step = 4; // No reply to this one, the report mode request follows when it has been sent
        else
            pad->step_deadline = rg_system_timer() + SWITCH_RETRY_US;
    }
    else if (pad->step == 4)
    {
        size_t n = gp_switch_report_mode_packet(pad->packet_number++, buf);
        send_out(pad, buf, n);
        pad->step = 5;
        pad->step_deadline = 0;
    }
}

static void out_done_cb(usb_transfer_t *xfer)
{
    pad_t *pad = xfer->context;
    pad->pending--;
    pad->out_busy = false;

    if (pad->gone)
        return;
    if (pad->type == PAD_SWITCHPRO && pad->step == 4)
    {
        switch_send_step(pad);
    }
    else if (pad->type == PAD_XBOXONE && pad->init_index > 0 && pad->init_index < 2)
    {
        uint8_t buf[8];
        size_t n = gp_xboxone_init_packet(pad->vid, pad->pid, pad->init_index++, buf);
        if (n)
            send_out(pad, buf, n);
    }
}

static void send_out(pad_t *pad, const uint8_t *data, size_t len)
{
    if (!pad->ep_out || pad->gone || pad->out_busy)
        return;
    memcpy(pad->xfer_out->data_buffer, data, len);
    pad->xfer_out->num_bytes = len;
    pad->xfer_out->device_handle = pad->dev;
    pad->xfer_out->bEndpointAddress = pad->ep_out;
    pad->xfer_out->callback = out_done_cb;
    pad->xfer_out->context = pad;
    if (usb_host_transfer_submit(pad->xfer_out) == ESP_OK)
    {
        pad->pending++;
        pad->out_busy = true;
    }
}

static void in_done_cb(usb_transfer_t *xfer)
{
    pad_t *pad = xfer->context;
    pad->pending--;

    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED && xfer->actual_num_bytes > 0 && !pad->gone)
    {
        const uint8_t *d = xfer->data_buffer;
        const size_t len = xfer->actual_num_bytes;
        uint8_t dpad;
        uint32_t buttons;

        switch (pad->type)
        {
        case PAD_XBOX360:
            if (gp_decode_xbox360(d, len, &dpad, &buttons))
                pad->dpad = dpad, pad->buttons = buttons;
            break;
        case PAD_XBOXONE:
            if (gp_decode_xboxone(d, len, &dpad, &buttons))
                pad->dpad = dpad, pad->buttons = buttons;
            break;
        case PAD_SWITCHPRO:
            if (gp_decode_switchpro(d, len, &dpad, &buttons))
            {
                pad->dpad = dpad, pad->buttons = buttons;
                pad->step = 5; // Streaming, the handshake is over
                pad->step_deadline = 0;
            }
            else if (len >= 2 && d[0] == 0x81 && pad->step < 3 && d[1] == switch_usb_cmds[pad->step])
            {
                // The controller answers each USB command with 0x81 <command>, go to the next one
                pad->step++;
                pad->retries = 0;
                switch_send_step(pad);
            }
            break;
        }
    }

    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED && !pad->gone)
    {
        xfer->num_bytes = xfer->data_buffer_size;
        if (usb_host_transfer_submit(xfer) == ESP_OK)
            pad->pending++;
    }
    else if (xfer->status != USB_TRANSFER_STATUS_COMPLETED && xfer->status != USB_TRANSFER_STATUS_NO_DEVICE
             && xfer->status != USB_TRANSFER_STATUS_CANCELED)
    {
        // Stalls and the like: try again, the device may recover
        if (!pad->gone && usb_host_transfer_submit(xfer) == ESP_OK)
            pad->pending++;
    }
}

static void free_pad(pad_t *pad)
{
    if (pad->xfer_in)
        usb_host_transfer_free(pad->xfer_in);
    if (pad->xfer_out)
        usb_host_transfer_free(pad->xfer_out);
    usb_host_interface_release(client, pad->dev, pad->intf);
    usb_host_device_close(client, pad->dev);
    memset(pad, 0, sizeof(*pad));
}

static void new_device(uint8_t address)
{
    usb_device_handle_t dev;
    if (usb_host_device_open(client, address, &dev) != ESP_OK)
        return;

    const usb_device_desc_t *dd;
    const usb_config_desc_t *cfg;
    if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK || usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK)
    {
        usb_host_device_close(client, dev);
        return;
    }

    // Look for an interface we know how to drive
    const uint8_t *p = (const uint8_t *)cfg;
    const uint8_t *end = p + cfg->wTotalLength;
    const usb_intf_desc_t *found = NULL;
    pad_type_t type = PAD_XBOX360;
    for (const uint8_t *q = p; q + 2 <= end && q[0] >= 2; q += q[0])
    {
        if (q[1] != USB_B_DESCRIPTOR_TYPE_INTERFACE)
            continue;
        const usb_intf_desc_t *intf = (const usb_intf_desc_t *)q;
        if (intf->bAlternateSetting != 0)
            continue;
        if (intf->bInterfaceClass == 0xFF && intf->bInterfaceSubClass == 0x5D && intf->bInterfaceProtocol == 0x01)
            found = intf, type = PAD_XBOX360;
        else if (intf->bInterfaceClass == 0xFF && intf->bInterfaceSubClass == 0x47 && intf->bInterfaceProtocol == 0xD0)
            found = intf, type = PAD_XBOXONE;
        else if (dd->idVendor == GP_SWITCH_VID && dd->idProduct == GP_SWITCH_PID_PRO
                 && intf->bInterfaceClass == USB_CLASS_HID)
            found = intf, type = PAD_SWITCHPRO;
        if (found)
            break;
    }
    if (!found)
    {
        usb_host_device_close(client, dev);
        return;
    }

    pad_t *pad = NULL;
    for (int i = 0; i < MAX_PADS && !pad; ++i)
        pad = pads[i].used ? NULL : &pads[i];
    if (!pad)
    {
        RG_LOGW("Too many USB gamepads, ignoring");
        usb_host_device_close(client, dev);
        return;
    }
    memset(pad, 0, sizeof(*pad));
    pad->type = type;
    pad->vid = dd->idVendor;
    pad->pid = dd->idProduct;
    pad->dev = dev;
    pad->intf = found->bInterfaceNumber;

    // Interrupt endpoints of the interface
    int in_mps = 0;
    int offset = 0;
    for (int i = 0; i < found->bNumEndpoints; ++i)
    {
        const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(found, i, cfg->wTotalLength, &offset);
        if (!ep || USB_EP_DESC_GET_XFERTYPE(ep) != USB_TRANSFER_TYPE_INTR)
            continue;
        if (USB_EP_DESC_GET_EP_DIR(ep) && !pad->ep_in)
            pad->ep_in = ep->bEndpointAddress, in_mps = USB_EP_DESC_GET_MPS(ep);
        else if (!USB_EP_DESC_GET_EP_DIR(ep) && !pad->ep_out)
            pad->ep_out = ep->bEndpointAddress;
    }

    if (!pad->ep_in || in_mps <= 0 || usb_host_interface_claim(client, dev, pad->intf, 0) != ESP_OK
        || usb_host_transfer_alloc(in_mps, 0, &pad->xfer_in) != ESP_OK
        || usb_host_transfer_alloc(64, 0, &pad->xfer_out) != ESP_OK)
    {
        RG_LOGW("USB gamepad %04X:%04X could not be set up", pad->vid, pad->pid);
        pad->used = true; // let free_pad() release what was taken
        pad->gone = true;
        return;
    }

    pad->used = true;
    RG_LOGI("USB gamepad %04X:%04X (%s)", pad->vid, pad->pid,
            type == PAD_XBOX360 ? "Xbox 360" : type == PAD_XBOXONE ? "Xbox One" : "Switch Pro");

    pad->xfer_in->device_handle = dev;
    pad->xfer_in->bEndpointAddress = pad->ep_in;
    pad->xfer_in->num_bytes = pad->xfer_in->data_buffer_size;
    pad->xfer_in->callback = in_done_cb;
    pad->xfer_in->context = pad;
    if (usb_host_transfer_submit(pad->xfer_in) == ESP_OK)
        pad->pending++;

    if (type == PAD_XBOXONE)
    {
        // Packets are sent one after the other, the next one when this one has been sent (out_done_cb)
        uint8_t buf[8];
        pad->init_index = 1;
        send_out(pad, buf, gp_xboxone_init_packet(pad->vid, pad->pid, 0, buf));
    }
    else if (type == PAD_SWITCHPRO)
    {
        switch_send_step(pad);
    }
}

static void client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
    {
        new_device(msg->new_dev.address);
    }
    else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE)
    {
        for (int i = 0; i < MAX_PADS; ++i)
        {
            if (pads[i].used && pads[i].dev == msg->dev_gone.dev_hdl)
            {
                RG_LOGI("USB gamepad %04X:%04X removed", pads[i].vid, pads[i].pid);
                pads[i].gone = true;
                pads[i].dpad = 0;
                pads[i].buttons = 0;
            }
        }
    }
}

static void raw_pad_task(void *arg)
{
    while (1)
    {
        usb_host_client_handle_events(client, pdMS_TO_TICKS(100));

        for (int i = 0; i < MAX_PADS; ++i)
        {
            pad_t *pad = &pads[i];
            if (!pad->used)
                continue;
            if (pad->gone)
            {
                // Wait until the stack has returned every transfer before freeing them
                if (pad->pending <= 0)
                    free_pad(pad);
                continue;
            }
            // The Switch Pro doesn't always answer the first commands, repeat the current one
            if (pad->type == PAD_SWITCHPRO && pad->step < 3 && pad->step_deadline
                && rg_system_timer() > pad->step_deadline && pad->retries++ < SWITCH_MAX_RETRIES)
            {
                switch_send_step(pad);
            }
        }
    }
}

void rg_usb_raw_pad_init(void)
{
    if (started)
        return;

    const usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {.client_event_callback = client_event_cb, .callback_arg = NULL},
    };
    if (usb_host_client_register(&cfg, &client) != ESP_OK)
    {
        RG_LOGE("USB gamepad client registration failed");
        return;
    }
    // Same core as the other USB tasks, see usb_gamepad.c
    rg_task_create("rg_usb_pad", &raw_pad_task, NULL, 4 * 1024, RG_TASK_PRIORITY_5, 1);
    started = true;
}

#else
void rg_usb_raw_pad_init(void) {}
void rg_usb_raw_pad_read(uint8_t *dpad, uint32_t *buttons)
{
    *dpad = 0;
    *buttons = 0;
}
int rg_usb_raw_pad_wants_device(uint16_t vid, uint16_t pid)
{
    return 0;
}
#endif
