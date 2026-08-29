#include "retro_go_input.h"

#include "retro_go_platform.h"

#include <board.h>
#include <cy_sysint.h>
#include <rtthread.h>
#include <usb_config.h>
#include <usbh_core.h>
#include <usbh_hid.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RETRO_GO_BOOT_REPORT_SIZE 8u
#define RETRO_GO_INPUT_QUEUE_DEPTH 16u
#define RETRO_GO_INPUT_STACK_BYTES (2u * 1024u)

#ifndef BSP_RETRO_GO_INPUT_THREAD_PRIORITY
#define BSP_RETRO_GO_INPUT_THREAD_PRIORITY 24
#endif

#if defined(FINSH_THREAD_PRIORITY) && \
    (CONFIG_USBHOST_PSC_PRIO <= FINSH_THREAD_PRIORITY || \
     BSP_RETRO_GO_INPUT_THREAD_PRIORITY <= FINSH_THREAD_PRIORITY)
#error "Retro-Go USB priority values must be greater than MSH"
#endif

#define HID_USAGE_A 0x04u
#define HID_USAGE_D 0x07u
#define HID_USAGE_E 0x08u
#define HID_USAGE_J 0x0du
#define HID_USAGE_K 0x0eu
#define HID_USAGE_Q 0x14u
#define HID_USAGE_R 0x15u
#define HID_USAGE_S 0x16u
#define HID_USAGE_W 0x1au
#define HID_USAGE_X 0x1bu
#define HID_USAGE_Z 0x1du
#define HID_USAGE_ENTER 0x28u
#define HID_USAGE_ESCAPE 0x29u
#define HID_USAGE_BACKSPACE 0x2au
#define HID_USAGE_SPACE 0x2cu
#define HID_USAGE_F5 0x3eu
#define HID_USAGE_F9 0x42u
#define HID_USAGE_RIGHT 0x4fu
#define HID_USAGE_LEFT 0x50u
#define HID_USAGE_DOWN 0x51u
#define HID_USAGE_UP 0x52u
#define HID_MOD_RIGHT_SHIFT (1u << 5)

typedef enum retro_go_usb_message_type
{
    RETRO_GO_USB_REPORT,
    RETRO_GO_USB_DISCONNECT,
    RETRO_GO_USB_ERROR,
} retro_go_usb_message_type_t;

typedef struct retro_go_usb_message
{
    retro_go_usb_message_type_t type;
    uint32_t generation;
    int status;
    uint8_t report[RETRO_GO_BOOT_REPORT_SIZE];
} retro_go_usb_message_t;

static struct rt_thread input_thread RETRO_GO_SOCMEM;
static rt_uint8_t input_stack[RETRO_GO_INPUT_STACK_BYTES] RETRO_GO_SOCMEM;
static struct rt_messagequeue input_queue RETRO_GO_SOCMEM;
static rt_uint8_t input_queue_pool[
    RT_MQ_BUF_SIZE(sizeof(retro_go_usb_message_t),
                   RETRO_GO_INPUT_QUEUE_DEPTH)] RETRO_GO_SOCMEM;
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX
    uint8_t report_buffer[RETRO_GO_BOOT_REPORT_SIZE];

static struct usbh_hid *volatile active_hid;
static volatile uint32_t active_generation;
static volatile uint32_t button_mask;
static volatile uint32_t event_mask;
static volatile bool fast_forward;
static volatile bool escape_held;
static volatile uint32_t input_report_count;
static volatile uint32_t input_queue_drop_count;
static bool worker_started;
static bool host_started;

static void input_worker(void *parameter);
static void keyboard_urb_complete(void *argument, int nbytes);

/*
 * The Infineon CherryUSB glue calls USBH_IRQHandler() directly from the
 * physical IRQ without marking RT-Thread interrupt context.  HID completion
 * callbacks use RT-Thread IPC, so install a platform-owned wrapper after the
 * host controller has initialized instead of modifying the vendor glue.
 */
static void retro_go_usb_host_irq_wrapper(void)
{
    rt_interrupt_enter();
    USBH_IRQHandler(0u);
    rt_interrupt_leave();
}

static void install_usb_host_irq_wrapper(void)
{
    rt_base_t level = rt_hw_interrupt_disable();

    (void)Cy_SysInt_SetVector(usbhs_interrupt_usbhsctrl_IRQn,
                              retro_go_usb_host_irq_wrapper);
    rt_hw_interrupt_enable(level);
}

static bool report_contains(const uint8_t *report, uint8_t usage)
{
    size_t index;

    for (index = 2u; index < RETRO_GO_BOOT_REPORT_SIZE; ++index)
    {
        if (report[index] == usage)
        {
            return true;
        }
    }
    return false;
}

static bool report_has_rollover(const uint8_t *report)
{
    size_t index;

    for (index = 2u; index < RETRO_GO_BOOT_REPORT_SIZE; ++index)
    {
        if (report[index] >= 0x01u && report[index] <= 0x03u)
        {
            return true;
        }
    }
    return false;
}

static uint32_t report_buttons(const uint8_t *report)
{
    uint32_t buttons = 0u;

    if (report_contains(report, HID_USAGE_RIGHT) ||
        report_contains(report, HID_USAGE_D))
        buttons |= RETRO_GO_BUTTON_RIGHT;
    if (report_contains(report, HID_USAGE_LEFT) ||
        report_contains(report, HID_USAGE_A))
        buttons |= RETRO_GO_BUTTON_LEFT;
    if (report_contains(report, HID_USAGE_UP) ||
        report_contains(report, HID_USAGE_W))
        buttons |= RETRO_GO_BUTTON_UP;
    if (report_contains(report, HID_USAGE_DOWN) ||
        report_contains(report, HID_USAGE_S))
        buttons |= RETRO_GO_BUTTON_DOWN;
    if (report_contains(report, HID_USAGE_Z) ||
        report_contains(report, HID_USAGE_J))
        buttons |= RETRO_GO_BUTTON_A;
    if (report_contains(report, HID_USAGE_X) ||
        report_contains(report, HID_USAGE_K))
        buttons |= RETRO_GO_BUTTON_B;
    if (report_contains(report, HID_USAGE_ENTER))
        buttons |= RETRO_GO_BUTTON_START;
    if (report_contains(report, HID_USAGE_BACKSPACE) ||
        (report[0] & HID_MOD_RIGHT_SHIFT) != 0u)
        buttons |= RETRO_GO_BUTTON_SELECT;
    if (report_contains(report, HID_USAGE_Q))
        buttons |= RETRO_GO_BUTTON_L;
    if (report_contains(report, HID_USAGE_E))
        buttons |= RETRO_GO_BUTTON_R;
    return buttons;
}

static uint32_t report_pressed_events(const uint8_t *previous,
                                      const uint8_t *current)
{
    uint32_t events = 0u;

    if (!report_contains(previous, HID_USAGE_F5) &&
        report_contains(current, HID_USAGE_F5))
        events |= RETRO_GO_EVENT_SAVE;
    if (!report_contains(previous, HID_USAGE_F9) &&
        report_contains(current, HID_USAGE_F9))
        events |= RETRO_GO_EVENT_LOAD;
    if (!report_contains(previous, HID_USAGE_R) &&
        report_contains(current, HID_USAGE_R))
        events |= RETRO_GO_EVENT_RESET;
    if (!report_contains(previous, HID_USAGE_ESCAPE) &&
        report_contains(current, HID_USAGE_ESCAPE))
        events |= RETRO_GO_EVENT_QUIT;
    return events;
}

static void publish_report(const uint8_t *previous, const uint8_t *current)
{
    rt_base_t level = rt_hw_interrupt_disable();

    button_mask = report_buttons(current);
    event_mask |= report_pressed_events(previous, current);
    fast_forward = report_contains(current, HID_USAGE_SPACE);
    escape_held = report_contains(current, HID_USAGE_ESCAPE);
    rt_hw_interrupt_enable(level);
}

static bool active_snapshot(struct usbh_hid *hid, uint32_t *generation)
{
    rt_base_t level = rt_hw_interrupt_disable();
    bool matches = hid != NULL && active_hid == hid;

    if (matches && generation != NULL)
    {
        *generation = active_generation;
    }
    rt_hw_interrupt_enable(level);
    return matches;
}

static bool active_matches(struct usbh_hid *hid, uint32_t generation)
{
    rt_base_t level = rt_hw_interrupt_disable();
    bool matches = active_hid == hid && active_generation == generation;

    rt_hw_interrupt_enable(level);
    return matches;
}

static bool claim_keyboard(struct usbh_hid *hid, uint32_t *generation)
{
    rt_base_t level = rt_hw_interrupt_disable();
    bool claimed = false;

    if (active_hid == NULL)
    {
        ++active_generation;
        if (active_generation == 0u)
        {
            ++active_generation;
        }
        active_hid = hid;
        *generation = active_generation;
        claimed = true;
    }
    rt_hw_interrupt_enable(level);
    return claimed;
}

static bool release_keyboard(struct usbh_hid *hid, uint32_t generation)
{
    rt_base_t level = rt_hw_interrupt_disable();
    bool released = false;

    if (active_hid == hid && active_generation == generation)
    {
        active_hid = NULL;
        released = true;
    }
    rt_hw_interrupt_enable(level);
    return released;
}

static int submit_keyboard_urb(struct usbh_hid *hid, uint32_t generation)
{
    if (!active_matches(hid, generation))
    {
        return 0;
    }
    usbh_int_urb_fill(&hid->intin_urb, hid->hport, hid->intin,
                      report_buffer, RETRO_GO_BOOT_REPORT_SIZE, 0u,
                      keyboard_urb_complete, hid);
    return usbh_submit_urb(&hid->intin_urb);
}

static bool is_boot_keyboard(const struct usbh_hid *hid)
{
    const struct usb_interface_descriptor *descriptor;

    if (hid == NULL || hid->hport == NULL || hid->intin == NULL)
    {
        return false;
    }
    descriptor = &hid->hport->config.intf[hid->intf]
                      .altsetting[0].intf_desc;
    return descriptor->bInterfaceSubClass == HID_SUBCLASS_BOOTIF &&
           hid->protocol == HID_PROTOCOL_KEYBOARD &&
           USB_GET_MAXPACKETSIZE(hid->intin->wMaxPacketSize) >=
               RETRO_GO_BOOT_REPORT_SIZE;
}

static bool queue_message(const retro_go_usb_message_t *message)
{
    rt_base_t level;

    if (rt_mq_send(&input_queue, message, sizeof(*message)) == RT_EOK)
    {
        return true;
    }

    level = rt_hw_interrupt_disable();
    ++input_queue_drop_count;
    rt_hw_interrupt_enable(level);
    return false;
}

static void input_worker(void *parameter)
{
    uint8_t previous[RETRO_GO_BOOT_REPORT_SIZE] = {0u};
    uint32_t generation = 0u;
    uint32_t reported_drop_count = 0u;

    (void)parameter;
    for (;;)
    {
        retro_go_usb_message_t message;

        if (rt_mq_recv(&input_queue, &message, sizeof(message),
                       RT_WAITING_FOREVER) != (rt_ssize_t)sizeof(message))
        {
            continue;
        }
        if (generation != message.generation)
        {
            memset(previous, 0, sizeof(previous));
            generation = message.generation;
        }
        if (message.type == RETRO_GO_USB_REPORT)
        {
            if (!report_has_rollover(message.report))
            {
                publish_report(previous, message.report);
                memcpy(previous, message.report, sizeof(previous));
            }
            ++input_report_count;
        }
        else
        {
            uint8_t released[RETRO_GO_BOOT_REPORT_SIZE] = {0u};

            publish_report(previous, released);
            memset(previous, 0, sizeof(previous));
            if (message.type == RETRO_GO_USB_ERROR)
            {
                rt_kprintf("[retro-go] USB keyboard error: %d\n",
                           message.status);
            }
            else
            {
                rt_kprintf("[retro-go] USB keyboard disconnected\n");
            }
        }

        if (reported_drop_count != input_queue_drop_count)
        {
            reported_drop_count = input_queue_drop_count;
            rt_kprintf("[retro-go] USB HID queue overflow: "
                       "reports=%u drops=%u\n",
                       (unsigned)input_report_count,
                       (unsigned)reported_drop_count);
        }
    }
}

static void keyboard_urb_complete(void *argument, int nbytes)
{
    struct usbh_hid *hid = (struct usbh_hid *)argument;
    retro_go_usb_message_t message;
    uint32_t generation;
    int result;

    if (!active_snapshot(hid, &generation))
    {
        return;
    }
    memset(&message, 0, sizeof(message));
    message.generation = generation;
    if (nbytes == (int)RETRO_GO_BOOT_REPORT_SIZE)
    {
        message.type = RETRO_GO_USB_REPORT;
        memcpy(message.report, report_buffer, sizeof(message.report));
        (void)queue_message(&message);
    }
    else if (nbytes < 0 && nbytes != -USB_ERR_NAK)
    {
        message.type = RETRO_GO_USB_ERROR;
        message.status = nbytes;
        (void)queue_message(&message);
        return;
    }

    result = submit_keyboard_urb(hid, generation);
    if (result < 0 && active_matches(hid, generation))
    {
        memset(&message, 0, sizeof(message));
        message.type = RETRO_GO_USB_ERROR;
        message.generation = generation;
        message.status = result;
        (void)queue_message(&message);
    }
}

uint32_t retro_go_input_buttons_get(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uint32_t result = button_mask;

    rt_hw_interrupt_enable(level);
    return result;
}

uint32_t retro_go_input_events_take(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uint32_t result = event_mask;

    event_mask = 0u;
    rt_hw_interrupt_enable(level);
    return result;
}

bool retro_go_input_fast_forward_get(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    bool result = fast_forward;

    rt_hw_interrupt_enable(level);
    return result;
}

void retro_go_input_session_barrier(uint32_t timeout_ms)
{
    uint32_t started_ms = rt_tick_get_millisecond();

    for (;;)
    {
        rt_base_t level = rt_hw_interrupt_disable();
        bool held = escape_held;

        /* Events belong to the session in which their key-down edge was
         * observed. Never let save/load/reset/quit edges cross into the
         * launcher or the next emulator session. */
        event_mask = 0u;
        rt_hw_interrupt_enable(level);
        if (!held)
        {
            /* One HID poll of quiet time closes the race where the key-up
             * report and a queued/bounced key-down straddle the session
             * boundary. */
            rt_thread_mdelay(20u);
            level = rt_hw_interrupt_disable();
            held = escape_held;
            event_mask = 0u;
            rt_hw_interrupt_enable(level);
            if (!held)
            {
                return;
            }
        }
        if ((uint32_t)(rt_tick_get_millisecond() - started_ms) >= timeout_ms)
        {
            rt_kprintf("[retro-go] input release barrier timed out; "
                       "stale events discarded\n");
            return;
        }
        rt_thread_mdelay(10u);
    }
}

bool retro_go_input_init(void)
{
    rt_err_t result;
    int host_result;

    if (!worker_started)
    {
        result = rt_mq_init(&input_queue, "rg_usb", input_queue_pool,
                            sizeof(retro_go_usb_message_t),
                            sizeof(input_queue_pool), RT_IPC_FLAG_FIFO);
        if (result != RT_EOK)
        {
            return false;
        }
        result = rt_thread_init(&input_thread, "rg_usb", input_worker,
                                NULL, input_stack, sizeof(input_stack),
                                BSP_RETRO_GO_INPUT_THREAD_PRIORITY,
                                10u);
        if (result != RT_EOK)
        {
            (void)rt_mq_detach(&input_queue);
            return false;
        }
        result = rt_thread_startup(&input_thread);
        if (result != RT_EOK)
        {
            (void)rt_thread_detach(&input_thread);
            (void)rt_mq_detach(&input_queue);
            return false;
        }
        worker_started = true;
    }
    if (host_started)
    {
        return true;
    }
    host_result = usbh_initialize(0u, USBHS_BASE, NULL);
    if (host_result < 0)
    {
        rt_kprintf("[retro-go] USB host init failed: %d\n", host_result);
        return false;
    }
    install_usb_host_irq_wrapper();
    host_started = true;
    rt_kprintf("[retro-go] USB host keyboard ready, "
               "RT-Thread IRQ wrapper active, queue=%u\n",
               RETRO_GO_INPUT_QUEUE_DEPTH);
    return true;
}

void usbh_hid_run(struct usbh_hid *hid)
{
    uint32_t generation;
    int result;

    if (!is_boot_keyboard(hid))
    {
        rt_kprintf("[retro-go] non-boot HID ignored\n");
        return;
    }
    if (!claim_keyboard(hid, &generation))
    {
        rt_kprintf("[retro-go] another keyboard is already active\n");
        return;
    }
    result = usbh_hid_set_protocol(hid, HID_PROTOCOL_BOOT);
    if (result < 0)
    {
        (void)release_keyboard(hid, generation);
        rt_kprintf("[retro-go] set keyboard boot protocol failed: %d\n",
                   result);
        return;
    }
    memset(report_buffer, 0, sizeof(report_buffer));
    result = submit_keyboard_urb(hid, generation);
    if (result < 0)
    {
        (void)release_keyboard(hid, generation);
        rt_kprintf("[retro-go] keyboard submit failed: %d\n", result);
        return;
    }
    rt_kprintf("[retro-go] keyboard connected: vid=%04x pid=%04x\n",
               hid->hport->device_desc.idVendor,
               hid->hport->device_desc.idProduct);
}

void usbh_hid_stop(struct usbh_hid *hid)
{
    retro_go_usb_message_t message;
    uint32_t generation;

    if (!active_snapshot(hid, &generation) ||
        !release_keyboard(hid, generation))
    {
        return;
    }
    (void)rt_mq_control(&input_queue, RT_IPC_CMD_RESET, NULL);
    memset(&message, 0, sizeof(message));
    message.type = RETRO_GO_USB_DISCONNECT;
    message.generation = generation;
    (void)queue_message(&message);
}
