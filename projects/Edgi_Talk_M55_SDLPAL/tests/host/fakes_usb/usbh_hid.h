#ifndef USBH_HID_H
#define USBH_HID_H

#include "usbh_core.h"

#define HID_SUBCLASS_BOOTIF 1
#define HID_PROTOCOL_BOOT 0
#define HID_PROTOCOL_KEYBOARD 1
#define HID_PROTOCOL_REPORT 1
#define HID_PROTOCOL_MOUSE 2

struct usbh_hid
{
    struct usbh_hubport *hport;
    struct usb_endpoint_descriptor *intin;
    struct usbh_urb intin_urb;
    uint8_t protocol;
    uint8_t intf;
};

int usbh_hid_set_protocol(struct usbh_hid *hid_class, uint8_t protocol);
void usbh_hid_run(struct usbh_hid *hid_class);
void usbh_hid_stop(struct usbh_hid *hid_class);

#endif
