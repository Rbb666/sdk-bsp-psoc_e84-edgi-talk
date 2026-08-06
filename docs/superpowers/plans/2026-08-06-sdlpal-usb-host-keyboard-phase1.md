# SDLPal USB Host Keyboard Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enumerate one directly connected USB HID Boot Keyboard through CherryUSB on the PSoC Edge M55 and print stable press/release plus future SDLPal control mappings without changing game input behavior.

**Architecture:** Keep CherryUSB core and class sources unchanged, and override its weak HID lifecycle hooks in the SDLPal platform layer. Split report decoding into a host-testable pure-C core; keep USB DMA, RT-Thread queue/thread ownership, and serial logging in a thin target port. Enable only Infineon DWC2 Host and HID in the checked-in project configuration, and retain CherryUSB class metadata in the linker script.

**Tech Stack:** C99, RT-Thread static threads/message queues, CherryUSB 1.6.0 HID Host, Infineon DWC2, SCons, MinGW Host tests, Python `unittest`, ARM GCC, ELF/linker contract checks.

## Global Constraints

- Phase 1 supports exactly one active, directly connected HID Boot Keyboard using the standard 8-byte report.
- Do not generate SDL events, modify `PalEngineBridge_PollEvent()`, disable touch, remove on-screen controls, or resize the viewport.
- Do not modify `libraries/components/CherryUSB-1.6.0/core/**`, `libraries/components/CherryUSB-1.6.0/class/**`, or `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/**`.
- Do not enable MSC, UVC, CDC, hubs as a supported topology, NKRO, Report-ID keyboards, consumer controls, or multiple keyboards.
- All DMA buffers must remain in `.cy_socmem_data` and be aligned to `CONFIG_USB_ALIGN_SIZE` (32 bytes for Infineon DWC2).
- All worker resources must be static; do not use `rt_thread_create()`, dynamic message queues, or runtime heap allocation.
- USB failure must never prevent SD-card validation, touch initialization, or SDLPal engine startup.
- All shell commands in this repository must begin with `rtk`.
- Baseline design commit for protected-file comparisons: `c2f8f182`.

---

### Task 1: Pure Boot Keyboard Report Decoder

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_usb_keyboard_core.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_usb_keyboard_core.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_usb_keyboard_core.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Interfaces:**
- Consumes: `pal_control_t` values from `platform/pal_controls.h`.
- Produces: `pal_usb_keyboard_state_init()`, `pal_usb_keyboard_process()`, `pal_usb_keyboard_release_all()`, `pal_usb_keyboard_usage_name()`, `pal_usb_keyboard_control()`, and `pal_usb_keyboard_control_name()`.
- Produces exact report result values `PAL_USB_KEYBOARD_REPORT_OK`, `PAL_USB_KEYBOARD_REPORT_INVALID`, and `PAL_USB_KEYBOARD_REPORT_ROLLOVER`.

- [ ] **Step 1: Add the failing decoder test and Host build target**

Create the test around this public contract:

```c
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
    assert(strcmp(pal_usb_keyboard_usage_name(0x04u), "A") == 0);
    assert(strcmp(pal_usb_keyboard_usage_name(0x3au), "F1") == 0);
    assert(strcmp(pal_usb_keyboard_usage_name(0xe1u), "LEFT_SHIFT") == 0);
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
```

Add `test_usb_keyboard_core` to `.PHONY` and `all`, and add this rule:

```make
test_usb_keyboard_core: test_usb_keyboard_core.exe
	.\test_usb_keyboard_core.exe

test_usb_keyboard_core.exe: test_usb_keyboard_core.c \
		$(PLATFORM)/pal_usb_keyboard_core.c \
		$(PLATFORM)/pal_usb_keyboard_core.h \
		$(PLATFORM)/pal_controls.h
	$(HOST_CC) $(CFLAGS) $(INCLUDES) test_usb_keyboard_core.c \
		$(PLATFORM)/pal_usb_keyboard_core.c -o $@
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host test_usb_keyboard_core
```

Expected: compilation fails because `pal_usb_keyboard_core.h` and the decoder API do not exist. Confirm that this is the only cause before implementation.

- [ ] **Step 3: Add the decoder header**

Create the exact public contract:

```c
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
```

- [ ] **Step 4: Implement minimal deterministic report processing**

Implement these exact rules in `pal_usb_keyboard_core.c`:

```c
#include "pal_usb_keyboard_core.h"

#include <string.h>

static bool key_slots_contain(const uint8_t report[8], uint8_t usage)
{
    size_t index;
    for (index = 2u; index < 8u; ++index) {
        if (report[index] == usage) {
            return true;
        }
    }
    return false;
}

static bool first_key_occurrence(const uint8_t report[8], size_t index)
{
    size_t previous;
    for (previous = 2u; previous < index; ++previous) {
        if (report[previous] == report[index]) {
            return false;
        }
    }
    return true;
}

static bool has_rollover(const uint8_t report[8])
{
    size_t index;
    for (index = 2u; index < 8u; ++index) {
        if (report[index] >= 0x01u && report[index] <= 0x03u) {
            return true;
        }
    }
    return false;
}

static void emit(pal_usb_keyboard_event_callback_t callback, void *context,
                 bool pressed, uint8_t usage)
{
    pal_usb_keyboard_event_t event;
    if (callback == NULL) {
        return;
    }
    event.pressed = pressed;
    event.usage = usage;
    callback(context, &event);
}

void pal_usb_keyboard_state_init(pal_usb_keyboard_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

pal_usb_keyboard_report_result_t pal_usb_keyboard_process(
    pal_usb_keyboard_state_t *state, const uint8_t *report, size_t length,
    pal_usb_keyboard_event_callback_t callback, void *context)
{
    size_t index;
    if (state == NULL || report == NULL ||
        length != PAL_USB_KEYBOARD_BOOT_REPORT_SIZE) {
        return PAL_USB_KEYBOARD_REPORT_INVALID;
    }
    if (has_rollover(report)) {
        return PAL_USB_KEYBOARD_REPORT_ROLLOVER;
    }

    for (index = 0u; index < 8u; ++index) {
        uint8_t bit = (uint8_t)(1u << index);
        if ((state->report[0] & bit) != 0u && (report[0] & bit) == 0u) {
            emit(callback, context, false, (uint8_t)(0xe0u + index));
        }
    }
    for (index = 2u; index < 8u; ++index) {
        uint8_t usage = state->report[index];
        if (usage != 0u && first_key_occurrence(state->report, index) &&
            !key_slots_contain(report, usage)) {
            emit(callback, context, false, usage);
        }
    }
    for (index = 0u; index < 8u; ++index) {
        uint8_t bit = (uint8_t)(1u << index);
        if ((report[0] & bit) != 0u && (state->report[0] & bit) == 0u) {
            emit(callback, context, true, (uint8_t)(0xe0u + index));
        }
    }
    for (index = 2u; index < 8u; ++index) {
        uint8_t usage = report[index];
        if (usage != 0u && first_key_occurrence(report, index) &&
            !key_slots_contain(state->report, usage)) {
            emit(callback, context, true, usage);
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
```

Add immutable usage names for `A-Z` (`0x04-0x1d`), digits (`0x1e-0x27`), and modifiers (`0xe0-0xe7`). Use exact modifier names `LEFT_CTRL`, `LEFT_SHIFT`, `LEFT_ALT`, `LEFT_GUI`, `RIGHT_CTRL`, `RIGHT_SHIFT`, `RIGHT_ALT`, and `RIGHT_GUI`; return `UNKNOWN` for all other values. Name the non-alphanumeric Boot usages exactly as follows so log output is stable:

```text
0x28 ENTER             0x29 ESCAPE          0x2a BACKSPACE
0x2b TAB               0x2c SPACE           0x2d MINUS
0x2e EQUAL             0x2f LEFT_BRACKET    0x30 RIGHT_BRACKET
0x31 BACKSLASH         0x32 NON_US_HASH     0x33 SEMICOLON
0x34 APOSTROPHE        0x35 GRAVE           0x36 COMMA
0x37 PERIOD            0x38 SLASH           0x39 CAPS_LOCK
0x3a-0x45 F1-F12       0x46 PRINT_SCREEN    0x47 SCROLL_LOCK
0x48 PAUSE             0x49 INSERT          0x4a HOME
0x4b PAGE_UP           0x4c DELETE          0x4d END
0x4e PAGE_DOWN         0x4f RIGHT           0x50 LEFT
0x51 DOWN              0x52 UP              0x53 NUM_LOCK
0x54 KEYPAD_SLASH      0x55 KEYPAD_ASTERISK 0x56 KEYPAD_MINUS
0x57 KEYPAD_PLUS       0x58 KEYPAD_ENTER    0x59-0x61 KEYPAD_1-KEYPAD_9
0x62 KEYPAD_0          0x63 KEYPAD_PERIOD   0x64 NON_US_BACKSLASH
0x65 APPLICATION       0x66 POWER           0x67 KEYPAD_EQUAL
0x68-0x73 F13-F24
```

Implement the eight mapping cases exactly as listed in the test and return `NONE` for control value zero.

- [ ] **Step 5: Run the focused and full Host tests and verify GREEN**

Run:

```powershell
rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host test_usb_keyboard_core
rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host clean all
```

Expected: `usb_keyboard_core: PASS`, followed by all existing Host test executables passing with no warnings.

- [ ] **Step 6: Commit the decoder**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL/platform/pal_usb_keyboard_core.h projects/Edgi_Talk_M55_SDLPAL/platform/pal_usb_keyboard_core.c projects/Edgi_Talk_M55_SDLPAL/tests/host/test_usb_keyboard_core.c projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile
rtk git commit -m "feat: decode USB boot keyboard reports"
```

### Task 2: CherryUSB and RT-Thread Keyboard Port

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_usb_keyboard_port.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_usb_keyboard_port.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_usb_keyboard_port.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/fakes_usb/rtthread.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/fakes_usb/board.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/fakes_usb/usb_config.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/fakes_usb/usbh_core.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/fakes_usb/usbh_hid.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Interfaces:**
- Consumes: Task 1 decoder API and CherryUSB `struct usbh_hid`.
- Produces: `bool pal_usb_keyboard_host_start(void)`.
- Overrides: `void usbh_hid_run(struct usbh_hid *hid_class)` and `void usbh_hid_stop(struct usbh_hid *hid_class)`.
- Owns: one 8-byte DMA buffer, one active HID pointer, one generation counter, an 8-message static queue, and one 2 KiB static worker stack.

- [ ] **Step 1: Add a failing port lifecycle test with target API fakes**

The fake headers must define only the types and constants used by the port. Use `USBHS_BASE = 0x44900000u`, `CONFIG_USB_ALIGN_SIZE = 32`, `CONFIG_USBHOST_PSC_PRIO = 5`, `HID_SUBCLASS_BOOTIF = 1`, `HID_PROTOCOL_BOOT = 0`, `HID_PROTOCOL_KEYBOARD = 1`, `HID_PROTOCOL_MOUSE = 2`, `USB_ERR_NAK = 10`, and `USB_ERR_IO = 12`. The fake URB must retain its completion callback and argument:

```c
struct usbh_urb
{
    usbh_complete_callback_t complete;
    void *arg;
    uint8_t *transfer_buffer;
    uint32_t transfer_buffer_length;
};

static inline void usbh_int_urb_fill(
    struct usbh_urb *urb, struct usbh_hubport *hport,
    struct usb_endpoint_descriptor *ep, uint8_t *buffer,
    uint32_t length, uint32_t timeout,
    usbh_complete_callback_t complete, void *arg)
{
    (void)hport;
    (void)ep;
    (void)timeout;
    urb->complete = complete;
    urb->arg = arg;
    urb->transfer_buffer = buffer;
    urb->transfer_buffer_length = length;
}
```

Use these exact fake RT-Thread surfaces; opaque storage is sufficient because
the production port does not inspect kernel object fields:

```c
typedef int rt_err_t;
typedef long rt_ssize_t;
typedef unsigned long rt_base_t;
typedef unsigned long rt_size_t;
typedef unsigned long rt_tick_t;
typedef unsigned char rt_uint8_t;
typedef unsigned int rt_uint32_t;

struct rt_thread { uintptr_t opaque[32]; };
struct rt_messagequeue { uintptr_t opaque[32]; };
struct rt_mq_message { struct rt_mq_message *next; rt_ssize_t length; };
typedef struct rt_thread *rt_thread_t;
typedef struct rt_messagequeue *rt_mq_t;

#define RT_EOK 0
#define RT_ERROR 1
#define RT_IPC_FLAG_FIFO 0u
#define RT_IPC_CMD_RESET 1
#define RT_WAITING_FOREVER (-1)
#define RT_ALIGN_SIZE sizeof(void *)
#define RT_ALIGN(value, alignment) \
    (((value) + (alignment) - 1u) & ~((alignment) - 1u))
#define RT_MQ_BUF_SIZE(msg_size, max_msgs) \
    ((RT_ALIGN((msg_size), RT_ALIGN_SIZE) + sizeof(struct rt_mq_message)) * \
     (max_msgs))

rt_err_t rt_thread_init(struct rt_thread *thread, const char *name,
                        void (*entry)(void *), void *parameter,
                        void *stack_start, rt_uint32_t stack_size,
                        rt_uint8_t priority, rt_uint32_t tick);
rt_err_t rt_thread_startup(rt_thread_t thread);
rt_err_t rt_mq_init(rt_mq_t mq, const char *name, void *msgpool,
                    rt_size_t msg_size, rt_size_t pool_size,
                    rt_uint8_t flag);
rt_err_t rt_mq_send(rt_mq_t mq, const void *buffer, rt_size_t size);
rt_ssize_t rt_mq_recv(rt_mq_t mq, void *buffer, rt_size_t size,
                      int timeout);
rt_err_t rt_mq_control(rt_mq_t mq, int command, void *argument);
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t level);
int rt_kprintf(const char *format, ...);
```

The fake CherryUSB model must include the real field path used by 1.6.0:
`hid_class->hport->config.intf[hid_class->intf].altsetting[0].intf_desc`,
plus `hport->device_desc.idVendor/idProduct`, `hid_class->intin`,
`hid_class->intin_urb`, `hid_class->protocol`, and `hid_class->intf`.
Declare fakes for `usbh_initialize()`, `usbh_hid_set_protocol()`,
`usbh_submit_urb()`, and `usbh_int_urb_fill()`. Define
`USB_NOCACHE_RAM_SECTION` and `USB_MEM_ALIGNX` as empty in Host builds and
define `USB_GET_MAXPACKETSIZE(value)` as `(value) & 0x7ffu`.

The test must provide fakes for every linked RT-Thread/CherryUSB function, call `pal_usb_keyboard_host_start()`, construct a Boot Keyboard interface, and verify this sequence:

```c
assert(pal_usb_keyboard_host_start());
assert(host_initialize_calls == 1u);
assert(last_host_base == (uintptr_t)USBHS_BASE);

usbh_hid_run(&keyboard);
assert(set_protocol_calls == 1u);
assert(last_protocol == HID_PROTOCOL_BOOT);
assert(submit_calls == 1u);
assert(last_urb->transfer_buffer_length == PAL_USB_KEYBOARD_BOOT_REPORT_SIZE);

last_urb->transfer_buffer[2] = 0x52u;
last_urb->complete(last_urb->arg, 8);
assert(message_send_calls == 1u);
assert(submit_calls == 2u);

last_urb->complete(last_urb->arg, -USB_ERR_NAK);
assert(message_send_calls == 1u);
assert(submit_calls == 3u);

last_urb->complete(last_urb->arg, -USB_ERR_IO);
assert(message_send_calls == 2u);
assert(submit_calls == 3u);

usbh_hid_stop(&keyboard);
assert(message_queue_reset_calls == 1u);
assert(message_send_calls == 3u);
last_urb->complete(last_urb->arg, -USB_ERR_NAK);
assert(submit_calls == 3u);
```

Also verify that a mouse, a non-Boot keyboard, an endpoint with max packet size below 8, and a second keyboard while the first is active do not call `usbh_hid_set_protocol()` or `usbh_submit_urb()`.

Capture every `rt_kprintf()` call. Assert that successful and NAK completion
callbacks do not print, proving that key and statistics logs are deferred to
the worker. Capture the message bytes passed to `rt_mq_send()` and assert the
successful report is copied rather than queued as a pointer.

Add this Host rule and include the target in `.PHONY` and `all`:

```make
USB_FAKE_INCLUDES := -Ifakes_usb

test_usb_keyboard_port: test_usb_keyboard_port.exe
	.\test_usb_keyboard_port.exe

test_usb_keyboard_port.exe: test_usb_keyboard_port.c \
		$(PLATFORM)/pal_usb_keyboard_port.c \
		$(PLATFORM)/pal_usb_keyboard_port.h \
		$(PLATFORM)/pal_usb_keyboard_core.c \
		$(PLATFORM)/pal_usb_keyboard_core.h
	$(HOST_CC) $(CFLAGS) $(USB_FAKE_INCLUDES) $(INCLUDES) \
		test_usb_keyboard_port.c $(PLATFORM)/pal_usb_keyboard_port.c \
		$(PLATFORM)/pal_usb_keyboard_core.c -o $@
```

- [ ] **Step 2: Run the port test and verify RED**

```powershell
rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host test_usb_keyboard_port
```

Expected: compilation fails because `pal_usb_keyboard_port.h`, `pal_usb_keyboard_host_start()`, and the strong HID hooks do not exist.

- [ ] **Step 3: Implement the static port resources and public start function**

Create this public header:

```c
#ifndef PAL_USB_KEYBOARD_PORT_H
#define PAL_USB_KEYBOARD_PORT_H

#include <stdbool.h>

bool pal_usb_keyboard_host_start(void);

#endif
```

In the source, define:

```c
#define PAL_USB_KEYBOARD_QUEUE_DEPTH 8u
#define PAL_USB_KEYBOARD_STACK_BYTES (2u * 1024u)

#if defined(__GNUC__)
#define PAL_USB_WORKER_STORAGE \
    __attribute__((section(".sdlpal_usb"), aligned(8)))
#else
#define PAL_USB_WORKER_STORAGE
#endif

typedef enum pal_usb_message_type
{
    PAL_USB_MESSAGE_REPORT,
    PAL_USB_MESSAGE_DISCONNECT,
    PAL_USB_MESSAGE_ERROR,
    PAL_USB_MESSAGE_STATS
} pal_usb_message_type_t;

typedef struct pal_usb_message
{
    pal_usb_message_type_t type;
    uint32_t generation;
    int status;
    uint8_t report[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE];
} pal_usb_message_t;
```

Place the static thread control block, 2 KiB stack, queue object, and queue pool in `.sdlpal_usb`. Place the report DMA buffer in `.cy_socmem_data` with `USB_MEM_ALIGNX`. Initialize the message queue with `RT_MQ_BUF_SIZE(sizeof(pal_usb_message_t), PAL_USB_KEYBOARD_QUEUE_DEPTH)`, initialize a static thread named `pal_usb`, and start it before calling `usbh_initialize(0, USBHS_BASE, NULL)`.

`pal_usb_keyboard_host_start()` must be idempotent. Resource initialization errors log `[PAL USB] worker init failed: <status>` and return `false`. Host initialization errors log `[PAL USB] host init failed: <status>` and return `false`. Success logs:

```c
rt_kprintf("[PAL USB] host ready: bus=0 base=0x%08lx\n",
           (unsigned long)USBHS_BASE);
```

Guard each read-modify-write of the active HID pointer, generation, and
callback-owned counters with `rt_hw_interrupt_disable()` /
`rt_hw_interrupt_enable()`. Never keep interrupts disabled while calling
CherryUSB, an RT-Thread queue API, or `rt_kprintf()`.

- [ ] **Step 4: Implement qualification, URB lifecycle, and deferred logging**

Implement `is_boot_keyboard()` using the selected interface descriptor and require:

```c
intf_desc->bInterfaceSubClass == HID_SUBCLASS_BOOTIF
hid_class->protocol == HID_PROTOCOL_KEYBOARD
hid_class->intin != NULL
USB_GET_MAXPACKETSIZE(hid_class->intin->wMaxPacketSize) >=
    PAL_USB_KEYBOARD_BOOT_REPORT_SIZE
```

`usbh_hid_run()` must ignore unsupported HID interfaces, reject a second active keyboard, call `usbh_hid_set_protocol(hid_class, HID_PROTOCOL_BOOT)`, claim a new nonzero generation, fill an interrupt IN URB for exactly 8 bytes, submit it, and then log VID, PID, endpoint address, and max packet size. If the first submission fails, clear ownership and log the failure without announcing a connection.

The completion callback must:

1. Return immediately when its argument is not the active HID instance.
2. Copy exactly 8 successful bytes into a local queue message.
3. Count other nonnegative lengths as invalid and enqueue a `PAL_USB_MESSAGE_STATS` wake-up.
4. Treat `-USB_ERR_NAK` as normal and resubmit without logging.
5. Enqueue `PAL_USB_MESSAGE_ERROR` and stop resubmission for other negative results.
6. Recheck active ownership before resubmitting.

`usbh_hid_stop()` must clear active ownership, reset the queue with `RT_IPC_CMD_RESET`, enqueue a disconnect message containing only the generation, and leave no CherryUSB pointer in the queue.

The worker maintains a local `pal_usb_keyboard_state_t`. A generation change releases old keys and resets state. Report messages call Task 1's decoder. Log events exactly as:

```c
rt_kprintf("[PAL KEY] %s usage=0x%02x key=%s action=%s\n",
           event->pressed ? "DOWN" : "UP  ",
           event->usage,
           pal_usb_keyboard_usage_name(event->usage),
           pal_usb_keyboard_control_name(
               pal_usb_keyboard_control(event->usage)));
```

Print only the first rollover warning in a consecutive rollover sequence. Aggregate invalid-length and queue-drop counters, and print their accumulated values from the worker, never from the completion callback. Disconnect handling must call `pal_usb_keyboard_release_all()` before logging `[PAL USB] keyboard disconnected`.

- [ ] **Step 5: Run focused and full Host tests and verify GREEN**

```powershell
rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host test_usb_keyboard_port
rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host clean all
```

Expected: `usb_keyboard_port: PASS`, `usb_keyboard_core: PASS`, and all pre-existing Host tests pass without warnings.

- [ ] **Step 6: Commit the port**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL/platform/pal_usb_keyboard_port.h projects/Edgi_Talk_M55_SDLPAL/platform/pal_usb_keyboard_port.c projects/Edgi_Talk_M55_SDLPAL/tests/host/test_usb_keyboard_port.c projects/Edgi_Talk_M55_SDLPAL/tests/host/fakes_usb projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile
rtk git commit -m "feat: add CherryUSB keyboard diagnostics port"
```

### Task 3: Target Configuration, Linker, and Startup Integration

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/.config`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/rtconfig.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/SConscript`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/board/linker_scripts/link.ld`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/applications/main.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_check_elf.py`

**Interfaces:**
- Consumes: `pal_usb_keyboard_host_start()` from Task 2.
- Produces: linked CherryUSB Host/HID classes, DWC2 controller support, class metadata boundaries, and zeroed `.sdlpal_usb` storage.
- Preserves: existing touch-only `PalEngineBridge_PollEvent()`.

- [ ] **Step 1: Add the failing target and ELF integration contracts**

Add a new test method that reads the six files above plus `pal_engine_bridge.c` and asserts:

```python
    def test_usb_keyboard_host_phase1_contract(self):
        config = (ROOT / ".config").read_text(encoding="utf-8")
        rtconfig = (ROOT / "rtconfig.h").read_text(encoding="utf-8")
        platform_build = (ROOT / "platform" / "SConscript").read_text(
            encoding="utf-8"
        )
        linker = (ROOT / "board" / "linker_scripts" / "link.ld").read_text(
            encoding="utf-8"
        )
        application = (ROOT / "applications" / "main.c").read_text(
            encoding="utf-8"
        )
        bridge = (ROOT / "platform" / "pal_engine_bridge.c").read_text(
            encoding="utf-8"
        )

        for setting in (
            "CONFIG_RT_USING_CHERRYUSB=y",
            "CONFIG_RT_CHERRYUSB_HOST=y",
            "CONFIG_RT_CHERRYUSB_HOST_DWC2_INFINEON=y",
            "CONFIG_RT_CHERRYUSB_HOST_HID=y",
        ):
            self.assertIn(setting, config)
        for define in (
            "#define RT_USING_CHERRYUSB",
            "#define RT_CHERRYUSB_HOST",
            "#define RT_CHERRYUSB_HOST_DWC2_INFINEON",
            "#define RT_CHERRYUSB_HOST_HID",
        ):
            self.assertIn(define, rtconfig)
        for unused in (
            "CONFIG_RT_CHERRYUSB_HOST_MSC=y",
            "CONFIG_RT_CHERRYUSB_HOST_VIDEO=y",
            "CONFIG_RT_CHERRYUSB_HOST_CDC_ACM=y",
        ):
            self.assertNotIn(unused, config)

        self.assertIn("CherryUSB-1.6.0/class/hid", platform_build)
        self.assertIn("Common/board/ports/usb", platform_build)
        self.assertIn("__usbh_class_info_start__", linker)
        self.assertIn("KEEP(*(.usbh_class_info))", linker)
        self.assertIn("__usbh_class_info_end__", linker)
        self.assertIn(".sdlpal_usb (NOLOAD)", linker)
        self.assertIn("LONG(__sdlpal_usb_start__)", linker)
        self.assertIn("pal_usb_keyboard_host_start()", application)
        self.assertIn('#include "pal_usb_keyboard_port.h"', application)
        self.assertNotIn("pal_usb_keyboard", bridge)
        self.assertIn("pal_touch_port_poll", bridge)
```

In `test_check_elf.py`, place USB storage immediately after audio storage in
the valid fixture:

```python
            "__sdlpal_usb_start__": 0x2606E0B8,
            "__sdlpal_usb_end__": 0x2606ECB8,
            "__HeapBase": 0x2606ECB8,
```

Replace the fixture's old `__HeapBase` value, then add:

```python
    def test_rejects_invalid_usb_sram_layout(self):
        self.symbols["__sdlpal_usb_end__"] = (
            self.symbols["__sdlpal_usb_start__"] + 4 * 1024 + 1
        )
        self.symbols["__HeapBase"] = self.symbols["__sdlpal_usb_end__"]
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("USB storage exceeds 4 KiB" in error for error in errors))

        self.symbols["__sdlpal_usb_start__"] = 0x2606D000
        self.symbols["__sdlpal_usb_end__"] = 0x2606DC00
        self.symbols["__HeapBase"] = 0x2606ECB8
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("USB storage overlaps audio" in error for error in errors))

        self.symbols["__sdlpal_usb_start__"] = 0x2606E0B8
        self.symbols["__sdlpal_usb_end__"] = 0x2606ECB8
        self.symbols["__HeapBase"] = 0x2606E800
        errors = check_elf.validate_layout(self.symbols, self.regions, 0)
        self.assertTrue(any("heap overlaps SDLPal USB" in error for error in errors))
```

Extend `test_parses_split_map_section_header()` with:

```text
.sdlpal_usb      0x2606e0b8    0xc00
```

and assert:

```python
        self.assertEqual(sections["sdlpal_usb"], (0x2606E0B8, 0xC00))
```

- [ ] **Step 2: Run the contract and verify RED**

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract.ProjectContractTest.test_usb_keyboard_host_phase1_contract -v
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_check_elf.ElfValidationTests.test_rejects_invalid_usb_sram_layout projects.Edgi_Talk_M55_SDLPAL.tests.host.test_check_elf.ElfValidationTests.test_parses_split_map_section_header -v
```

Expected: the project contract fails on the first missing CherryUSB setting;
the ELF tests fail because USB symbols and `.sdlpal_usb` parsing are not yet
part of the checker. Do not alter production integration before observing
both failures.

- [ ] **Step 3: Enable only the required CherryUSB target symbols**

Replace the single disabled CherryUSB line in `.config` with the complete
Host choice block. Keep every controller and class not selected explicitly
disabled; the significant enabled lines are:

```text
CONFIG_RT_USING_CHERRYUSB=y
# CONFIG_RT_CHERRYUSB_DEVICE is not set
CONFIG_RT_CHERRYUSB_HOST=y
CONFIG_RT_CHERRYUSB_HOST_DWC2_INFINEON=y
CONFIG_RT_CHERRYUSB_HOST_HID=y
CONFIG_CONFIG_USBHOST_SERIAL_RX_SIZE=2048
```

Mirror the checked-in M55 UVC project's generated ordering for all `# ... is
not set` controller/class entries, changing its class selection to HID only.
This avoids leaving an implicit Host-IP choice in `.config`.

Add the matching generated defines to `rtconfig.h`:

```c
#define RT_USING_CHERRYUSB
#define RT_CHERRYUSB_HOST
#define RT_CHERRYUSB_HOST_DWC2_INFINEON
#define RT_CHERRYUSB_HOST_HID
#define CONFIG_USBHOST_SERIAL_RX_SIZE 2048
```

Do not add `BSP_USING_USB`; the project selects the CherryUSB Host controller directly, matching the existing M55 UVC reference project. Do not enable any other class.

- [ ] **Step 4: Add platform include paths without changing CherryUSB**

Append these paths only when HID Host is enabled:

```python
if GetDepend(['RT_CHERRYUSB_HOST_HID']):
    path += [
        cwd + '/../../../libraries/Common/board/ports/usb',
        cwd + '/../../../libraries/components/CherryUSB-1.6.0/common',
        cwd + '/../../../libraries/components/CherryUSB-1.6.0/core',
        cwd + '/../../../libraries/components/CherryUSB-1.6.0/class/hid',
    ]
```

- [ ] **Step 5: Retain HID class metadata and zero USB worker storage**

Insert this block after the RT-Thread init records and before `__text_end`:

```ld
        . = ALIGN(4);
        __usbh_class_info_start__ = .;
        KEEP(*(.usbh_class_info))
        __usbh_class_info_end__ = .;
```

Add `.sdlpal_usb` beside the existing SDLPal static sections:

```ld
    .sdlpal_usb (NOLOAD) :
    {
        . = ALIGN(8);
        __sdlpal_usb_start__ = .;
        KEEP(*(.sdlpal_usb))
        KEEP(*(.sdlpal_usb.*))
        . = ALIGN(8);
        __sdlpal_usb_end__ = .;
    } > m55_data_secondary

    ASSERT(
      SIZEOF(.sdlpal_usb) >= 0x800,
      "SDLPal USB section must contain the static 2 KiB worker stack."
    )
    ASSERT(
      SIZEOF(.sdlpal_usb) <= 0x1000,
      "SDLPal USB section exceeds the 4 KiB Secondary SRAM budget."
    )
```

Add these entries to `.zero.table` after the audio entries:

```ld
        LONG(__sdlpal_usb_start__)
        LONG((__sdlpal_usb_end__ - __sdlpal_usb_start__)/4)
```

- [ ] **Step 6: Start Host diagnostics without gating the game**

Before changing startup, extend the ELF checker. Add:

```python
PAL_USB_MAX_BYTES = 4 * 1024
```

Include `sdlpal_usb` in both map-section regular expressions/lists. Add
`__sdlpal_usb_start__` and `__sdlpal_usb_end__` to `required`. In the
Secondary SRAM validation, require this exact ordering and budget:

```python
        usb_start = symbols["__sdlpal_usb_start__"]
        usb_end = symbols["__sdlpal_usb_end__"]
        if usb_start < origin or usb_end > origin + length:
            errors.append("SDLPal USB storage is outside Secondary SRAM")
        if usb_start < audio_end or usb_end < usb_start:
            errors.append("SDLPal USB storage overlaps audio storage")
        if usb_end - usb_start > PAL_USB_MAX_BYTES:
            errors.append("SDLPal USB storage exceeds 4 KiB")
        if symbols["__HeapBase"] < usb_end:
            errors.append("primary heap overlaps SDLPal USB storage")
```

In `main()`, require the `.sdlpal_usb` map section and reject a length over
`PAL_USB_MAX_BYTES`. Include `usb=<bytes>` in the PASS summary.

- [ ] **Step 7: Start Host diagnostics without gating the game**

Include `pal_usb_keyboard_port.h` in `applications/main.c`. Immediately after `pal_memory_init_allocators()`, call:

```c
    (void)pal_usb_keyboard_host_start();
```

Do not add the return value to `boot_initialize_io()` or any `pal_boot_state_t` transition.

- [ ] **Step 8: Run the contracts and target build and verify GREEN**

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract.ProjectContractTest.test_usb_keyboard_host_phase1_contract -v
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_check_elf -v
rtk powershell -NoProfile -Command "$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -C 'projects\Edgi_Talk_M55_SDLPAL' -j1"
```

Expected: the focused contract reports `OK`; SCons exits 0 and links `rt-thread.elf` without missing `__usbh_class_info_*`, HID hook, or DWC2 symbols.

- [ ] **Step 9: Commit target integration**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py projects/Edgi_Talk_M55_SDLPAL/tests/host/test_check_elf.py projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py projects/Edgi_Talk_M55_SDLPAL/.config projects/Edgi_Talk_M55_SDLPAL/rtconfig.h projects/Edgi_Talk_M55_SDLPAL/platform/SConscript projects/Edgi_Talk_M55_SDLPAL/board/linker_scripts/link.ld projects/Edgi_Talk_M55_SDLPAL/applications/main.c
rtk git commit -m "feat: enable SDLPal USB host keyboard diagnostics"
```

### Task 4: User Documentation and Full Software Verification

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/README.md`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`

**Interfaces:**
- Consumes: completed phase-1 firmware and existing test/build tooling.
- Produces: documented hardware scope, log format, acceptance procedure, and fresh software verification evidence.

- [ ] **Step 1: Add a failing documentation contract**

Extend `test_delivery_tools_and_documentation()` with exact phase-1 terms:

```python
        for item in (
            "USB HID Boot Keyboard",
            "[PAL USB] keyboard connected",
            "[PAL KEY] DOWN",
            "PAL_CONTROL_PGDN",
            "Twenty consecutive",
            "30-minute",
        ):
            self.assertIn(item, project_readme)
```

Run:

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract.ProjectContractTest.test_delivery_tools_and_documentation -v
```

Expected: failure because the README does not yet describe phase 1.

- [ ] **Step 2: Document phase-1 scope, mappings, and board checklist**

Add an ASCII/UTF-8 README section headed `## USB HID Boot Keyboard diagnostics`. State that touch remains the active game input in phase 1. Include the eight mappings, the exact log examples from the design, the unsupported NKRO/Report-ID/multi-keyboard scope, and these acceptance phrases exactly: `Twenty consecutive disconnect/reconnect cycles` and `30-minute stability run`.

- [ ] **Step 3: Run all Host C/C++ and Python tests**

```powershell
rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host clean all
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe -m unittest discover -s projects/Edgi_Talk_M55_SDLPAL/tests/host -p "test_*.py" -v
```

Expected: every Host executable prints `PASS`; Python reports zero failures and zero errors.

- [ ] **Step 4: Build ARM firmware and run ELF validation**

```powershell
rtk powershell -NoProfile -Command "$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -C 'projects\Edgi_Talk_M55_SDLPAL' -j16"
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py --elf projects/Edgi_Talk_M55_SDLPAL/rt-thread.elf --map projects/Edgi_Talk_M55_SDLPAL/rtthread.map --nm D:/workspace_work/env-windows/tools/gnu_gcc/arm_gcc/mingw/bin/arm-none-eabi-nm.exe --rotation 90
```

Expected: SCons exits 0; ELF validation prints `PASS rotation=90` and all memory budgets remain within limits.

- [ ] **Step 5: Confirm linked USB ownership and protected boundaries**

```powershell
rtk powershell -NoProfile -Command "& 'D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin\arm-none-eabi-nm.exe' 'projects\Edgi_Talk_M55_SDLPAL\rt-thread.elf' | Select-String '__usbh_class_info_start__|__usbh_class_info_end__|hid_class_driver|usbh_hid_run|pal_usb_keyboard_host_start'"
rtk git diff --quiet c2f8f182 -- projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream libraries/components/CherryUSB-1.6.0/core libraries/components/CherryUSB-1.6.0/class
rtk git diff --check
```

Expected: all five USB symbols appear; protected source comparison and whitespace check both exit 0.

- [ ] **Step 6: Run the existing four-rotation build matrix**

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File projects/Edgi_Talk_M55_SDLPAL/tools/build_matrix.ps1
```

Expected: `Rotation matrix passed`; the script restores the USB-enabled `.config` and `rtconfig.h` after all four builds.

- [ ] **Step 7: Commit documentation and contract coverage**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL/README.md projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py
rtk git commit -m "docs: document USB keyboard diagnostics acceptance"
```

### Task 5: Edgi-Talk Board Enumeration Acceptance

**Files:**
- Create after a successful run: `projects/Edgi_Talk_M55_SDLPAL/docs/validation/usb-keyboard-phase1-board.txt`

**Interfaces:**
- Consumes: `projects/Edgi_Talk_M55_SDLPAL/rtthread.hex`, the Edgi-Talk board's existing M33 boot chain, one standard wired Boot Keyboard, serial console, and the repository's approved flashing/probe workflow.
- Produces: timestamped serial evidence for enumeration, mappings, disconnect/reconnect, unsupported mouse handling, and stability.

- [ ] **Step 1: Flash the verified M55 artifact with the repository's probe workflow**

At execution time, invoke the `mklink-flash:mklink-ai-probe` skill, enumerate available probes/resources, and flash the exact artifact `projects/Edgi_Talk_M55_SDLPAL/rtthread.hex`. Preserve the existing Secure M33 and Non-secure M33 images required to start M55.

Expected: flash verification succeeds and the serial console reaches:

```text
SDLPal PSoC Edge start, resources=/sdcard/pal/
[PAL USB] host ready: bus=0 base=0x44900000
```

- [ ] **Step 2: Capture direct-connect enumeration and key transitions**

Start with no keyboard and confirm the game plus touch still work. Insert the keyboard and save the complete descriptor/class log through:

```text
[PAL USB] keyboard connected:
```

Press and release Up, Down, Left, Right, Enter, Escape, Page Up, Page Down, `A`, and Left Shift. Confirm one DOWN and one UP transition per physical edge, exact `PAL_CONTROL_*` annotations for the eight mapped keys, and `action=NONE` for `A` and Left Shift.

- [ ] **Step 3: Exercise combinations, disconnect, reconnect, and mouse ignore**

Hold two ordinary keys plus one modifier and confirm independent transitions without unchanged-report spam. Disconnect while one mapped key is held and confirm its UP line precedes the disconnect line. Reconnect and repeat one mapped key. Insert a mouse and confirm CherryUSB may enumerate it while the project logs it as ignored and SDLPal remains responsive.

- [ ] **Step 4: Complete cycling and stability acceptance**

Perform twenty direct keyboard disconnect/reconnect cycles. Then run the firmware for 30 minutes with normal touch gameplay and occasional keyboard diagnostic input. Reject the build if there is a crash, hang, stuck key state, continuous URB error output, or loss of touch behavior.

- [ ] **Step 5: Save evidence and commit only the text report**

The report must contain board/probe identity, firmware commit, keyboard VID/PID, timestamp, representative logs, cycle count, 30-minute result, and an explicit PASS/FAIL for every board criterion. Do not add binary captures or generated firmware artifacts.

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL/docs/validation/usb-keyboard-phase1-board.txt
rtk git commit -m "test: record USB keyboard board acceptance"
```

## Plan Self-Review

- Spec coverage: Tasks 1-3 implement every phase-1 code, configuration, memory, lifecycle, and no-game-input requirement; Tasks 4-5 cover documentation plus automated and physical verification.
- Protected boundaries: no task edits CherryUSB core/class files or SDLPal upstream files; Task 4 verifies this against `c2f8f182`.
- Type consistency: Task 2 consumes exactly Task 1's event/state/result names; Task 3 calls exactly Task 2's `pal_usb_keyboard_host_start()`.
- Configuration consistency: `.config`, `rtconfig.h`, SConscript include paths, `.usbh_class_info`, `.sdlpal_usb`, and startup invocation are changed together in Task 3.
- TDD order: each new behavior has a focused failing test before production implementation and a focused green run afterward.
