#include "pal_usb_keyboard_core.h"

#include <string.h>

static const char *const letter_names[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};

static const char *const digit_names[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};

static const char *const boot_usage_names[] = {
    "ENTER", "ESCAPE", "BACKSPACE", "TAB", "SPACE", "MINUS", "EQUAL",
    "LEFT_BRACKET", "RIGHT_BRACKET", "BACKSLASH", "NON_US_HASH",
    "SEMICOLON", "APOSTROPHE", "GRAVE", "COMMA", "PERIOD", "SLASH",
    "CAPS_LOCK", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8",
    "F9", "F10", "F11", "F12", "PRINT_SCREEN", "SCROLL_LOCK", "PAUSE",
    "INSERT", "HOME", "PAGE_UP", "DELETE", "END", "PAGE_DOWN", "RIGHT",
    "LEFT", "DOWN", "UP", "NUM_LOCK", "KEYPAD_SLASH", "KEYPAD_ASTERISK",
    "KEYPAD_MINUS", "KEYPAD_PLUS", "KEYPAD_ENTER", "KEYPAD_1", "KEYPAD_2",
    "KEYPAD_3", "KEYPAD_4", "KEYPAD_5", "KEYPAD_6", "KEYPAD_7", "KEYPAD_8",
    "KEYPAD_9", "KEYPAD_0", "KEYPAD_PERIOD", "NON_US_BACKSLASH",
    "APPLICATION", "POWER", "KEYPAD_EQUAL", "F13", "F14", "F15", "F16",
    "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24"};

static const char *const modifier_names[] = {
    "LEFT_CTRL", "LEFT_SHIFT", "LEFT_ALT", "LEFT_GUI",
    "RIGHT_CTRL", "RIGHT_SHIFT", "RIGHT_ALT", "RIGHT_GUI"};

static bool key_slots_contain(
    const uint8_t report[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE], uint8_t usage)
{
    size_t index;

    for (index = 2u; index < PAL_USB_KEYBOARD_BOOT_REPORT_SIZE; ++index)
    {
        if (report[index] == usage)
        {
            return true;
        }
    }
    return false;
}

static bool first_key_occurrence(
    const uint8_t report[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE], size_t index)
{
    size_t previous;

    for (previous = 2u; previous < index; ++previous)
    {
        if (report[previous] == report[index])
        {
            return false;
        }
    }
    return true;
}

static bool has_rollover(
    const uint8_t report[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE])
{
    size_t index;

    for (index = 2u; index < PAL_USB_KEYBOARD_BOOT_REPORT_SIZE; ++index)
    {
        if (report[index] >= 0x01u && report[index] <= 0x03u)
        {
            return true;
        }
    }
    return false;
}

static void emit_event(pal_usb_keyboard_event_callback_t callback,
                       void *context, bool pressed, uint8_t usage)
{
    pal_usb_keyboard_event_t event;

    if (callback == NULL)
    {
        return;
    }
    event.pressed = pressed;
    event.usage = usage;
    callback(context, &event);
}

void pal_usb_keyboard_state_init(pal_usb_keyboard_state_t *state)
{
    if (state != NULL)
    {
        memset(state, 0, sizeof(*state));
    }
}

pal_usb_keyboard_report_result_t pal_usb_keyboard_process(
    pal_usb_keyboard_state_t *state, const uint8_t *report, size_t length,
    pal_usb_keyboard_event_callback_t callback, void *context)
{
    size_t index;

    if (state == NULL || report == NULL ||
        length != PAL_USB_KEYBOARD_BOOT_REPORT_SIZE)
    {
        return PAL_USB_KEYBOARD_REPORT_INVALID;
    }
    if (has_rollover(report))
    {
        return PAL_USB_KEYBOARD_REPORT_ROLLOVER;
    }

    for (index = 0u; index < 8u; ++index)
    {
        uint8_t bit = (uint8_t)(1u << index);
        if ((state->report[0] & bit) != 0u && (report[0] & bit) == 0u)
        {
            emit_event(callback, context, false, (uint8_t)(0xe0u + index));
        }
    }
    for (index = 2u; index < PAL_USB_KEYBOARD_BOOT_REPORT_SIZE; ++index)
    {
        uint8_t usage = state->report[index];
        if (usage != 0u && first_key_occurrence(state->report, index) &&
            !key_slots_contain(report, usage))
        {
            emit_event(callback, context, false, usage);
        }
    }
    for (index = 0u; index < 8u; ++index)
    {
        uint8_t bit = (uint8_t)(1u << index);
        if ((report[0] & bit) != 0u && (state->report[0] & bit) == 0u)
        {
            emit_event(callback, context, true, (uint8_t)(0xe0u + index));
        }
    }
    for (index = 2u; index < PAL_USB_KEYBOARD_BOOT_REPORT_SIZE; ++index)
    {
        uint8_t usage = report[index];
        if (usage != 0u && first_key_occurrence(report, index) &&
            !key_slots_contain(state->report, usage))
        {
            emit_event(callback, context, true, usage);
        }
    }

    memcpy(state->report, report, PAL_USB_KEYBOARD_BOOT_REPORT_SIZE);
    return PAL_USB_KEYBOARD_REPORT_OK;
}

void pal_usb_keyboard_release_all(
    pal_usb_keyboard_state_t *state,
    pal_usb_keyboard_event_callback_t callback, void *context)
{
    static const uint8_t released[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE] = {0u};

    (void)pal_usb_keyboard_process(state, released, sizeof(released),
                                   callback, context);
}

const char *pal_usb_keyboard_usage_name(uint8_t usage)
{
    if (usage >= 0x04u && usage <= 0x1du)
    {
        return letter_names[usage - 0x04u];
    }
    if (usage >= 0x1eu && usage <= 0x27u)
    {
        return digit_names[usage - 0x1eu];
    }
    if (usage >= 0x28u && usage <= 0x73u)
    {
        return boot_usage_names[usage - 0x28u];
    }
    if (usage >= 0xe0u && usage <= 0xe7u)
    {
        return modifier_names[usage - 0xe0u];
    }
    return "UNKNOWN";
}

uint32_t pal_usb_keyboard_control(uint8_t usage)
{
    switch (usage)
    {
    case 0x52u:
        return PAL_CONTROL_UP;
    case 0x51u:
        return PAL_CONTROL_DOWN;
    case 0x50u:
        return PAL_CONTROL_LEFT;
    case 0x4fu:
        return PAL_CONTROL_RIGHT;
    case 0x28u:
        return PAL_CONTROL_A;
    case 0x29u:
        return PAL_CONTROL_B;
    case 0x4bu:
        return PAL_CONTROL_PGUP;
    case 0x4eu:
        return PAL_CONTROL_PGDN;
    default:
        return 0u;
    }
}

const char *pal_usb_keyboard_control_name(uint32_t control)
{
    switch (control)
    {
    case PAL_CONTROL_UP:
        return "PAL_CONTROL_UP";
    case PAL_CONTROL_DOWN:
        return "PAL_CONTROL_DOWN";
    case PAL_CONTROL_LEFT:
        return "PAL_CONTROL_LEFT";
    case PAL_CONTROL_RIGHT:
        return "PAL_CONTROL_RIGHT";
    case PAL_CONTROL_A:
        return "PAL_CONTROL_A";
    case PAL_CONTROL_B:
        return "PAL_CONTROL_B";
    case PAL_CONTROL_PGUP:
        return "PAL_CONTROL_PGUP";
    case PAL_CONTROL_PGDN:
        return "PAL_CONTROL_PGDN";
    default:
        return "NONE";
    }
}
