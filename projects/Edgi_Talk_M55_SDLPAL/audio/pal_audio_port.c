#include "pal_audio_port.h"

#include <drivers/audio.h>
#include <rtdevice.h>
#include <rtthread.h>

#include <string.h>

#define PAL_AUDIO_SAMPLE_RATE 16000u
#define PAL_AUDIO_SAMPLE_BITS 16u
#define PAL_AUDIO_CHANNELS 1u
#define PAL_AUDIO_BLOCK_SAMPLES 256u
#define PAL_AUDIO_BLOCK_BYTES \
    (PAL_AUDIO_BLOCK_SAMPLES * sizeof(int16_t))
#define PAL_AUDIO_STACK_BYTES (8u * 1024u)
#define PAL_AUDIO_THREAD_PRIORITY ((RT_THREAD_PRIORITY_MAX / 2u) - 1u)
#define PAL_AUDIO_THREAD_SLICE 5u

#if defined(__GNUC__)
#define PAL_AUDIO_SRAM __attribute__((section(".sdlpal_audio"), aligned(8)))
#else
#define PAL_AUDIO_SRAM
#endif

typedef struct pal_audio_port_state
{
    rt_device_t device;
    pal_audio_render_fn render;
    void *render_context;
    volatile rt_bool_t running;
    volatile rt_bool_t stop_requested;
    volatile rt_bool_t stopped;
} pal_audio_port_state_t;

static struct rt_thread audio_thread PAL_AUDIO_SRAM;
static rt_uint8_t audio_stack[PAL_AUDIO_STACK_BYTES] PAL_AUDIO_SRAM;
static int16_t audio_block[PAL_AUDIO_BLOCK_SAMPLES] PAL_AUDIO_SRAM;
static pal_audio_port_state_t audio_state PAL_AUDIO_SRAM;

static int audio_configure(rt_device_t device)
{
    struct rt_audio_caps caps;

    rt_memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = PAL_AUDIO_SAMPLE_RATE;
    caps.udata.config.channels = PAL_AUDIO_CHANNELS;
    caps.udata.config.samplebits = PAL_AUDIO_SAMPLE_BITS;
    return rt_device_control(device, AUDIO_CTL_CONFIGURE, &caps);
}

static void audio_thread_entry(void *parameter)
{
    rt_ssize_t written;

    (void)parameter;
    audio_state.running = RT_TRUE;
    audio_state.stopped = RT_FALSE;

    while (!audio_state.stop_requested)
    {
        audio_state.render(audio_state.render_context, audio_block,
                           PAL_AUDIO_BLOCK_SAMPLES);
        written = rt_device_write(audio_state.device, 0, audio_block,
                                  PAL_AUDIO_BLOCK_BYTES);
        if (written != (rt_ssize_t)PAL_AUDIO_BLOCK_BYTES)
        {
            rt_memset(audio_block, 0, sizeof(audio_block));
            (void)rt_device_write(audio_state.device, 0, audio_block,
                                  PAL_AUDIO_BLOCK_BYTES);
        }
    }

    (void)rt_device_close(audio_state.device);
    audio_state.device = RT_NULL;
    audio_state.running = RT_FALSE;
    audio_state.stopped = RT_TRUE;
}

int pal_audio_port_start(pal_audio_render_fn render, void *context)
{
    rt_err_t result;
    rt_device_t device;

    if (render == NULL)
    {
        return -RT_EINVAL;
    }
    if (audio_state.running)
    {
        return RT_EOK;
    }

    device = rt_device_find("sound0");
    if (device == RT_NULL)
    {
        return -RT_ENOSYS;
    }
    result = rt_device_open(device, RT_DEVICE_OFLAG_WRONLY);
    if (result != RT_EOK)
    {
        return result;
    }
    result = audio_configure(device);
    if (result != RT_EOK)
    {
        (void)rt_device_close(device);
        return result;
    }

    rt_memset(&audio_state, 0, sizeof(audio_state));
    audio_state.device = device;
    audio_state.render = render;
    audio_state.render_context = context;
    audio_state.running = RT_TRUE;
    result = rt_thread_init(&audio_thread, "pal_audio", audio_thread_entry,
                            RT_NULL, audio_stack, sizeof(audio_stack),
                            PAL_AUDIO_THREAD_PRIORITY,
                            PAL_AUDIO_THREAD_SLICE);
    if (result != RT_EOK)
    {
        (void)rt_device_close(device);
        rt_memset(&audio_state, 0, sizeof(audio_state));
        return result;
    }
    result = rt_thread_startup(&audio_thread);
    if (result != RT_EOK)
    {
        (void)rt_thread_detach(&audio_thread);
        (void)rt_device_close(device);
        rt_memset(&audio_state, 0, sizeof(audio_state));
        return result;
    }
    return RT_EOK;
}

int pal_audio_port_stop(uint32_t timeout_ms)
{
    uint32_t start;

    if (!audio_state.running && audio_state.device == RT_NULL)
    {
        return RT_EOK;
    }

    audio_state.stop_requested = RT_TRUE;
    start = rt_tick_get_millisecond();
    while (!audio_state.stopped)
    {
        if ((uint32_t)(rt_tick_get_millisecond() - start) >= timeout_ms)
        {
            return -RT_ETIMEOUT;
        }
        rt_thread_mdelay(1u);
    }
    return RT_EOK;
}

int pal_audio_port_is_running(void)
{
    return audio_state.running != RT_FALSE;
}
