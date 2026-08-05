#include "pal_touch_port.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "drv_touch.h"
#include "pal_touch_poll_cache.h"

#define PAL_TOUCH_DEVICE_NAME "ST7102"

static rt_device_t touch_device;
static pal_touch_poll_cache_t touch_cache;

bool pal_touch_port_init(void)
{
    if (touch_device != RT_NULL)
    {
        return true;
    }

    touch_device = rt_device_find(PAL_TOUCH_DEVICE_NAME);
    if (touch_device == RT_NULL)
    {
        if (rt_hw_ST7102_port() != RT_EOK)
        {
            return false;
        }
        touch_device = rt_device_find(PAL_TOUCH_DEVICE_NAME);
    }

    if (touch_device == RT_NULL ||
        rt_device_open(touch_device, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
    {
        touch_device = RT_NULL;
        return false;
    }

    pal_touch_poll_cache_init(&touch_cache);
    return true;
}

bool pal_touch_port_poll(pal_touch_point_t *points, size_t capacity,
                         size_t *count)
{
    struct rt_touch_data data[PAL_TOUCH_MAX_POINTS];
    pal_touch_point_t sampled_points[PAL_TOUCH_MAX_POINTS];
    rt_ssize_t result;
    size_t sampled_count = 0u;
    size_t i;
    uint32_t now_ms;

    if (count == NULL || (capacity != 0u && points == NULL))
    {
        return false;
    }
    *count = 0u;

    if (!pal_touch_port_init())
    {
        return false;
    }

    now_ms = (uint32_t)rt_tick_get_millisecond();
    if (!pal_touch_poll_cache_sample_due(&touch_cache, now_ms))
    {
        *count = pal_touch_poll_cache_copy(&touch_cache, points, capacity);
        return true;
    }

    rt_memset(data, 0, sizeof(data));
    result = rt_device_read(touch_device, 0, data, PAL_TOUCH_MAX_POINTS);
    if (result < 0)
    {
        return false;
    }

    for (i = 0u; i < PAL_TOUCH_MAX_POINTS; ++i)
    {
        if (data[i].event == RT_TOUCH_EVENT_DOWN ||
            data[i].event == RT_TOUCH_EVENT_MOVE)
        {
            sampled_points[sampled_count].x = data[i].x_coordinate;
            sampled_points[sampled_count].y = data[i].y_coordinate;
            sampled_points[sampled_count].id = data[i].track_id;
            sampled_points[sampled_count].active = 1u;
            ++sampled_count;
        }
    }

    pal_touch_poll_cache_store(&touch_cache, now_ms, sampled_points,
                               sampled_count);
    *count = pal_touch_poll_cache_copy(&touch_cache, points, capacity);
    return true;
}
