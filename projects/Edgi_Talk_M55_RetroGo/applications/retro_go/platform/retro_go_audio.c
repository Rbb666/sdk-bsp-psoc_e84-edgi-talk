#include "retro_go_audio.h"

#include "retro_go_platform.h"

#include "cy_pdl.h"
#include "cybsp.h"

#include <drivers/audio.h>
#include <rtdevice.h>
#include <rtthread.h>

#include <string.h>

#define RETRO_GO_AUDIO_RING_SAMPLES 8192u
#define RETRO_GO_AUDIO_RING_MASK (RETRO_GO_AUDIO_RING_SAMPLES - 1u)
#define RETRO_GO_AUDIO_WRITE_SAMPLES 256u
#define RETRO_GO_AUDIO_RATE 16000u
#define RETRO_GO_AUDIO_STACK_BYTES 4096u
#define RETRO_GO_AUDIO_Q16_ONE 65536u

#ifndef BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES
#define BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES 1024
#endif

#ifndef BSP_RETRO_GO_AUDIO_START_BATCH_SAMPLES
#define BSP_RETRO_GO_AUDIO_START_BATCH_SAMPLES 1024
#endif

#ifndef BSP_RETRO_GO_AUDIO_TARGET_SAMPLES
#define BSP_RETRO_GO_AUDIO_TARGET_SAMPLES 1024
#endif

#ifndef BSP_RETRO_GO_AUDIO_MIN_SPEED_PERCENT
#define BSP_RETRO_GO_AUDIO_MIN_SPEED_PERCENT 25
#endif

#ifndef BSP_RETRO_GO_AUDIO_RATE_WINDOW_MS
#define BSP_RETRO_GO_AUDIO_RATE_WINDOW_MS 500
#endif

#ifndef BSP_RETRO_GO_AUDIO_STARTUP_RATE_WINDOW_MS
#define BSP_RETRO_GO_AUDIO_STARTUP_RATE_WINDOW_MS 100
#endif

#ifndef BSP_RETRO_GO_AUDIO_CONCEAL_FADE_MS
#define BSP_RETRO_GO_AUDIO_CONCEAL_FADE_MS 6
#endif

#if BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES < RETRO_GO_AUDIO_WRITE_SAMPLES || \
    BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES > RETRO_GO_AUDIO_RING_SAMPLES
#error "Retro-Go audio preroll must fit the platform ring"
#endif
#if BSP_RETRO_GO_AUDIO_START_BATCH_SAMPLES > \
        BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES || \
    (BSP_RETRO_GO_AUDIO_START_BATCH_SAMPLES % \
        RETRO_GO_AUDIO_WRITE_SAMPLES) != 0
#error "Retro-Go initial audio batch must be block-aligned and fit preroll"
#endif

#ifndef BSP_RETRO_GO_I2S_THREAD_PRIORITY
#define BSP_RETRO_GO_I2S_THREAD_PRIORITY 21
#endif

#ifndef BSP_RETRO_GO_AUDIO_THREAD_PRIORITY
#define BSP_RETRO_GO_AUDIO_THREAD_PRIORITY 22
#endif

#if defined(FINSH_THREAD_PRIORITY) && \
    (BSP_RETRO_GO_I2S_THREAD_PRIORITY <= FINSH_THREAD_PRIORITY || \
     BSP_RETRO_GO_AUDIO_THREAD_PRIORITY <= FINSH_THREAD_PRIORITY)
#error "Retro-Go audio priority values must be greater than MSH"
#endif

static rt_device_t audio_device;
static struct rt_thread audio_thread RETRO_GO_SOCMEM;
static struct rt_semaphore audio_data_sem RETRO_GO_SOCMEM;
static rt_uint8_t audio_thread_stack[RETRO_GO_AUDIO_STACK_BYTES]
    RETRO_GO_SOCMEM;
static int16_t audio_ring[RETRO_GO_AUDIO_RING_SAMPLES] RETRO_GO_SOCMEM;
static int16_t audio_write_block[RETRO_GO_AUDIO_WRITE_SAMPLES]
    RETRO_GO_DTCM;
#ifdef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
static int16_t audio_start_block[BSP_RETRO_GO_AUDIO_START_BATCH_SAMPLES]
    RETRO_GO_SOCMEM;
#endif
static volatile uint32_t audio_write_index;
static volatile uint32_t audio_read_index;
static volatile uint32_t audio_source_index;
static volatile uint32_t audio_output_index;
static volatile bool audio_stop_requested;
static volatile bool audio_stopped;
static volatile bool audio_thread_reaped = true;
static bool audio_overflow_reported;
static volatile bool audio_playback_started;
static bool audio_thread_started;
static bool audio_ready;

static void audio_thread_cleanup(struct rt_thread *thread)
{
    (void)thread;
    audio_thread_reaped = true;
}

static bool wait_audio_thread_reaped(uint32_t timeout_ms)
{
    uint32_t started = rt_tick_get_millisecond();

    while (!audio_thread_reaped &&
           (uint32_t)(rt_tick_get_millisecond() - started) < timeout_ms)
    {
        /* The static thread object is detached from RT-Thread's defunct
         * queue by idle. Sleeping here gives idle a deterministic window. */
        rt_thread_mdelay(1u);
    }
    return audio_thread_reaped;
}

static bool configure_exact_i2s_clock(void)
{
#ifdef BSP_RETRO_GO_I2S_EXACT_16K_CLOCK
    const en_clk_dst_t divider_group =
        (en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM;
    uint32_t readback[2] __attribute__((aligned(32))) = {0u, 0u};
    cy_en_sysclk_status_t status;

    status = Cy_SysClk_PeriPclkDisableDivider(
        divider_group, CY_SYSCLK_DIV_16_5_BIT, 0u);
    if (status == CY_SYSCLK_SUCCESS)
    {
        /* The register stores divisor-1: value 23 means /24. The vendor
         * mono branch uses 24 (/25, 15.36 kHz), while the board-generated
         * 49.152 MHz audio clock requires /24 for exact 16 kHz LRCK. */
        status = Cy_SysClk_PeriPclkSetFracDivider(
            divider_group, CY_SYSCLK_DIV_16_5_BIT, 0u, 23u, 0u);
    }
    if (status == CY_SYSCLK_SUCCESS)
    {
        status = Cy_SysClk_PeriPclkEnableDivider(
            divider_group, CY_SYSCLK_DIV_16_5_BIT, 0u);
    }
    if (status != CY_SYSCLK_SUCCESS)
    {
        rt_kprintf("[retro-go] exact 16 kHz I2S divider setup failed: %d\n",
                   status);
        return false;
    }
    Cy_SysClk_PeriPclkGetFracDivider(
        divider_group, CY_SYSCLK_DIV_16_5_BIT, 0u,
        &readback[0], &readback[1]);
    rt_kprintf("[retro-go] I2S clock: mono input, divider=%u+%u/32 "
               "(register /%u), LRCK=16000 Hz BCLK=512000 Hz\n",
               (unsigned)readback[0], (unsigned)readback[1],
               (unsigned)(readback[0] + 1u));
    return readback[0] == 23u && readback[1] == 0u;
#else
    return true;
#endif
}

#ifdef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
typedef struct audio_continuity_state
{
    uint32_t phase_q16;
    uint32_t step_q16;
    uint32_t feedforward_q16;
    uint32_t control_started_ms;
    uint32_t control_source_index;
    uint32_t control_output_index;
    uint32_t input_rate_hz;
    uint32_t output_rate_hz;
    uint32_t concealed_samples;
    uint32_t underrun_events;
    int32_t gain_q15;
    int16_t last_source_sample;
    bool measured;
    bool starving;
} audio_continuity_state_t;

static audio_continuity_state_t continuity RETRO_GO_SOCMEM;

static uint32_t continuity_min_step_q16(void)
{
    return (uint32_t)BSP_RETRO_GO_AUDIO_MIN_SPEED_PERCENT *
           RETRO_GO_AUDIO_Q16_ONE / 100u;
}

static void continuity_update_control(void)
{
    uint32_t now_ms = rt_tick_get_millisecond();
    uint32_t elapsed_ms = now_ms - continuity.control_started_ms;
    uint32_t available;
    uint32_t measurement_window_ms = continuity.measured ?
        (uint32_t)BSP_RETRO_GO_AUDIO_RATE_WINDOW_MS :
        (uint32_t)BSP_RETRO_GO_AUDIO_STARTUP_RATE_WINDOW_MS;
    bool first_measurement = false;
    int32_t error;
    int32_t correction_q16;
    int32_t desired_q16;
    const uint32_t maximum_step_q16 = RETRO_GO_AUDIO_Q16_ONE;
    const int32_t correction_limit_q16 =
        (int32_t)(RETRO_GO_AUDIO_Q16_ONE * 3u / 100u);

    if (elapsed_ms >= measurement_window_ms)
    {
        uint32_t source_index = audio_source_index;
        uint32_t output_index = audio_output_index;
        uint32_t source_rate =
            (source_index - continuity.control_source_index) * 1000u /
            elapsed_ms;
        uint32_t output_rate =
            (output_index - continuity.control_output_index) * 1000u /
            elapsed_ms;
        uint32_t feedforward_q16 = (uint32_t)(
            ((uint64_t)source_rate << 16u) / RETRO_GO_AUDIO_RATE);

        if (feedforward_q16 < continuity_min_step_q16())
        {
            feedforward_q16 = continuity_min_step_q16();
        }
        if (feedforward_q16 > maximum_step_q16)
        {
            feedforward_q16 = maximum_step_q16;
        }
        first_measurement = !continuity.measured;
        continuity.measured = true;
        continuity.feedforward_q16 = feedforward_q16;
        continuity.input_rate_hz = source_rate;
        continuity.output_rate_hz = output_rate;
        continuity.control_started_ms = now_ms;
        continuity.control_source_index = source_index;
        continuity.control_output_index = output_index;
    }
#ifndef BSP_RETRO_GO_AUDIO_VARISPEED
    continuity.feedforward_q16 = RETRO_GO_AUDIO_Q16_ONE;
    continuity.step_q16 = RETRO_GO_AUDIO_Q16_ONE;
    return;
#endif
    available = audio_write_index - audio_read_index;
    error = (int32_t)available -
            (int32_t)BSP_RETRO_GO_AUDIO_TARGET_SAMPLES;
    correction_q16 = (int32_t)(((int64_t)error *
        (int32_t)(RETRO_GO_AUDIO_Q16_ONE / 64u)) /
        (int32_t)BSP_RETRO_GO_AUDIO_TARGET_SAMPLES);
    if (correction_q16 < -correction_limit_q16)
    {
        correction_q16 = -correction_limit_q16;
    }
    if (correction_q16 > correction_limit_q16)
    {
        correction_q16 = correction_limit_q16;
    }
    desired_q16 = (int32_t)continuity.feedforward_q16 + correction_q16;
    if (desired_q16 < (int32_t)continuity_min_step_q16())
    {
        desired_q16 = (int32_t)continuity_min_step_q16();
    }
    if (desired_q16 > (int32_t)maximum_step_q16)
    {
        desired_q16 = (int32_t)maximum_step_q16;
    }
    if (first_measurement ||
        (available < RETRO_GO_AUDIO_WRITE_SAMPLES * 2u &&
         (uint32_t)desired_q16 < continuity.step_q16))
    {
        continuity.step_q16 = (uint32_t)desired_q16;
    }
    else if ((uint32_t)desired_q16 < continuity.step_q16)
    {
        continuity.step_q16 =
            (continuity.step_q16 + (uint32_t)desired_q16) / 2u;
    }
    else
    {
        continuity.step_q16 =
            (continuity.step_q16 * 7u + (uint32_t)desired_q16) / 8u;
    }
}

static void continuity_fill_block(int16_t *destination, size_t count)
{
    const uint32_t fade_samples =
        (RETRO_GO_AUDIO_RATE * BSP_RETRO_GO_AUDIO_CONCEAL_FADE_MS) / 1000u;
    const int32_t fade_step =
        (32767 + (int32_t)(fade_samples != 0u ? fade_samples : 1u) - 1) /
        (int32_t)(fade_samples != 0u ? fade_samples : 1u);
    size_t output;

    continuity_update_control();
    for (output = 0u; output < count; ++output)
    {
        uint32_t available = audio_write_index - audio_read_index;

        if (available >= 2u)
        {
            uint32_t read_index = audio_read_index;
            int32_t first =
                audio_ring[read_index & RETRO_GO_AUDIO_RING_MASK];
            int32_t second =
                audio_ring[(read_index + 1u) & RETRO_GO_AUDIO_RING_MASK];
            int32_t sample = first +
                (int32_t)(((int64_t)(second - first) *
                           (int32_t)(continuity.phase_q16 & 0xffffu)) >> 16u);
            uint32_t advance;

            continuity.phase_q16 += continuity.step_q16;
            advance = continuity.phase_q16 >> 16u;
            if (advance >= available)
            {
                advance = available - 1u;
            }
            audio_read_index += advance;
            continuity.phase_q16 &= 0xffffu;
            continuity.last_source_sample = (int16_t)sample;
            continuity.starving = false;
            if (continuity.gain_q15 < 32767)
            {
                continuity.gain_q15 += fade_step;
                if (continuity.gain_q15 > 32767)
                {
                    continuity.gain_q15 = 32767;
                }
            }
            destination[output] = continuity.gain_q15 == 32767 ?
                (int16_t)sample : (int16_t)(
                    sample * continuity.gain_q15 / 32767);
        }
        else
        {
            if (!continuity.starving)
            {
                continuity.starving = true;
                ++continuity.underrun_events;
            }
            if (continuity.gain_q15 > 0)
            {
                continuity.gain_q15 -= fade_step;
                if (continuity.gain_q15 < 0)
                {
                    continuity.gain_q15 = 0;
                }
            }
            destination[output] = (int16_t)(
                (int32_t)continuity.last_source_sample *
                continuity.gain_q15 / 32767);
            ++continuity.concealed_samples;
        }
    }
}
#endif

static bool configure_i2s_thread_priority(void)
{
    rt_thread_t thread = rt_thread_find("sound_thread");
    rt_uint8_t priority = BSP_RETRO_GO_I2S_THREAD_PRIORITY;

    if (thread == RT_NULL)
    {
        rt_kprintf("[retro-go] I2S playback thread not found\n");
        return false;
    }
    if (rt_thread_control(thread, RT_THREAD_CTRL_CHANGE_PRIORITY,
                          &priority) != RT_EOK)
    {
        rt_kprintf("[retro-go] I2S playback priority setup failed\n");
        return false;
    }
    return true;
}

static bool verify_driver_input_format(void)
{
    struct rt_audio_caps readback;

    rt_memset(&readback, 0, sizeof(readback));
    readback.main_type = AUDIO_TYPE_OUTPUT;
    readback.sub_type = AUDIO_DSP_PARAM;
    if (rt_device_control(audio_device, AUDIO_CTL_GETCAPS, &readback) !=
            RT_EOK ||
        readback.udata.config.samplerate != RETRO_GO_AUDIO_RATE ||
        readback.udata.config.channels != 1u ||
        readback.udata.config.samplebits != 16u)
    {
        rt_kprintf("[retro-go] audio format mismatch: rate=%u channels=%u "
                   "bits=%u (required 16000/1/16)\n",
                   (unsigned)readback.udata.config.samplerate,
                   (unsigned)readback.udata.config.channels,
                   (unsigned)readback.udata.config.samplebits);
        return false;
    }
    rt_kprintf("[retro-go] audio format readback: 16000 Hz, "
               "channels=1 mono s16; driver expands to dual-mono I2S\n");
    return true;
}

#ifndef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
static size_t ring_take(int16_t *destination, size_t capacity)
{
    rt_base_t level;
    uint32_t available;
    size_t count;
    size_t first;

    level = rt_hw_interrupt_disable();
    available = audio_write_index - audio_read_index;
    if (available < capacity && !audio_stop_requested)
    {
        rt_hw_interrupt_enable(level);
        return 0u;
    }
    count = available < capacity ? (size_t)available : capacity;
    count &= ~(size_t)1u;
    first = count;
    if (first > RETRO_GO_AUDIO_RING_SAMPLES -
                    (audio_read_index & RETRO_GO_AUDIO_RING_MASK))
    {
        first = RETRO_GO_AUDIO_RING_SAMPLES -
                (audio_read_index & RETRO_GO_AUDIO_RING_MASK);
    }
    if (first != 0u)
    {
        memcpy(destination,
               &audio_ring[audio_read_index & RETRO_GO_AUDIO_RING_MASK],
               first * sizeof(destination[0]));
    }
    if (count > first)
    {
        memcpy(destination + first, audio_ring,
               (count - first) * sizeof(destination[0]));
    }
    audio_read_index += (uint32_t)count;
    rt_hw_interrupt_enable(level);
    return count;
}
#endif

static void audio_thread_entry(void *parameter)
{
#ifdef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
    bool first_device_write = true;
#endif

    (void)parameter;
    audio_stopped = false;
    while (!audio_stop_requested)
    {
#ifdef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
        rt_ssize_t written;
        int16_t *write_block = first_device_write ?
            audio_start_block : audio_write_block;
        size_t write_samples = first_device_write ?
            BSP_RETRO_GO_AUDIO_START_BATCH_SAMPLES :
            RETRO_GO_AUDIO_WRITE_SAMPLES;

        if (!audio_playback_started)
        {
            if (rt_sem_take(&audio_data_sem, RT_WAITING_FOREVER) != RT_EOK)
            {
                continue;
            }
            if (!audio_playback_started)
            {
                continue;
            }
        }
        continuity_fill_block(write_block, write_samples);
        written = audio_device != RT_NULL ?
            rt_device_write(audio_device, 0, write_block,
                            write_samples * sizeof(write_block[0])) : 0;
        if (written ==
            (rt_ssize_t)(write_samples * sizeof(write_block[0])))
        {
            audio_output_index += (uint32_t)write_samples;
            first_device_write = false;
        }
        else if (!audio_stop_requested)
        {
            rt_thread_mdelay(1u);
        }
#else
        size_t count;

        if (rt_sem_take(&audio_data_sem, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }
        do
        {
            count = ring_take(audio_write_block,
                              RETRO_GO_AUDIO_WRITE_SAMPLES);
            if (count != 0u && audio_device != RT_NULL)
            {
                rt_ssize_t written = rt_device_write(
                    audio_device, 0, audio_write_block,
                    count * sizeof(audio_write_block[0]));
                if (written ==
                    (rt_ssize_t)(count * sizeof(audio_write_block[0])))
                {
                    audio_output_index += (uint32_t)count;
                }
            }
        } while (count != 0u && !audio_stop_requested);
#endif
    }
    audio_stopped = true;
}

bool retro_go_audio_init(void)
{
#ifdef BSP_RETRO_GO_AUDIO
    struct rt_audio_caps caps;
    rt_err_t result;

    if (audio_thread_started || !audio_thread_reaped)
    {
        rt_kprintf("[retro-go] previous audio worker is still retiring\n");
        return false;
    }

    audio_device = rt_device_find("sound0");
    if (audio_device == RT_NULL)
    {
        rt_kprintf("[retro-go] sound0 not found; audio disabled\n");
        return false;
    }
    if (!configure_i2s_thread_priority())
    {
        audio_device = RT_NULL;
        return false;
    }
    if (rt_device_open(audio_device, RT_DEVICE_OFLAG_WRONLY) != RT_EOK)
    {
        rt_kprintf("[retro-go] sound0 open failed; audio disabled\n");
        audio_device = RT_NULL;
        return false;
    }

    rt_memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = 16000u;
    /* The BSP_USING_SDLPAL driver input contract is mono. Its playback task
     * expands every input sample to L/R itself. Setting channels=2 selects
     * drv_i2s.c's divider-11 branch and drives LRCK at roughly 32 kHz. The
     * mono contract selects the driver divider-24 branch; the platform then
     * corrects its off-by-one value to 23 for an exact 16 kHz sink. */
    caps.udata.config.channels = 1u;
    caps.udata.config.samplebits = 16u;
    if (rt_device_control(audio_device, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK)
    {
        rt_kprintf("[retro-go] sound0 configure failed; audio disabled\n");
        (void)rt_device_close(audio_device);
        audio_device = RT_NULL;
        return false;
    }
    if (!verify_driver_input_format())
    {
        (void)rt_device_close(audio_device);
        audio_device = RT_NULL;
        return false;
    }
    if (!configure_exact_i2s_clock())
    {
        rt_kprintf("[retro-go] exact 16 kHz clock unavailable; audio disabled\n");
        (void)rt_device_close(audio_device);
        audio_device = RT_NULL;
        return false;
    }

    audio_write_index = 0u;
    audio_read_index = 0u;
    audio_source_index = 0u;
    audio_output_index = 0u;
    audio_stop_requested = false;
    audio_stopped = false;
    audio_thread_reaped = false;
    audio_overflow_reported = false;
    audio_playback_started = false;
#ifdef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
    memset(&continuity, 0, sizeof(continuity));
    continuity.phase_q16 = 0u;
    continuity.step_q16 = RETRO_GO_AUDIO_Q16_ONE;
    continuity.feedforward_q16 = RETRO_GO_AUDIO_Q16_ONE;
    continuity.gain_q15 = 32767;
    continuity.control_started_ms = rt_tick_get_millisecond();
#endif
    result = rt_sem_init(&audio_data_sem, "rg_audio", 0u,
                         RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        audio_thread_reaped = true;
        (void)rt_device_close(audio_device);
        audio_device = RT_NULL;
        return false;
    }
    result = rt_thread_init(
        &audio_thread, "rg_audio", audio_thread_entry, RT_NULL,
        audio_thread_stack, sizeof(audio_thread_stack),
        BSP_RETRO_GO_AUDIO_THREAD_PRIORITY, 5u);
    if (result != RT_EOK)
    {
        audio_thread_reaped = true;
        (void)rt_sem_detach(&audio_data_sem);
        (void)rt_device_close(audio_device);
        audio_device = RT_NULL;
        return false;
    }
    audio_thread.cleanup = audio_thread_cleanup;
    result = rt_thread_startup(&audio_thread);
    if (result != RT_EOK)
    {
        (void)rt_thread_detach(&audio_thread);
        (void)wait_audio_thread_reaped(500u);
        (void)rt_sem_detach(&audio_data_sem);
        (void)rt_device_close(audio_device);
        audio_device = RT_NULL;
        return false;
    }

    audio_thread_started = true;
    audio_ready = true;
    rt_kprintf("[retro-go] audio ready: 16000 Hz mono input -> driver "
               "dual-mono I2S, "
               "async ring=%u, preroll=%u, start-batch=%u, "
               "continuity=%s, priority=%u/%u\n",
               RETRO_GO_AUDIO_RING_SAMPLES,
               BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES,
               BSP_RETRO_GO_AUDIO_START_BATCH_SAMPLES,
#ifdef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
#ifdef BSP_RETRO_GO_AUDIO_VARISPEED
               "linear-varispeed",
#else
               "linear-conceal",
#endif
#else
               "off",
#endif
               BSP_RETRO_GO_I2S_THREAD_PRIORITY,
               BSP_RETRO_GO_AUDIO_THREAD_PRIORITY);
    return true;
#else
    return false;
#endif
}

void retro_go_audio_submit(const int16_t *samples, size_t sample_count)
{
    rt_base_t level;
    uint32_t used;
    size_t free_samples;
    size_t accepted;
    size_t first;
    size_t dropped;
    bool report_overflow = false;
    bool wake_worker = false;

    if (!audio_ready || samples == NULL || sample_count == 0u)
    {
        return;
    }
    sample_count &= ~(size_t)1u;
    if (sample_count == 0u)
    {
        return;
    }
    level = rt_hw_interrupt_disable();
#ifdef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
    if (audio_source_index == 0u)
    {
        continuity.control_started_ms = rt_tick_get_millisecond();
        continuity.control_source_index = (uint32_t)sample_count;
        continuity.control_output_index = 0u;
    }
#endif
    audio_source_index += (uint32_t)sample_count;
    used = audio_write_index - audio_read_index;
    free_samples = RETRO_GO_AUDIO_RING_SAMPLES - used;
    accepted = sample_count < free_samples ? sample_count : free_samples;
    accepted &= ~(size_t)1u;
    first = accepted;
    if (first > RETRO_GO_AUDIO_RING_SAMPLES -
                    (audio_write_index & RETRO_GO_AUDIO_RING_MASK))
    {
        first = RETRO_GO_AUDIO_RING_SAMPLES -
                (audio_write_index & RETRO_GO_AUDIO_RING_MASK);
    }
    if (first != 0u)
    {
        memcpy(&audio_ring[audio_write_index & RETRO_GO_AUDIO_RING_MASK],
               samples, first * sizeof(samples[0]));
    }
    if (accepted > first)
    {
        memcpy(audio_ring, samples + first,
               (accepted - first) * sizeof(samples[0]));
    }
    audio_write_index += (uint32_t)accepted;
    if (accepted != 0u && !audio_playback_started &&
        (audio_write_index - audio_read_index) >=
            BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES)
    {
        audio_playback_started = true;
        wake_worker = true;
    }
#ifndef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
    else if (accepted != 0u && audio_playback_started)
    {
        wake_worker = true;
    }
#endif
    dropped = sample_count - accepted;
    if (dropped != 0u && !audio_overflow_reported)
    {
        audio_overflow_reported = true;
        report_overflow = true;
    }
    rt_hw_interrupt_enable(level);

    if (wake_worker)
    {
        (void)rt_sem_release(&audio_data_sem);
    }
    if (report_overflow)
    {
        rt_kprintf("[retro-go] audio buffer overrun; dropped=%u samples\n",
                   (unsigned)dropped);
    }
}

size_t retro_go_audio_buffered_samples(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    size_t buffered = audio_ready ?
        (size_t)(audio_write_index - audio_read_index) : 0u;

    rt_hw_interrupt_enable(level);
    return buffered;
}

uint32_t retro_go_audio_submitted_samples(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    uint32_t submitted = audio_write_index;

    rt_hw_interrupt_enable(level);
    return submitted;
}

void retro_go_audio_get_stats(retro_go_audio_stats_t *stats)
{
    rt_base_t level;
    uint32_t available;
    uint32_t step_q16 = RETRO_GO_AUDIO_Q16_ONE;

    if (stats == NULL)
    {
        return;
    }
    memset(stats, 0, sizeof(*stats));
    level = rt_hw_interrupt_disable();
    available = audio_ready ? audio_write_index - audio_read_index : 0u;
    stats->source_buffer_samples = available;
#ifdef BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR
    step_q16 = continuity.step_q16 != 0u ?
        continuity.step_q16 : RETRO_GO_AUDIO_Q16_ONE;
    stats->input_rate_hz = continuity.input_rate_hz;
    stats->output_rate_hz = continuity.output_rate_hz;
    stats->concealed_samples = continuity.concealed_samples;
    stats->underrun_events = continuity.underrun_events;
#endif
    rt_hw_interrupt_enable(level);
    stats->stretch_permille =
        (uint32_t)(((uint64_t)step_q16 * 1000u) >> 16u);
    stats->source_buffer_ms = step_q16 != 0u ?
        (uint32_t)(((uint64_t)available * 1000u *
                    RETRO_GO_AUDIO_Q16_ONE) /
                   ((uint64_t)RETRO_GO_AUDIO_RATE * step_q16)) : 0u;
}

void retro_go_audio_deinit(void)
{
    uint32_t started;

    audio_ready = false;
    if (audio_thread_started)
    {
        audio_stop_requested = true;
        (void)rt_sem_release(&audio_data_sem);
        started = rt_tick_get_millisecond();
        while (!audio_stopped &&
               (uint32_t)(rt_tick_get_millisecond() - started) < 500u)
        {
            rt_thread_mdelay(1u);
        }
        if (audio_stopped && wait_audio_thread_reaped(500u))
        {
            /* The idle thread has detached the static worker object. Only
             * its separately initialized semaphore remains ours to detach. */
            (void)rt_sem_detach(&audio_data_sem);
            audio_thread_started = false;
        }
        else if (!audio_stopped)
        {
            rt_kprintf("[retro-go] audio worker stop timeout\n");
        }
        else
        {
            rt_kprintf("[retro-go] audio worker idle cleanup timeout\n");
        }
    }
    if (audio_device != RT_NULL &&
        (!audio_thread_started || audio_stopped))
    {
        (void)rt_device_close(audio_device);
        audio_device = RT_NULL;
    }
}
