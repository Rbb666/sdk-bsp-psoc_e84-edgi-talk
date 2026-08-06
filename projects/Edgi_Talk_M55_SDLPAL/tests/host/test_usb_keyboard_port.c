#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "pal_usb_keyboard_core.h"
#include "pal_usb_keyboard_port.h"
#include "rtthread.h"
#include "usb_config.h"
#include "usbh_hid.h"

enum test_message_type
{
    TEST_MESSAGE_REPORT,
    TEST_MESSAGE_DISCONNECT,
    TEST_MESSAGE_ERROR,
    TEST_MESSAGE_STATS
};

typedef struct test_message
{
    int type;
    uint32_t generation;
    int status;
    uint8_t report[PAL_USB_KEYBOARD_BOOT_REPORT_SIZE];
} test_message_t;

typedef struct test_hid
{
    struct usbh_hubport hport;
    struct usb_endpoint_descriptor endpoint;
    struct usbh_hid hid;
} test_hid_t;

static unsigned host_initialize_calls;
static uintptr_t last_host_base;
static unsigned set_protocol_calls;
static uint8_t last_protocol;
static unsigned submit_calls;
static struct usbh_urb *last_urb;
static unsigned message_send_calls;
static unsigned message_queue_reset_calls;
static unsigned message_queue_init_calls;
static unsigned thread_init_calls;
static unsigned thread_startup_calls;
static void (*worker_entry)(void *);
static void *worker_parameter;
static test_message_t sent_messages[16];
static size_t sent_count;
static size_t replay_index;
static bool replay_messages;
static const uint32_t *replay_expected_masks;
static size_t replay_expected_count;
static jmp_buf worker_exit;
static char log_output[8192];
static size_t log_length;

static void setup_hid(test_hid_t *fixture, uint8_t subclass,
                      uint8_t protocol, uint16_t max_packet_size)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->hport.device_desc.idVendor = 0x1234u;
    fixture->hport.device_desc.idProduct = 0x5678u;
    fixture->hport.config.intf[0].altsetting[0].intf_desc
        .bInterfaceSubClass = subclass;
    fixture->endpoint.bEndpointAddress = 0x81u;
    fixture->endpoint.wMaxPacketSize = max_packet_size;
    fixture->hid.hport = &fixture->hport;
    fixture->hid.intin = &fixture->endpoint;
    fixture->hid.protocol = protocol;
    fixture->hid.intf = 0u;
}

rt_err_t rt_thread_init(struct rt_thread *thread, const char *name,
                        void (*entry)(void *), void *parameter,
                        void *stack_start, rt_uint32_t stack_size,
                        rt_uint8_t priority, rt_uint32_t tick)
{
    (void)thread;
    (void)stack_start;
    assert(strcmp(name, "pal_usb") == 0);
    assert(stack_size == 2u * 1024u);
    assert(priority == CONFIG_USBHOST_PSC_PRIO + 1u);
    assert(tick > 0u);
    ++thread_init_calls;
    worker_entry = entry;
    worker_parameter = parameter;
    return RT_EOK;
}

rt_err_t rt_thread_detach(rt_thread_t thread)
{
    (void)thread;
    return RT_EOK;
}

rt_err_t rt_thread_startup(rt_thread_t thread)
{
    (void)thread;
    ++thread_startup_calls;
    return RT_EOK;
}

rt_err_t rt_mq_init(rt_mq_t mq, const char *name, void *msgpool,
                    rt_size_t msg_size, rt_size_t pool_size,
                    rt_uint8_t flag)
{
    (void)mq;
    assert(strcmp(name, "pal_usb") == 0);
    assert(msgpool != NULL);
    assert(msg_size == sizeof(test_message_t));
    assert(pool_size >= msg_size * 8u);
    assert(flag == RT_IPC_FLAG_FIFO);
    ++message_queue_init_calls;
    return RT_EOK;
}

rt_err_t rt_mq_detach(rt_mq_t mq)
{
    (void)mq;
    return RT_EOK;
}

rt_err_t rt_mq_send(rt_mq_t mq, const void *buffer, rt_size_t size)
{
    (void)mq;
    assert(buffer != NULL);
    assert(size == sizeof(test_message_t));
    assert(sent_count < sizeof(sent_messages) / sizeof(sent_messages[0]));
    memcpy(&sent_messages[sent_count++], buffer, sizeof(test_message_t));
    ++message_send_calls;
    return RT_EOK;
}

rt_ssize_t rt_mq_recv(rt_mq_t mq, void *buffer, rt_size_t size,
                      int timeout)
{
    (void)mq;
    assert(replay_messages);
    assert(size == sizeof(test_message_t));
    assert(timeout == RT_WAITING_FOREVER);
    if (replay_expected_masks != NULL)
    {
        assert(replay_index < replay_expected_count);
        assert(pal_usb_keyboard_controls_get() ==
               replay_expected_masks[replay_index]);
    }
    if (replay_index >= sent_count)
    {
        longjmp(worker_exit, 1);
    }
    memcpy(buffer, &sent_messages[replay_index++], sizeof(test_message_t));
    return (rt_ssize_t)sizeof(test_message_t);
}

rt_err_t rt_mq_control(rt_mq_t mq, int command, void *argument)
{
    (void)mq;
    (void)argument;
    assert(command == RT_IPC_CMD_RESET);
    ++message_queue_reset_calls;
    return RT_EOK;
}

rt_base_t rt_hw_interrupt_disable(void)
{
    return 0u;
}

void rt_hw_interrupt_enable(rt_base_t level)
{
    assert(level == 0u);
}

int rt_kprintf(const char *format, ...)
{
    va_list arguments;
    int written;
    size_t available = sizeof(log_output) - log_length;

    va_start(arguments, format);
    written = vsnprintf(log_output + log_length, available, format, arguments);
    va_end(arguments);
    assert(written >= 0);
    assert((size_t)written < available);
    log_length += (size_t)written;
    return written;
}

int usbh_initialize(uint8_t busid, uintptr_t reg_base, void *event_handler)
{
    assert(busid == 0u);
    assert(event_handler == NULL);
    ++host_initialize_calls;
    last_host_base = reg_base;
    return 0;
}

int usbh_hid_set_protocol(struct usbh_hid *hid_class, uint8_t protocol)
{
    assert(hid_class != NULL);
    ++set_protocol_calls;
    last_protocol = protocol;
    return 0;
}

int usbh_submit_urb(struct usbh_urb *urb)
{
    assert(urb != NULL);
    ++submit_calls;
    last_urb = urb;
    return 0;
}

static void verify_ignored_hid(test_hid_t *fixture)
{
    unsigned protocols_before = set_protocol_calls;
    unsigned submits_before = submit_calls;

    usbh_hid_run(&fixture->hid);
    assert(set_protocol_calls == protocols_before);
    assert(submit_calls == submits_before);
}

static void test_host_and_hid_lifecycle(void)
{
    test_hid_t mouse;
    test_hid_t non_boot;
    test_hid_t short_packet;
    test_hid_t keyboard;
    test_hid_t second_keyboard;
    size_t callback_log_length;

    assert(pal_usb_keyboard_host_start());
    assert(pal_usb_keyboard_host_start());
    assert(message_queue_init_calls == 1u);
    assert(thread_init_calls == 1u);
    assert(thread_startup_calls == 1u);
    assert(host_initialize_calls == 1u);
    assert(last_host_base == (uintptr_t)USBHS_BASE);
    assert(worker_entry != NULL);

    usbh_hid_stop(NULL);
    assert(message_queue_reset_calls == 0u);
    assert(message_send_calls == 0u);

    setup_hid(&mouse, HID_SUBCLASS_BOOTIF, HID_PROTOCOL_MOUSE, 8u);
    setup_hid(&non_boot, 0u, HID_PROTOCOL_KEYBOARD, 8u);
    setup_hid(&short_packet, HID_SUBCLASS_BOOTIF,
              HID_PROTOCOL_KEYBOARD, 4u);
    verify_ignored_hid(&mouse);
    verify_ignored_hid(&non_boot);
    verify_ignored_hid(&short_packet);

    setup_hid(&keyboard, HID_SUBCLASS_BOOTIF, HID_PROTOCOL_KEYBOARD, 8u);
    usbh_hid_run(&keyboard.hid);
    assert(set_protocol_calls == 1u);
    assert(last_protocol == HID_PROTOCOL_BOOT);
    assert(submit_calls == 1u);
    assert(last_urb == &keyboard.hid.intin_urb);
    assert(last_urb->transfer_buffer_length ==
           PAL_USB_KEYBOARD_BOOT_REPORT_SIZE);
    assert(last_urb->complete != NULL);

    setup_hid(&second_keyboard, HID_SUBCLASS_BOOTIF,
              HID_PROTOCOL_KEYBOARD, 8u);
    verify_ignored_hid(&second_keyboard);

    callback_log_length = log_length;
    last_urb->transfer_buffer[2] = 0x52u;
    last_urb->complete(last_urb->arg, 8);
    assert(message_send_calls == 1u);
    assert(sent_messages[0].type == TEST_MESSAGE_REPORT);
    assert(sent_messages[0].report[2] == 0x52u);
    assert(submit_calls == 2u);
    assert(log_length == callback_log_length);

    last_urb->complete(last_urb->arg, -USB_ERR_NAK);
    assert(message_send_calls == 1u);
    assert(submit_calls == 3u);
    assert(log_length == callback_log_length);

    last_urb->complete(last_urb->arg, -USB_ERR_IO);
    assert(message_send_calls == 2u);
    assert(sent_messages[1].type == TEST_MESSAGE_ERROR);
    assert(sent_messages[1].status == -USB_ERR_IO);
    assert(submit_calls == 3u);
    assert(log_length == callback_log_length);

    usbh_hid_stop(&keyboard.hid);
    assert(message_queue_reset_calls == 1u);
    assert(message_send_calls == 3u);
    assert(sent_messages[2].type == TEST_MESSAGE_DISCONNECT);
    last_urb->complete(last_urb->arg, -USB_ERR_NAK);
    assert(submit_calls == 3u);
}

static void test_worker_logs_queued_messages(void)
{
    static const uint32_t expected_masks[] = {
        0u,
        PAL_CONTROL_UP,
        0u,
        0u,
    };

    replay_index = 0u;
    replay_messages = true;
    replay_expected_masks = expected_masks;
    replay_expected_count = sizeof(expected_masks) / sizeof(expected_masks[0]);
    if (setjmp(worker_exit) == 0)
    {
        worker_entry(worker_parameter);
        assert(false);
    }
    replay_messages = false;
    replay_expected_masks = NULL;
    replay_expected_count = 0u;

    assert(strstr(log_output,
                  "[PAL KEY] DOWN usage=0x52 key=UP action=PAL_CONTROL_UP") !=
           NULL);
    assert(strstr(log_output, "[PAL USB] transfer error: -12") != NULL);
    assert(strstr(log_output,
                  "[PAL KEY] UP   usage=0x52 key=UP action=PAL_CONTROL_UP") !=
           NULL);
    assert(strstr(log_output, "[PAL USB] keyboard disconnected") != NULL);
    assert(pal_usb_keyboard_controls_get() == 0u);
}

static void test_worker_updates_control_snapshot(void)
{
    static const uint32_t expected_masks[] = {
        0u,
        PAL_CONTROL_UP | PAL_CONTROL_A,
        PAL_CONTROL_A,
        0u,
        0u,
    };

    memset(sent_messages, 0, sizeof(sent_messages));
    sent_count = 4u;
    sent_messages[0].type = TEST_MESSAGE_REPORT;
    sent_messages[0].generation = 7u;
    sent_messages[0].report[2] = 0x52u;
    sent_messages[0].report[3] = 0x28u;
    sent_messages[1].type = TEST_MESSAGE_REPORT;
    sent_messages[1].generation = 7u;
    sent_messages[1].report[2] = 0x28u;
    sent_messages[2].type = TEST_MESSAGE_REPORT;
    sent_messages[2].generation = 7u;
    sent_messages[3].type = TEST_MESSAGE_REPORT;
    sent_messages[3].generation = 7u;
    sent_messages[3].report[2] = 0x04u;

    replay_index = 0u;
    replay_messages = true;
    replay_expected_masks = expected_masks;
    replay_expected_count = sizeof(expected_masks) / sizeof(expected_masks[0]);
    if (setjmp(worker_exit) == 0)
    {
        worker_entry(worker_parameter);
        assert(false);
    }
    replay_messages = false;
    replay_expected_masks = NULL;
    replay_expected_count = 0u;
    assert(pal_usb_keyboard_controls_get() == 0u);
}

int main(void)
{
    test_host_and_hid_lifecycle();
    test_worker_logs_queued_messages();
    test_worker_updates_control_snapshot();
    puts("usb_keyboard_port: PASS");
    return 0;
}
