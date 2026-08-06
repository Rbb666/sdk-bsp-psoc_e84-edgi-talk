#ifndef PAL_USB_KEYBOARD_PORT_H
#define PAL_USB_KEYBOARD_PORT_H

#include <stdbool.h>
#include <stdint.h>

bool pal_usb_keyboard_host_start(void);
uint32_t pal_usb_keyboard_controls_get(void);

#endif
