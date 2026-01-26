#include <rtthread.h>
#include <rtdevice.h>

#ifdef RT_USING_CAN

#define CAN_SAMPLE_DEFAULT_DEV "canfd0"

static rt_device_t can_dev;
static struct rt_semaphore rx_sem;

static rt_err_t can_rx_ind(rt_device_t dev, rt_size_t size)
{
    RT_UNUSED(dev);
    RT_UNUSED(size);
    rt_sem_release(&rx_sem);
    return RT_EOK;
}

static void can_rx_thread(void *parameter)
{
    struct rt_can_msg rxmsg = {0};
    rt_err_t res;

    RT_UNUSED(parameter);

    rt_device_set_rx_indicate(can_dev, can_rx_ind);

    while (1)
    {
        rxmsg.hdr_index = -1; /* read from uselist */
        res = rt_sem_take(&rx_sem, RT_WAITING_FOREVER);
        RT_ASSERT(res == RT_EOK);

        rt_device_read(can_dev, 0, &rxmsg, sizeof(rxmsg));

        rt_kprintf("[CAN] RX ID:0x%lx DLC:%d Data:", rxmsg.id, rxmsg.len);
        for (rt_uint8_t i = 0; i < rxmsg.len; i++)
        {
            rt_kprintf(" %02x", rxmsg.data[i]);
        }
        rt_kprintf("\n");
    }
}

static int can_sample(int argc, char *argv[])
{
    struct rt_can_msg msg = {0};
    rt_thread_t thread;
    rt_err_t res;
    char can_name[RT_NAME_MAX];

    if (argc >= 2)
    {
        rt_strncpy(can_name, argv[1], RT_NAME_MAX);
    }
    else
    {
        rt_strncpy(can_name, CAN_SAMPLE_DEFAULT_DEV, RT_NAME_MAX);
    }

    can_dev = rt_device_find(can_name);
    if (!can_dev)
    {
        rt_kprintf("Find %s failed.\n", can_name);
        return -RT_ERROR;
    }

    res = rt_sem_init(&rx_sem, "can_rx", 0, RT_IPC_FLAG_FIFO);
    RT_ASSERT(res == RT_EOK);

    res = rt_device_open(can_dev, RT_DEVICE_FLAG_INT_TX | RT_DEVICE_FLAG_INT_RX);
    RT_ASSERT(res == RT_EOK);

    rt_device_control(can_dev, RT_CAN_CMD_SET_MODE, (void *)RT_CAN_MODE_NORMAL);

#ifdef RT_CAN_USING_HDR
    {
        struct rt_can_filter_item items[1] =
        {
            RT_CAN_FILTER_ITEM_INIT(0x3, 0, 0, 0, 0, RT_NULL, RT_NULL),
        };
        struct rt_can_filter_config cfg = {1, 1, items};
        rt_device_control(can_dev, RT_CAN_CMD_SET_FILTER, &cfg);
    }
#endif

    thread = rt_thread_create("can_rx", can_rx_thread, RT_NULL, 2048, 25, 10);
    if (thread != RT_NULL)
    {
        res = rt_thread_startup(thread);
        RT_ASSERT(res == RT_EOK);
    }
    else
    {
        rt_kprintf("Create can_rx thread failed.\n");
        return -RT_ERROR;
    }

    msg.id = 0x78;
    msg.ide = RT_CAN_STDID;
    msg.rtr = RT_CAN_DTR;
    msg.len = 8;

    msg.data[0] = 0x00;
    msg.data[1] = 0x11;
    msg.data[2] = 0x22;
    msg.data[3] = 0x33;
    msg.data[4] = 0x44;
    msg.data[5] = 0x55;
    msg.data[6] = 0x66;
    msg.data[7] = 0x77;

    rt_device_write(can_dev, 0, &msg, sizeof(msg));

    return RT_EOK;
}
MSH_CMD_EXPORT(can_sample, can device sample);

#endif /* RT_USING_CAN */
