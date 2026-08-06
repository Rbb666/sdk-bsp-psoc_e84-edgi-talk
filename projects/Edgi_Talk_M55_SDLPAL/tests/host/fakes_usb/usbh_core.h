#ifndef USBH_CORE_H
#define USBH_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define USB_ERR_NAK 10
#define USB_ERR_IO 12
#define USB_GET_MAXPACKETSIZE(value) ((uint16_t)(value) & 0x07ffu)

typedef void (*usbh_complete_callback_t)(void *arg, int nbytes);

struct usb_device_descriptor
{
    uint16_t idVendor;
    uint16_t idProduct;
};

struct usb_endpoint_descriptor
{
    uint8_t bEndpointAddress;
    uint16_t wMaxPacketSize;
};

struct usb_interface_descriptor
{
    uint8_t bInterfaceSubClass;
};

struct usbh_interface_altsetting
{
    struct usb_interface_descriptor intf_desc;
};

struct usbh_interface
{
    struct usbh_interface_altsetting altsetting[1];
};

struct usbh_configuration
{
    struct usbh_interface intf[1];
};

struct usbh_hubport
{
    struct usb_device_descriptor device_desc;
    struct usbh_configuration config;
};

struct usbh_urb
{
    struct usbh_hubport *hport;
    struct usb_endpoint_descriptor *ep;
    uint8_t *transfer_buffer;
    uint32_t transfer_buffer_length;
    uint32_t timeout;
    usbh_complete_callback_t complete;
    void *arg;
};

static inline void usbh_int_urb_fill(
    struct usbh_urb *urb, struct usbh_hubport *hport,
    struct usb_endpoint_descriptor *ep, uint8_t *buffer,
    uint32_t length, uint32_t timeout,
    usbh_complete_callback_t complete, void *arg)
{
    urb->hport = hport;
    urb->ep = ep;
    urb->transfer_buffer = buffer;
    urb->transfer_buffer_length = length;
    urb->timeout = timeout;
    urb->complete = complete;
    urb->arg = arg;
}

int usbh_initialize(uint8_t busid, uintptr_t reg_base, void *event_handler);
int usbh_submit_urb(struct usbh_urb *urb);

#endif
