#ifndef PAL_USB_KEYBOARD_CORE_H
#define PAL_USB_KEYBOARD_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pal_controls.h"

#define PAL_USB_KEYBOARD_BOOT_REPORT_SIZE 8u
#define PAL_USB_KEYBOARD_KEY_SLOTS 6u

typedef enum pal_usb_keyboard_report_result
{
    PAL_USB_KEYBOARD_REPORT_OK = 0,
    PAL_USB_KEYBOARD_REPORT_INVALID,
    PAL_USB_KEYBOARD_REPORT_ROLLOVER
} pal_usb_keyboard_report_result_t;

typedef struct pal_usb_keyboard_state
{
    uint8_t report[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE];
} pal_usb_keyboard_state_t;

typedef struct pal_usb_keyboard_event
{
    bool pressed;
    uint8_t usage;
} pal_usb_keyboard_event_t;

typedef void (*pal_usb_keyboard_event_callback_t)(
    void *context, const pal_usb_keyboard_event_t *event);

void pal_usb_keyboard_state_init(pal_usb_keyboard_state_t *state);
pal_usb_keyboard_report_result_t pal_usb_keyboard_process(
    pal_usb_keyboard_state_t *state, const uint8_t *report, size_t length,
    pal_usb_keyboard_event_callback_t callback, void *context);
void pal_usb_keyboard_release_all(
    pal_usb_keyboard_state_t *state,
    pal_usb_keyboard_event_callback_t callback, void *context);
const char *pal_usb_keyboard_usage_name(uint8_t usage);
uint32_t pal_usb_keyboard_control(uint8_t usage);
const char *pal_usb_keyboard_control_name(uint32_t control);

#endif
