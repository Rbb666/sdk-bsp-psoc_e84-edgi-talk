#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pal_usb_keyboard_core.h"

typedef struct recorded_event
{
    bool pressed;
    uint8_t usage;
} recorded_event_t;

static recorded_event_t events[32];
static size_t event_count;

static void record_event(void *context,
                         const pal_usb_keyboard_event_t *event)
{
    (void)context;
    assert(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count++] = (recorded_event_t){event->pressed, event->usage};
}

static void clear_events(void)
{
    memset(events, 0, sizeof(events));
    event_count = 0u;
}

static void test_press_hold_release(void)
{
    pal_usb_keyboard_state_t state;
    uint8_t report[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE] = {0u};

    pal_usb_keyboard_state_init(&state);
    report[2] = 0x04u;
    assert(pal_usb_keyboard_process(&state, report, sizeof(report),
                                    record_event, NULL) ==
           PAL_USB_KEYBOARD_REPORT_OK);
    assert(event_count == 1u && events[0].pressed && events[0].usage == 0x04u);

    clear_events();
    assert(pal_usb_keyboard_process(&state, report, sizeof(report),
                                    record_event, NULL) ==
           PAL_USB_KEYBOARD_REPORT_OK);
    assert(event_count == 0u);

    report[2] = 0u;
    assert(pal_usb_keyboard_process(&state, report, sizeof(report),
                                    record_event, NULL) ==
           PAL_USB_KEYBOARD_REPORT_OK);
    assert(event_count == 1u && !events[0].pressed && events[0].usage == 0x04u);
}

static void test_modifiers_duplicates_and_six_keys(void)
{
    pal_usb_keyboard_state_t state;
    uint8_t report[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE] = {
        0x03u, 0u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u};

    pal_usb_keyboard_state_init(&state);
    clear_events();
    assert(pal_usb_keyboard_process(&state, report, sizeof(report),
                                    record_event, NULL) ==
           PAL_USB_KEYBOARD_REPORT_OK);
    assert(event_count == 8u);
    assert(events[0].usage == 0xe0u && events[0].pressed);
    assert(events[1].usage == 0xe1u && events[1].pressed);

    report[3] = 0x04u;
    clear_events();
    assert(pal_usb_keyboard_process(&state, report, sizeof(report),
                                    record_event, NULL) ==
           PAL_USB_KEYBOARD_REPORT_OK);
    assert(event_count == 1u);
    assert(!events[0].pressed && events[0].usage == 0x05u);
}

static void test_invalid_rollover_and_disconnect(void)
{
    pal_usb_keyboard_state_t state;
    uint8_t down[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE] = {0u, 0u, 0x52u};
    uint8_t rollover[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE] = {
        0u, 0u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u};

    pal_usb_keyboard_state_init(&state);
    clear_events();
    assert(pal_usb_keyboard_process(&state, down, sizeof(down),
                                    record_event, NULL) ==
           PAL_USB_KEYBOARD_REPORT_OK);
    clear_events();
    assert(pal_usb_keyboard_process(&state, rollover, sizeof(rollover),
                                    record_event, NULL) ==
           PAL_USB_KEYBOARD_REPORT_ROLLOVER);
    assert(event_count == 0u);
    assert(pal_usb_keyboard_process(&state, down, 7u, record_event, NULL) ==
           PAL_USB_KEYBOARD_REPORT_INVALID);
    assert(event_count == 0u);
    pal_usb_keyboard_release_all(&state, record_event, NULL);
    assert(event_count == 1u && !events[0].pressed && events[0].usage == 0x52u);
}

static void test_names_and_controls(void)
{
    static const char *const letters[] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K",
        "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
        "W", "X", "Y", "Z"};
    static const char *const digits[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    static const char *const boot_names[] = {
        "ENTER", "ESCAPE", "BACKSPACE", "TAB", "SPACE", "MINUS",
        "EQUAL", "LEFT_BRACKET", "RIGHT_BRACKET", "BACKSLASH",
        "NON_US_HASH", "SEMICOLON", "APOSTROPHE", "GRAVE", "COMMA",
        "PERIOD", "SLASH", "CAPS_LOCK", "F1", "F2", "F3", "F4",
        "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
        "PRINT_SCREEN", "SCROLL_LOCK", "PAUSE", "INSERT", "HOME",
        "PAGE_UP", "DELETE", "END", "PAGE_DOWN", "RIGHT", "LEFT",
        "DOWN", "UP", "NUM_LOCK", "KEYPAD_SLASH", "KEYPAD_ASTERISK",
        "KEYPAD_MINUS", "KEYPAD_PLUS", "KEYPAD_ENTER", "KEYPAD_1",
        "KEYPAD_2", "KEYPAD_3", "KEYPAD_4", "KEYPAD_5", "KEYPAD_6",
        "KEYPAD_7", "KEYPAD_8", "KEYPAD_9", "KEYPAD_0",
        "KEYPAD_PERIOD", "NON_US_BACKSLASH", "APPLICATION", "POWER",
        "KEYPAD_EQUAL", "F13", "F14", "F15", "F16", "F17", "F18",
        "F19", "F20", "F21", "F22", "F23", "F24"};
    static const char *const modifiers[] = {
        "LEFT_CTRL", "LEFT_SHIFT", "LEFT_ALT", "LEFT_GUI",
        "RIGHT_CTRL", "RIGHT_SHIFT", "RIGHT_ALT", "RIGHT_GUI"};
    size_t index;

    for (index = 0u; index < sizeof(letters) / sizeof(letters[0]); ++index)
    {
        assert(strcmp(pal_usb_keyboard_usage_name((uint8_t)(0x04u + index)),
                      letters[index]) == 0);
    }
    for (index = 0u; index < sizeof(digits) / sizeof(digits[0]); ++index)
    {
        assert(strcmp(pal_usb_keyboard_usage_name((uint8_t)(0x1eu + index)),
                      digits[index]) == 0);
    }
    for (index = 0u; index < sizeof(boot_names) / sizeof(boot_names[0]);
         ++index)
    {
        assert(strcmp(pal_usb_keyboard_usage_name((uint8_t)(0x28u + index)),
                      boot_names[index]) == 0);
    }
    for (index = 0u; index < sizeof(modifiers) / sizeof(modifiers[0]); ++index)
    {
        assert(strcmp(pal_usb_keyboard_usage_name((uint8_t)(0xe0u + index)),
                      modifiers[index]) == 0);
    }
    assert(strcmp(pal_usb_keyboard_usage_name(0xfeu), "UNKNOWN") == 0);
    assert(pal_usb_keyboard_control(0x52u) == PAL_CONTROL_UP);
    assert(pal_usb_keyboard_control(0x51u) == PAL_CONTROL_DOWN);
    assert(pal_usb_keyboard_control(0x50u) == PAL_CONTROL_LEFT);
    assert(pal_usb_keyboard_control(0x4fu) == PAL_CONTROL_RIGHT);
    assert(pal_usb_keyboard_control(0x28u) == PAL_CONTROL_A);
    assert(pal_usb_keyboard_control(0x29u) == PAL_CONTROL_B);
    assert(pal_usb_keyboard_control(0x4bu) == PAL_CONTROL_PGUP);
    assert(pal_usb_keyboard_control(0x4eu) == PAL_CONTROL_PGDN);
    assert(pal_usb_keyboard_control(0x04u) == 0u);
    assert(strcmp(pal_usb_keyboard_control_name(PAL_CONTROL_UP),
                  "PAL_CONTROL_UP") == 0);
    assert(strcmp(pal_usb_keyboard_control_name(0u), "NONE") == 0);
}

int main(void)
{
    test_press_hold_release();
    test_modifiers_duplicates_and_six_keys();
    test_invalid_rollover_and_disconnect();
    test_names_and_controls();
    puts("usb_keyboard_core: PASS");
    return 0;
}
