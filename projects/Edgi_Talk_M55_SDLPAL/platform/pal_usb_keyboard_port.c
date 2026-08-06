#include "pal_usb_keyboard_port.h"

#include "pal_usb_keyboard_core.h"

#include <board.h>
#include <rtthread.h>
#include <usb_config.h>
#include <usbh_core.h>
#include <usbh_hid.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PAL_USB_KEYBOARD_QUEUE_DEPTH 8u
#define PAL_USB_KEYBOARD_STACK_BYTES (2u * 1024u)
#define PAL_USB_KEYBOARD_THREAD_TICK 10u

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

static struct rt_thread worker_thread PAL_USB_WORKER_STORAGE;
static rt_uint8_t worker_stack[PAL_USB_KEYBOARD_STACK_BYTES]
    PAL_USB_WORKER_STORAGE;
static struct rt_messagequeue worker_queue PAL_USB_WORKER_STORAGE;
static rt_uint8_t worker_queue_pool[
    RT_MQ_BUF_SIZE(sizeof(pal_usb_message_t), PAL_USB_KEYBOARD_QUEUE_DEPTH)]
    PAL_USB_WORKER_STORAGE;

static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t report_buffer[
    PAL_USB_KEYBOARD_BOOT_REPORT_SIZE];

static struct usbh_hid *volatile active_hid;
static volatile uint32_t active_generation;
static volatile uint32_t invalid_length_count;
static volatile uint32_t queue_drop_count;
static volatile uint32_t keyboard_control_mask;
static bool worker_started;
static bool host_started;

static void keyboard_worker_entry(void *parameter);
static void keyboard_urb_complete(void *argument, int nbytes);

static void increment_counter(volatile uint32_t *counter)
{
    rt_base_t level = rt_hw_interrupt_disable();
    ++(*counter);
    rt_hw_interrupt_enable(level);
}

static bool active_snapshot(struct usbh_hid *hid_class,
                            uint32_t *generation)
{
    rt_base_t level;
    bool matches;

    level = rt_hw_interrupt_disable();
    matches = hid_class != NULL && active_hid == hid_class;
    if (matches && generation != NULL)
    {
        *generation = active_generation;
    }
    rt_hw_interrupt_enable(level);
    return matches;
}

static bool active_matches_generation(struct usbh_hid *hid_class,
                                      uint32_t generation)
{
    rt_base_t level;
    bool matches;

    level = rt_hw_interrupt_disable();
    matches = active_hid == hid_class && active_generation == generation;
    rt_hw_interrupt_enable(level);
    return matches;
}

static bool claim_keyboard(struct usbh_hid *hid_class, uint32_t *generation)
{
    rt_base_t level;
    bool claimed = false;

    level = rt_hw_interrupt_disable();
    if (active_hid == NULL)
    {
        ++active_generation;
        if (active_generation == 0u)
        {
            ++active_generation;
        }
        active_hid = hid_class;
        *generation = active_generation;
        claimed = true;
    }
    rt_hw_interrupt_enable(level);
    return claimed;
}

static bool release_keyboard(struct usbh_hid *hid_class,
                             uint32_t generation)
{
    rt_base_t level;
    bool released = false;

    level = rt_hw_interrupt_disable();
    if (active_hid == hid_class && active_generation == generation)
    {
        active_hid = NULL;
        released = true;
    }
    rt_hw_interrupt_enable(level);
    return released;
}

static void take_stats(uint32_t *invalid_lengths, uint32_t *queue_drops)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    *invalid_lengths = invalid_length_count;
    *queue_drops = queue_drop_count;
    invalid_length_count = 0u;
    queue_drop_count = 0u;
    rt_hw_interrupt_enable(level);
}

static bool queue_message(const pal_usb_message_t *message)
{
    if (rt_mq_send(&worker_queue, message, sizeof(*message)) != RT_EOK)
    {
        increment_counter(&queue_drop_count);
        return false;
    }
    return true;
}

static void queue_transfer_error(uint32_t generation, int status)
{
    pal_usb_message_t message;

    memset(&message, 0, sizeof(message));
    message.type = PAL_USB_MESSAGE_ERROR;
    message.generation = generation;
    message.status = status;
    (void)queue_message(&message);
}

static int submit_keyboard_urb(struct usbh_hid *hid_class,
                               uint32_t generation)
{
    if (!active_matches_generation(hid_class, generation))
    {
        return 0;
    }

    usbh_int_urb_fill(&hid_class->intin_urb, hid_class->hport,
                      hid_class->intin, report_buffer,
                      PAL_USB_KEYBOARD_BOOT_REPORT_SIZE, 0u,
                      keyboard_urb_complete, hid_class);
    return usbh_submit_urb(&hid_class->intin_urb);
}

static bool is_boot_keyboard(const struct usbh_hid *hid_class)
{
    const struct usb_interface_descriptor *interface_descriptor;

    if (hid_class == NULL || hid_class->hport == NULL ||
        hid_class->intin == NULL)
    {
        return false;
    }

    interface_descriptor =
        &hid_class->hport->config.intf[hid_class->intf]
             .altsetting[0]
             .intf_desc;
    return interface_descriptor->bInterfaceSubClass == HID_SUBCLASS_BOOTIF &&
           hid_class->protocol == HID_PROTOCOL_KEYBOARD &&
           USB_GET_MAXPACKETSIZE(hid_class->intin->wMaxPacketSize) >=
               PAL_USB_KEYBOARD_BOOT_REPORT_SIZE;
}

static void handle_key_event(void *context,
                             const pal_usb_keyboard_event_t *event)
{
    uint32_t control;
    rt_base_t level;

    (void)context;
    control = pal_usb_keyboard_control(event->usage);
    if (control != 0u)
    {
        level = rt_hw_interrupt_disable();
        if (event->pressed)
        {
            keyboard_control_mask |= control;
        }
        else
        {
            keyboard_control_mask &= ~control;
        }
        rt_hw_interrupt_enable(level);
    }

    rt_kprintf("[PAL KEY] %s usage=0x%02x key=%s action=%s\n",
               event->pressed ? "DOWN" : "UP  ",
               (unsigned int)event->usage,
               pal_usb_keyboard_usage_name(event->usage),
               pal_usb_keyboard_control_name(control));
}

uint32_t pal_usb_keyboard_controls_get(void)
{
    uint32_t controls;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    controls = keyboard_control_mask;
    rt_hw_interrupt_enable(level);
    return controls;
}

static void log_pending_stats(void)
{
    uint32_t invalid_lengths;
    uint32_t queue_drops;

    take_stats(&invalid_lengths, &queue_drops);
    if (invalid_lengths != 0u || queue_drops != 0u)
    {
        rt_kprintf("[PAL USB] input stats: invalid=%lu dropped=%lu\n",
                   (unsigned long)invalid_lengths,
                   (unsigned long)queue_drops);
    }
}

static void keyboard_worker_entry(void *parameter)
{
    pal_usb_keyboard_state_t state;
    uint32_t generation = 0u;
    bool rollover_active = false;

    (void)parameter;
    pal_usb_keyboard_state_init(&state);

    for (;;)
    {
        pal_usb_message_t message;
        pal_usb_keyboard_report_result_t result;
        rt_ssize_t received;

        received = rt_mq_recv(&worker_queue, &message, sizeof(message),
                              RT_WAITING_FOREVER);
        if (received != (rt_ssize_t)sizeof(message))
        {
            continue;
        }

        log_pending_stats();
        if (generation != message.generation)
        {
            pal_usb_keyboard_release_all(&state, handle_key_event, NULL);
            pal_usb_keyboard_state_init(&state);
            generation = message.generation;
            rollover_active = false;
        }

        switch (message.type)
        {
        case PAL_USB_MESSAGE_REPORT:
            result = pal_usb_keyboard_process(
                &state, message.report, sizeof(message.report),
                handle_key_event, NULL);
            if (result == PAL_USB_KEYBOARD_REPORT_ROLLOVER)
            {
                if (!rollover_active)
                {
                    rt_kprintf("[PAL USB] keyboard rollover\n");
                }
                rollover_active = true;
            }
            else
            {
                rollover_active = false;
            }
            break;
        case PAL_USB_MESSAGE_DISCONNECT:
            pal_usb_keyboard_release_all(&state, handle_key_event, NULL);
            pal_usb_keyboard_state_init(&state);
            generation = 0u;
            rollover_active = false;
            rt_kprintf("[PAL USB] keyboard disconnected\n");
            break;
        case PAL_USB_MESSAGE_ERROR:
            pal_usb_keyboard_release_all(&state, handle_key_event, NULL);
            pal_usb_keyboard_state_init(&state);
            rollover_active = false;
            rt_kprintf("[PAL USB] transfer error: %d\n", message.status);
            break;
        case PAL_USB_MESSAGE_STATS:
        default:
            break;
        }
    }
}

static void keyboard_urb_complete(void *argument, int nbytes)
{
    struct usbh_hid *hid_class = (struct usbh_hid *)argument;
    pal_usb_message_t message;
    uint32_t generation;
    int result;

    if (!active_snapshot(hid_class, &generation))
    {
        return;
    }

    memset(&message, 0, sizeof(message));
    message.generation = generation;
    if (nbytes == (int)PAL_USB_KEYBOARD_BOOT_REPORT_SIZE)
    {
        message.type = PAL_USB_MESSAGE_REPORT;
        memcpy(message.report, report_buffer, sizeof(message.report));
        if (active_matches_generation(hid_class, generation))
        {
            (void)queue_message(&message);
        }
    }
    else if (nbytes >= 0)
    {
        message.type = PAL_USB_MESSAGE_STATS;
        increment_counter(&invalid_length_count);
        if (active_matches_generation(hid_class, generation))
        {
            (void)queue_message(&message);
        }
    }
    else if (nbytes != -USB_ERR_NAK)
    {
        queue_transfer_error(generation, nbytes);
        return;
    }

    result = submit_keyboard_urb(hid_class, generation);
    if (result < 0 && active_matches_generation(hid_class, generation))
    {
        queue_transfer_error(generation, result);
    }
}

bool pal_usb_keyboard_host_start(void)
{
    rt_err_t result;
    int host_result;

    if (host_started)
    {
        return true;
    }

    if (!worker_started)
    {
        result = rt_mq_init(&worker_queue, "pal_usb", worker_queue_pool,
                            sizeof(pal_usb_message_t),
                            sizeof(worker_queue_pool), RT_IPC_FLAG_FIFO);
        if (result != RT_EOK)
        {
            rt_kprintf("[PAL USB] worker init failed: %d\n", result);
            return false;
        }

        result = rt_thread_init(
            &worker_thread, "pal_usb", keyboard_worker_entry, NULL,
            worker_stack, sizeof(worker_stack),
            (rt_uint8_t)(CONFIG_USBHOST_PSC_PRIO + 1u),
            PAL_USB_KEYBOARD_THREAD_TICK);
        if (result != RT_EOK)
        {
            (void)rt_mq_detach(&worker_queue);
            rt_kprintf("[PAL USB] worker init failed: %d\n", result);
            return false;
        }

        result = rt_thread_startup(&worker_thread);
        if (result != RT_EOK)
        {
            (void)rt_thread_detach(&worker_thread);
            (void)rt_mq_detach(&worker_queue);
            rt_kprintf("[PAL USB] worker init failed: %d\n", result);
            return false;
        }
        worker_started = true;
    }

    host_result = usbh_initialize(0u, USBHS_BASE, NULL);
    if (host_result < 0)
    {
        rt_kprintf("[PAL USB] host init failed: %d\n", host_result);
        return false;
    }

    host_started = true;
    rt_kprintf("[PAL USB] host ready: bus=0 base=0x%08lx\n",
               (unsigned long)USBHS_BASE);
    return true;
}

void usbh_hid_run(struct usbh_hid *hid_class)
{
    uint32_t generation;
    uint16_t max_packet_size = 0u;
    int result;

    if (hid_class != NULL && hid_class->intin != NULL)
    {
        max_packet_size =
            USB_GET_MAXPACKETSIZE(hid_class->intin->wMaxPacketSize);
    }
    if (!is_boot_keyboard(hid_class))
    {
        rt_kprintf("[PAL USB] HID ignored: subclass/protocol unsupported, "
                   "protocol=%u mps=%u\n",
                   hid_class != NULL
                       ? (unsigned int)hid_class->protocol
                       : 0u,
                   (unsigned int)max_packet_size);
        return;
    }
    if (!claim_keyboard(hid_class, &generation))
    {
        rt_kprintf("[PAL USB] keyboard ignored: another keyboard is active\n");
        return;
    }

    result = usbh_hid_set_protocol(hid_class, HID_PROTOCOL_BOOT);
    if (result < 0)
    {
        (void)release_keyboard(hid_class, generation);
        rt_kprintf("[PAL USB] set boot protocol failed: %d\n", result);
        return;
    }

    memset(report_buffer, 0, sizeof(report_buffer));
    result = submit_keyboard_urb(hid_class, generation);
    if (result < 0)
    {
        (void)release_keyboard(hid_class, generation);
        rt_kprintf("[PAL USB] keyboard submit failed: %d\n", result);
        return;
    }

    rt_kprintf("[PAL USB] keyboard connected: vid=0x%04x pid=0x%04x "
               "ep=0x%02x mps=%u\n",
               (unsigned int)hid_class->hport->device_desc.idVendor,
               (unsigned int)hid_class->hport->device_desc.idProduct,
               (unsigned int)hid_class->intin->bEndpointAddress,
               (unsigned int)max_packet_size);
}

void usbh_hid_stop(struct usbh_hid *hid_class)
{
    pal_usb_message_t message;
    uint32_t generation;

    if (!active_snapshot(hid_class, &generation) ||
        !release_keyboard(hid_class, generation))
    {
        return;
    }

    (void)rt_mq_control(&worker_queue, RT_IPC_CMD_RESET, NULL);
    memset(&message, 0, sizeof(message));
    message.type = PAL_USB_MESSAGE_DISCONNECT;
    message.generation = generation;
    (void)queue_message(&message);
}
