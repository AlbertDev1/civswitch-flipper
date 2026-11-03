#include "usb_composite.h"
#include "usb_descriptors.h"
#include <furi.h>
#include <usb.h>
#include <usb_hid.h>
#include <string.h>

#define PAD_IN  0x81
#define PAD_OUT 0x02
#define KBD_IN  0x83
#define KBD_OUT 0x03

static usbd_device* device;
static FuriSemaphore* pad_ready;
static FuriSemaphore* kbd_ready;
static volatile bool configured;
static volatile bool suspended;
static CivPadReport pad_report = {0, 8, 128, 128, 128, 128, 0};
static CivKeyboardReport kbd_report;
static uint8_t pad_out[64];
static uint8_t kbd_led;
static uint8_t pad_idle, kbd_idle, kbd_protocol = 1;

static struct {
    uint8_t length, type;
    uint16_t chars[16];
} __attribute__((packed)) manuf = {
    34,
    USB_DTYPE_STRING,
    {'P', 'I', 'C', 'O', ' ', 'S', 'W', 'I', 'T', 'C', 'H', ' ', 'P', 'R', 'O', '.'}};
static struct {
    uint8_t length, type;
    uint16_t chars[14];
} __attribute__((packed)) product = {
    30,
    USB_DTYPE_STRING,
    {'J', '2', '4', ' ', 'C', 'O', 'N', 'T', 'R', 'O', 'L', 'L', 'E', 'R'}};

static void ep_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    if(event == usbd_evt_eptx) {
        if(ep == PAD_IN) furi_semaphore_release(pad_ready);
        if(ep == KBD_IN) furi_semaphore_release(kbd_ready);
    } else if(event == usbd_evt_eprx) {
        if(ep == PAD_OUT) usbd_ep_read(dev, PAD_OUT, pad_out, sizeof(pad_out));
        if(ep == KBD_OUT) usbd_ep_read(dev, KBD_OUT, &kbd_led, 1);
    }
}

static usbd_respond config_callback(usbd_device* dev, uint8_t cfg) {
    configured = false;
    const uint8_t eps[] = {PAD_IN, PAD_OUT, KBD_IN, KBD_OUT};
    if(cfg == 0) {
        for(size_t i = 0; i < sizeof(eps); ++i) {
            usbd_ep_deconfig(dev, eps[i]);
            usbd_reg_endpoint(dev, eps[i], NULL);
        }
        furi_semaphore_release(pad_ready);
        furi_semaphore_release(kbd_ready);
        return usbd_ack;
    }
    if(cfg != 1) return usbd_fail;
    bool ok = usbd_ep_config(dev, PAD_IN, USB_EPTYPE_INTERRUPT, 64) &&
              usbd_ep_config(dev, PAD_OUT, USB_EPTYPE_INTERRUPT, 64) &&
              usbd_ep_config(dev, KBD_IN, USB_EPTYPE_INTERRUPT, 8) &&
              usbd_ep_config(dev, KBD_OUT, USB_EPTYPE_INTERRUPT, 8);
    if(!ok) {
        for(size_t i = 0; i < sizeof(eps); ++i)
            usbd_ep_deconfig(dev, eps[i]);
        return usbd_fail;
    }
    usbd_reg_endpoint(dev, PAD_IN, ep_callback);
    usbd_reg_endpoint(dev, PAD_OUT, ep_callback);
    usbd_reg_endpoint(dev, KBD_IN, ep_callback);
    usbd_reg_endpoint(dev, KBD_OUT, ep_callback);
    pad_report = (CivPadReport){0, 8, 128, 128, 128, 128, 0};
    memset(&kbd_report, 0, sizeof(kbd_report));
    kbd_protocol = 1;
    suspended = false;
    furi_semaphore_release(pad_ready);
    furi_semaphore_release(kbd_ready);
    configured = true;
    return usbd_ack;
}

static void set_report_complete(usbd_device* dev, usbd_ctlreq* req) {
    UNUSED(dev);
    if(req->wIndex == 1 && req->wLength == 1 && (req->wValue >> 8) == USB_HID_REPORT_OUT)
        kbd_led = req->data[0];
}

static usbd_respond control_callback(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* cb) {
    if(req->wIndex > 1) return usbd_fail;
    const uint8_t itf = req->wIndex;
    const uint8_t kind = req->bmRequestType & (USB_REQ_RECIPIENT | USB_REQ_TYPE);
    if(kind == (USB_REQ_INTERFACE | USB_REQ_STANDARD) && req->bRequest == USB_STD_GET_DESCRIPTOR) {
        uint8_t type = req->wValue >> 8;
        if(type == USB_DTYPE_HID) {
            dev->status.data_ptr =
                (uint8_t*)switch_usb_configuration_descriptor + (itf == 0 ? 18 : 50);
            dev->status.data_count = 9;
            return usbd_ack;
        }
        if(type == USB_DTYPE_HID_REPORT) {
            dev->status.data_ptr =
                (void*)(itf == 0 ? switch_usb_report_descriptor : keyboard_usb_report_descriptor);
            dev->status.data_count = itf == 0 ? sizeof(switch_usb_report_descriptor) :
                                                sizeof(keyboard_usb_report_descriptor);
            return usbd_ack;
        }
    }
    if(kind != (USB_REQ_INTERFACE | USB_REQ_CLASS)) return usbd_fail;
    switch(req->bRequest) {
    case USB_HID_GETREPORT:
        if((req->wValue >> 8) != USB_HID_REPORT_IN || (req->wValue & 0xFF)) return usbd_fail;
        dev->status.data_ptr = itf == 0 ? (void*)&pad_report : (void*)&kbd_report;
        dev->status.data_count = itf == 0 ? sizeof(pad_report) : sizeof(kbd_report);
        return usbd_ack;
    case USB_HID_SETREPORT:
        if((req->wValue >> 8) != USB_HID_REPORT_OUT || (req->wValue & 0xFF)) return usbd_fail;
        if(itf == 1 && req->wLength == 1) {
            *cb = set_report_complete;
            return usbd_ack;
        }
        if(itf == 0 && req->wLength <= sizeof(pad_out)) return usbd_ack;
        return usbd_fail;
    case USB_HID_SETIDLE:
        if(itf == 0)
            pad_idle = req->wValue >> 8;
        else
            kbd_idle = req->wValue >> 8;
        return usbd_ack;
    case USB_HID_GETIDLE:
        dev->status.data_ptr = itf == 0 ? (void*)&pad_idle : (void*)&kbd_idle;
        dev->status.data_count = 1;
        return usbd_ack;
    case USB_HID_SETPROTOCOL:
        if(itf != 1 || req->wValue > 1) return usbd_fail;
        kbd_protocol = req->wValue;
        return usbd_ack;
    case USB_HID_GETPROTOCOL:
        if(itf != 1) return usbd_fail;
        dev->status.data_ptr = &kbd_protocol;
        dev->status.data_count = 1;
        return usbd_ack;
    default:
        return usbd_fail;
    }
}

static void init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    UNUSED(intf);
    UNUSED(ctx);
    if(!pad_ready) pad_ready = furi_semaphore_alloc(1, 1);
    if(!kbd_ready) kbd_ready = furi_semaphore_alloc(1, 1);
    device = dev;
    configured = suspended = false;
    usbd_reg_config(dev, config_callback);
    usbd_reg_control(dev, control_callback);
    usbd_connect(dev, true);
}

static void deinit(usbd_device* dev) {
    configured = false;
    usbd_reg_config(dev, NULL);
    usbd_reg_control(dev, NULL);
    device = NULL;
    if(pad_ready) {
        furi_semaphore_free(pad_ready);
        pad_ready = NULL;
    }
    if(kbd_ready) {
        furi_semaphore_free(kbd_ready);
        kbd_ready = NULL;
    }
}

static void wakeup(usbd_device* dev) {
    UNUSED(dev);
    suspended = false;
}
static void suspend_cb(usbd_device* dev) {
    UNUSED(dev);
    suspended = true;
    if(pad_ready) furi_semaphore_release(pad_ready);
    if(kbd_ready) furi_semaphore_release(kbd_ready);
}

FuriHalUsbInterface civ_usb_interface = {
    .init = init,
    .deinit = deinit,
    .wakeup = wakeup,
    .suspend = suspend_cb,
    .dev_descr = (struct usb_device_descriptor*)switch_usb_device_descriptor,
    .str_manuf_descr = &manuf,
    .str_prod_descr = &product,
    .str_serial_descr = NULL,
    .cfg_descr = (void*)switch_usb_configuration_descriptor,
};

bool civ_usb_connected(void) {
    return configured && !suspended && device &&
           device->status.device_state == usbd_state_configured;
}

static bool send(uint8_t ep, FuriSemaphore* ready, const void* report, uint16_t len) {
    if(!civ_usb_connected() || !ready) return false;
    if(furi_semaphore_acquire(ready, 20) != FuriStatusOk) return false;
    if(!civ_usb_connected()) {
        furi_semaphore_release(ready);
        return false;
    }
    if(usbd_ep_write(device, ep, report, len) != len) {
        furi_semaphore_release(ready);
        return false;
    }
    return true;
}

bool civ_usb_send_pad(bool pressed) {
    pad_report.buttons = pressed ? 0x0004 : 0;
    return send(PAD_IN, pad_ready, &pad_report, sizeof(pad_report));
}

bool civ_usb_send_keyboard(uint8_t modifier, uint8_t key) {
    kbd_report.modifier = modifier;
    kbd_report.key[0] = key;
    return send(KBD_IN, kbd_ready, &kbd_report, sizeof(kbd_report));
}

void civ_usb_neutral(void) {
    civ_usb_send_pad(false);
    civ_usb_send_keyboard(0, 0);
}
