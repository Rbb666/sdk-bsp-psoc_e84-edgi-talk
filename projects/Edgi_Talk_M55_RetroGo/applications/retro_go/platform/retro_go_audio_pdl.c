#include "retro_go_audio.h"

#include "retro_go_platform.h"

#include "cy_pdl.h"
#include "cybsp.h"
#include "drv_es8388.h"

#include <rtthread.h>

#include <string.h>

#define RETRO_GO_AUDIO_RING_SAMPLES 8192u
#define RETRO_GO_AUDIO_RING_MASK (RETRO_GO_AUDIO_RING_SAMPLES - 1u)
#define RETRO_GO_AUDIO_RATE 16000u
#define RETRO_GO_AUDIO_INPUT_CHANNELS 1u
#define RETRO_GO_AUDIO_SAMPLE_BITS 16u
#define RETRO_GO_AUDIO_Q16_ONE 65536u
#define RETRO_GO_TDM_FIFO_MONO_FRAMES 32u
#define RETRO_GO_TDM_STARTUP_MONO_FRAMES 64u
#define RETRO_GO_WSOLA_OUTPUT_RING_SAMPLES 4096u
#define RETRO_GO_WSOLA_OUTPUT_RING_MASK \
    (RETRO_GO_WSOLA_OUTPUT_RING_SAMPLES - 1u)
#ifndef BSP_RETRO_GO_AUDIO_WSOLA_GRAIN_SAMPLES
#define BSP_RETRO_GO_AUDIO_WSOLA_GRAIN_SAMPLES 512
#endif
#ifndef BSP_RETRO_GO_AUDIO_WSOLA_SEARCH_SAMPLES
#define BSP_RETRO_GO_AUDIO_WSOLA_SEARCH_SAMPLES 96
#endif
#define RETRO_GO_WSOLA_GRAIN_SAMPLES \
    ((uint32_t)BSP_RETRO_GO_AUDIO_WSOLA_GRAIN_SAMPLES)
#define RETRO_GO_WSOLA_HOP_SAMPLES \
    (RETRO_GO_WSOLA_GRAIN_SAMPLES / 2u)
#define RETRO_GO_WSOLA_SEARCH_SAMPLES \
    ((uint32_t)BSP_RETRO_GO_AUDIO_WSOLA_SEARCH_SAMPLES)
#define RETRO_GO_WSOLA_SCORE_STRIDE 2u

#ifndef BSP_RETRO_GO_AUDIO_OUTPUT_TARGET_SAMPLES
#define BSP_RETRO_GO_AUDIO_OUTPUT_TARGET_SAMPLES 3584
#endif

#ifndef BSP_RETRO_GO_AUDIO_BUFFER_CORRECTION_PERCENT
#define BSP_RETRO_GO_AUDIO_BUFFER_CORRECTION_PERCENT 12
#endif

#ifndef BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES
#define BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES 2048
#endif

#ifndef BSP_RETRO_GO_AUDIO_TARGET_SAMPLES
#define BSP_RETRO_GO_AUDIO_TARGET_SAMPLES 1536
#endif

#ifndef BSP_RETRO_GO_AUDIO_MIN_SPEED_PERCENT
#define BSP_RETRO_GO_AUDIO_MIN_SPEED_PERCENT 25
#endif

#ifndef BSP_RETRO_GO_AUDIO_RATE_WINDOW_MS
#define BSP_RETRO_GO_AUDIO_RATE_WINDOW_MS 250
#endif

#ifndef BSP_RETRO_GO_AUDIO_STARTUP_RATE_WINDOW_MS
#define BSP_RETRO_GO_AUDIO_STARTUP_RATE_WINDOW_MS 50
#endif

#ifndef BSP_RETRO_GO_AUDIO_CONCEAL_FADE_MS
#define BSP_RETRO_GO_AUDIO_CONCEAL_FADE_MS 10
#endif

#if (RETRO_GO_AUDIO_RING_SAMPLES & RETRO_GO_AUDIO_RING_MASK) != 0
#error "Retro-Go PDL audio ring must be a power of two"
#endif
#if BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES > RETRO_GO_AUDIO_RING_SAMPLES
#error "Retro-Go PDL audio preroll must fit the source ring"
#endif
#if RETRO_GO_AUDIO_INPUT_CHANNELS != 1u || RETRO_GO_AUDIO_SAMPLE_BITS != 16u
#error "Retro-Go PDL backend accepts only one channel of signed 16-bit PCM"
#endif
#if (RETRO_GO_WSOLA_OUTPUT_RING_SAMPLES & \
     RETRO_GO_WSOLA_OUTPUT_RING_MASK) != 0
#error "Retro-Go WSOLA output ring must be a power of two"
#endif
#if BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES > \
    RETRO_GO_WSOLA_OUTPUT_RING_SAMPLES
#error "Retro-Go audio preroll must fit the WSOLA output ring"
#endif
#if BSP_RETRO_GO_AUDIO_OUTPUT_TARGET_SAMPLES > \
    RETRO_GO_WSOLA_OUTPUT_RING_SAMPLES
#error "Retro-Go WSOLA target must fit the output ring"
#endif
#if (BSP_RETRO_GO_AUDIO_WSOLA_GRAIN_SAMPLES & 1) != 0 || \
    BSP_RETRO_GO_AUDIO_WSOLA_GRAIN_SAMPLES < 256 || \
    BSP_RETRO_GO_AUDIO_WSOLA_GRAIN_SAMPLES > 1024
#error "Retro-Go WSOLA grain must be an even value from 256 to 1024"
#endif
#if BSP_RETRO_GO_AUDIO_WSOLA_SEARCH_SAMPLES >= \
    (BSP_RETRO_GO_AUDIO_WSOLA_GRAIN_SAMPLES / 2)
#error "Retro-Go WSOLA search must be smaller than the synthesis hop"
#endif

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

typedef struct audio_wsola_state
{
    uint64_t source_position_q16;
    uint32_t current_start;
    uint32_t next_start;
    uint16_t hop_position;
    bool initialized;
    bool first_hop;
    bool next_ready;
    int16_t current_grain[RETRO_GO_WSOLA_GRAIN_SAMPLES];
    int16_t next_grain[RETRO_GO_WSOLA_GRAIN_SAMPLES];
} audio_wsola_state_t;

static int16_t audio_ring[RETRO_GO_AUDIO_RING_SAMPLES] RETRO_GO_SOCMEM;
static audio_continuity_state_t continuity RETRO_GO_SOCMEM;
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
static int16_t wsola_output_ring[RETRO_GO_WSOLA_OUTPUT_RING_SAMPLES]
    RETRO_GO_SOCMEM;
static audio_wsola_state_t wsola RETRO_GO_SOCMEM;
static volatile uint32_t wsola_output_write_index;
static volatile uint32_t wsola_output_read_index;
#endif
static volatile uint32_t audio_write_index;
static volatile uint32_t audio_read_index;
static volatile uint32_t audio_source_index;
static volatile uint32_t audio_output_index;
static volatile uint32_t audio_hardware_underflows;
static volatile bool audio_ready;
static volatile bool audio_playback_started;
static volatile bool audio_hw_running;
static bool audio_overflow_reported;
static cy_stc_tdm_config_tx_t pdl_tdm_tx_config;
static cy_stc_tdm_config_t pdl_tdm_config;

static uint32_t continuity_min_step_q16(void)
{
    return (uint32_t)BSP_RETRO_GO_AUDIO_MIN_SPEED_PERCENT *
           RETRO_GO_AUDIO_Q16_ONE / 100u;
}

static void continuity_update_control(void)
{
    uint32_t now_ms = rt_tick_get_millisecond();
    uint32_t elapsed_ms = now_ms - continuity.control_started_ms;
    uint32_t measurement_window_ms = continuity.measured ?
        (uint32_t)BSP_RETRO_GO_AUDIO_RATE_WINDOW_MS :
        (uint32_t)BSP_RETRO_GO_AUDIO_STARTUP_RATE_WINDOW_MS;
    uint32_t available = audio_write_index - audio_read_index;
    bool first_measurement = false;
    int32_t error;
    int32_t correction_q16;
    int32_t desired_q16;
    const uint32_t maximum_step_q16 = RETRO_GO_AUDIO_Q16_ONE;
    const int32_t correction_limit_q16 =
        (int32_t)(RETRO_GO_AUDIO_Q16_ONE *
                  BSP_RETRO_GO_AUDIO_BUFFER_CORRECTION_PERCENT / 100u);

    if (elapsed_ms >= measurement_window_ms)
    {
        uint32_t source_index = audio_source_index;
        uint32_t output_index = audio_output_index;
        uint32_t source_delta =
            source_index - continuity.control_source_index;
        uint32_t output_delta =
            output_index - continuity.control_output_index;
        uint32_t source_rate = source_delta * 1000u / elapsed_ms;
        uint32_t output_rate = output_delta * 1000u / elapsed_ms;
        uint32_t measured_q16 = output_delta != 0u ?
            (uint32_t)(((uint64_t)source_delta << 16u) / output_delta) :
            (uint32_t)(((uint64_t)source_rate << 16u) /
                       RETRO_GO_AUDIO_RATE);

        if (measured_q16 > maximum_step_q16)
        {
            measured_q16 = maximum_step_q16;
        }
        first_measurement = !continuity.measured;
        if (first_measurement)
        {
            continuity.feedforward_q16 = measured_q16;
        }
        else if (measured_q16 < continuity.feedforward_q16)
        {
            continuity.feedforward_q16 = (uint32_t)(
                (int32_t)continuity.feedforward_q16 +
                ((int32_t)measured_q16 -
                 (int32_t)continuity.feedforward_q16) / 2);
        }
        else
        {
            continuity.feedforward_q16 = (uint32_t)(
                (int32_t)continuity.feedforward_q16 +
                ((int32_t)measured_q16 -
                 (int32_t)continuity.feedforward_q16) / 4);
        }
        continuity.measured = true;
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

    error = (int32_t)available -
            (int32_t)BSP_RETRO_GO_AUDIO_TARGET_SAMPLES;
    correction_q16 = (int32_t)(((int64_t)error *
        correction_limit_q16) /
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
        (available < RETRO_GO_TDM_FIFO_MONO_FRAMES * 2u &&
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

#ifdef BSP_RETRO_GO_AUDIO_WSOLA
static void wsola_copy_source(int16_t *destination,
                              uint32_t source_index,
                              uint32_t sample_count)
{
    uint32_t first = sample_count;
    uint32_t offset = source_index & RETRO_GO_AUDIO_RING_MASK;

    if (first > RETRO_GO_AUDIO_RING_SAMPLES - offset)
    {
        first = RETRO_GO_AUDIO_RING_SAMPLES - offset;
    }
    memcpy(destination, &audio_ring[offset], first * sizeof(destination[0]));
    if (sample_count > first)
    {
        memcpy(destination + first, audio_ring,
               (sample_count - first) * sizeof(destination[0]));
    }
}

static bool wsola_prepare_initial_grain(void)
{
    uint32_t start = audio_read_index;

    if (audio_write_index - start < RETRO_GO_WSOLA_GRAIN_SAMPLES)
    {
        return false;
    }
    wsola_copy_source(wsola.current_grain, start,
                      RETRO_GO_WSOLA_GRAIN_SAMPLES);
    wsola.source_position_q16 = (uint64_t)start << 16u;
    wsola.current_start = start;
    wsola.next_start = start;
    wsola.hop_position = 0u;
    wsola.first_hop = true;
    wsola.next_ready = false;
    wsola.initialized = true;
    return true;
}

static bool wsola_prepare_next_grain(void)
{
    uint64_t desired_q16 = wsola.source_position_q16 +
        (uint64_t)continuity.step_q16 * RETRO_GO_WSOLA_HOP_SAMPLES;
    uint32_t desired_start = (uint32_t)(desired_q16 >> 16u);
    uint32_t search = continuity.step_q16 == RETRO_GO_AUDIO_Q16_ONE ?
        0u : RETRO_GO_WSOLA_SEARCH_SAMPLES;
    uint32_t maximum_start;
    uint32_t lower;
    uint32_t upper;
    uint32_t best_start;
    uint32_t best_score = UINT32_MAX;
    uint32_t candidate;

    if (audio_write_index - wsola.current_start <
        RETRO_GO_WSOLA_GRAIN_SAMPLES)
    {
        return false;
    }
    maximum_start = audio_write_index - RETRO_GO_WSOLA_GRAIN_SAMPLES;
    lower = desired_start > search ? desired_start - search : 0u;
    if (lower < wsola.current_start)
    {
        lower = wsola.current_start;
    }
    upper = desired_start + search;
    if (upper > maximum_start)
    {
        upper = maximum_start;
    }
    if (lower > upper)
    {
        return false;
    }
    best_start = lower;
    for (candidate = lower; candidate <= upper; ++candidate)
    {
        uint32_t score = 0u;
        uint32_t sample;

        for (sample = 0u; sample < RETRO_GO_WSOLA_HOP_SAMPLES;
             sample += RETRO_GO_WSOLA_SCORE_STRIDE)
        {
            int32_t difference =
                (int32_t)wsola.current_grain[
                    RETRO_GO_WSOLA_HOP_SAMPLES + sample] -
                (int32_t)audio_ring[
                    (candidate + sample) & RETRO_GO_AUDIO_RING_MASK];

            score += (uint32_t)(difference < 0 ?
                                -difference : difference);
        }
        if (score < best_score ||
            (score == best_score &&
             (candidate > desired_start ? candidate - desired_start :
                                           desired_start - candidate) <
             (best_start > desired_start ? best_start - desired_start :
                                            desired_start - best_start)))
        {
            best_score = score;
            best_start = candidate;
        }
    }
    wsola_copy_source(wsola.next_grain, best_start,
                      RETRO_GO_WSOLA_GRAIN_SAMPLES);
    wsola.next_start = best_start;
    /* Keep the long-term analysis clock independent from the local waveform
     * match. Accumulating best_start's +/- search correction makes the
     * effective consumption rate random-walk away from STR, which is visible
     * as a full source ring followed by a sudden drain and PLC burst. */
    wsola.source_position_q16 = desired_q16;
    audio_read_index = best_start;
    __DMB();
    wsola.next_ready = true;
    return true;
}

static bool wsola_next_sample(int16_t *sample)
{
    uint32_t position;

    if (!wsola.initialized && !wsola_prepare_initial_grain())
    {
        return false;
    }
    if (wsola.first_hop)
    {
        *sample = wsola.current_grain[wsola.hop_position++];
        if (wsola.hop_position == RETRO_GO_WSOLA_HOP_SAMPLES)
        {
            wsola.hop_position = 0u;
            wsola.first_hop = false;
        }
        return true;
    }
    if (!wsola.next_ready && !wsola_prepare_next_grain())
    {
        return false;
    }
    position = wsola.hop_position;
    *sample = (int16_t)(
        ((int32_t)wsola.current_grain[
            RETRO_GO_WSOLA_HOP_SAMPLES + position] *
             (int32_t)(RETRO_GO_WSOLA_HOP_SAMPLES - position) +
         (int32_t)wsola.next_grain[position] * (int32_t)position) /
        (int32_t)RETRO_GO_WSOLA_HOP_SAMPLES);
    ++wsola.hop_position;
    if (wsola.hop_position == RETRO_GO_WSOLA_HOP_SAMPLES)
    {
        memcpy(wsola.current_grain, wsola.next_grain,
               sizeof(wsola.current_grain));
        wsola.current_start = wsola.next_start;
        wsola.hop_position = 0u;
        wsola.next_ready = false;
    }
    return true;
}

static void wsola_fill_output_ring(void)
{
    rt_base_t level;
    uint32_t write_snapshot;
    uint32_t read_snapshot;
    uint32_t local_write;
    uint32_t buffered;

    level = rt_hw_interrupt_disable();
    write_snapshot = wsola_output_write_index;
    read_snapshot = wsola_output_read_index;
    rt_hw_interrupt_enable(level);
    local_write = write_snapshot;
    buffered = write_snapshot - read_snapshot;
    while (buffered < (uint32_t)BSP_RETRO_GO_AUDIO_OUTPUT_TARGET_SAMPLES &&
           buffered < RETRO_GO_WSOLA_OUTPUT_RING_SAMPLES)
    {
        int16_t sample;

        if (!wsola_next_sample(&sample))
        {
            break;
        }
        wsola_output_ring[local_write &
                          RETRO_GO_WSOLA_OUTPUT_RING_MASK] = sample;
        ++local_write;
        ++buffered;
    }
    if (local_write != write_snapshot)
    {
        __DMB();
        wsola_output_write_index = local_write;
    }
}
#endif

#ifndef BSP_RETRO_GO_AUDIO_WSOLA
static int16_t continuity_next_sample(void)
{
    const uint32_t fade_samples =
        (RETRO_GO_AUDIO_RATE * BSP_RETRO_GO_AUDIO_CONCEAL_FADE_MS) / 1000u;
    const int32_t fade_step =
        (32767 + (int32_t)(fade_samples != 0u ? fade_samples : 1u) - 1) /
        (int32_t)(fade_samples != 0u ? fade_samples : 1u);
    uint32_t available = audio_write_index - audio_read_index;

    if (available >= 2u)
    {
        uint32_t read_index = audio_read_index;
        int32_t first = audio_ring[read_index & RETRO_GO_AUDIO_RING_MASK];
        int32_t second =
            audio_ring[(read_index + 1u) & RETRO_GO_AUDIO_RING_MASK];
        int32_t sample = first + (int32_t)(
            ((int64_t)(second - first) *
             (int32_t)(continuity.phase_q16 & 0xffffu)) >> 16u);
        uint32_t advance;

        continuity.phase_q16 += continuity.step_q16;
        advance = continuity.phase_q16 >> 16u;
        if (advance >= available)
        {
            advance = available - 1u;
        }
        audio_read_index += advance;
        __DMB();
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
        return continuity.gain_q15 == 32767 ? (int16_t)sample :
            (int16_t)(sample * continuity.gain_q15 / 32767);
    }

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
    ++continuity.concealed_samples;
    return (int16_t)((int32_t)continuity.last_source_sample *
                     continuity.gain_q15 / 32767);
}
#else
static int16_t continuity_next_sample(void)
{
    const uint32_t fade_samples =
        (RETRO_GO_AUDIO_RATE * BSP_RETRO_GO_AUDIO_CONCEAL_FADE_MS) / 1000u;
    const int32_t fade_step =
        (32767 + (int32_t)(fade_samples != 0u ? fade_samples : 1u) - 1) /
        (int32_t)(fade_samples != 0u ? fade_samples : 1u);

    if (wsola_output_write_index - wsola_output_read_index != 0u)
    {
        int16_t sample = wsola_output_ring[
            wsola_output_read_index & RETRO_GO_WSOLA_OUTPUT_RING_MASK];

        ++wsola_output_read_index;
        __DMB();
        continuity.last_source_sample = sample;
        continuity.starving = false;
        if (continuity.gain_q15 < 32767)
        {
            continuity.gain_q15 += fade_step;
            if (continuity.gain_q15 > 32767)
            {
                continuity.gain_q15 = 32767;
            }
        }
        return continuity.gain_q15 == 32767 ? sample :
            (int16_t)((int32_t)sample * continuity.gain_q15 / 32767);
    }
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
    ++continuity.concealed_samples;
    return (int16_t)((int32_t)continuity.last_source_sample *
                     continuity.gain_q15 / 32767);
}
#endif

static void pdl_write_mono_frames(uint32_t frame_count)
{
    uint32_t frame;

    for (frame = 0u; frame < frame_count; ++frame)
    {
        uint16_t sample = (uint16_t)continuity_next_sample();

        /* Application contract is one s16 mono sample. The generated TDM
         * link is two 16-bit slots, so place the exact same sample in L/R. */
        Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, sample);
        Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, sample);
    }
    audio_output_index += frame_count;
}

static void retro_go_audio_tdm_isr(void)
{
    uint32_t status;

    rt_interrupt_enter();
    status = Cy_AudioTDM_GetTxInterruptStatusMasked(TDM_STRUCT0_TX);
    if ((status & CY_TDM_INTR_TX_FIFO_TRIGGER) != 0u && audio_hw_running)
    {
        pdl_write_mono_frames(RETRO_GO_TDM_FIFO_MONO_FRAMES);
    }
    if ((status & (CY_TDM_INTR_TX_FIFO_UNDERFLOW |
                   CY_TDM_INTR_TX_IF_UNDERFLOW)) != 0u)
    {
        ++audio_hardware_underflows;
    }
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, status);
    rt_interrupt_leave();
}

static bool verify_generated_tdm_contract(void)
{
    const cy_stc_tdm_config_tx_t *tx = pdl_tdm_config.tx_config;

    if (tx == NULL || !tx->enable ||
        tx->masterMode != CY_TDM_DEVICE_MASTER ||
        tx->format != CY_TDM_LEFT_DELAYED ||
        tx->clkSel != CY_TDM_SEL_SRSS_CLK0 ||
        tx->channelNum != 2u ||
        tx->channelSize != 16u || tx->wordSize != CY_TDM_SIZE_16 ||
        tx->chEn != 0x3u || tx->fifoTriggerLevel != 64u ||
        tx->clkDiv != 8u || !tx->i2sMode)
    {
        rt_kprintf("[retro-go] generated TDM contract mismatch\n");
        return false;
    }
    return true;
}

static void prepare_pdl_tdm_config(void)
{
    pdl_tdm_tx_config = CYBSP_TDM_CONTROLLER_0_tx_config;
    pdl_tdm_tx_config.clkDiv = 8u;
    pdl_tdm_config = CYBSP_TDM_CONTROLLER_0_config;
    pdl_tdm_config.tx_config = &pdl_tdm_tx_config;
}

static bool configure_exact_i2s_clock(void)
{
    const en_clk_dst_t divider_group =
        (en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM;
    uint32_t readback[2] __attribute__((aligned(32))) = {0u, 0u};
    uint32_t mclk_hz;
    uint32_t bclk_hz;
    uint32_t lrck_hz;
    cy_en_sysclk_status_t status;

    status = Cy_SysClk_PeriPclkDisableDivider(
        divider_group, CY_SYSCLK_DIV_16_5_BIT, 0u);
    if (status == CY_SYSCLK_SUCCESS)
    {
        status = Cy_SysClk_PeriPclkSetFracDivider(
            divider_group, CY_SYSCLK_DIV_16_5_BIT, 0u, 11u, 0u);
    }
    if (status == CY_SYSCLK_SUCCESS)
    {
        status = Cy_SysClk_PeriPclkEnableDivider(
            divider_group, CY_SYSCLK_DIV_16_5_BIT, 0u);
    }
    if (status != CY_SYSCLK_SUCCESS)
    {
        return false;
    }
    Cy_SysClk_PeriPclkGetFracDivider(
        divider_group, CY_SYSCLK_DIV_16_5_BIT, 0u,
        &readback[0], &readback[1]);
    mclk_hz = Cy_SysClk_PeriPclkGetFrequency(
        divider_group, CY_SYSCLK_DIV_16_5_BIT, 0u);
    bclk_hz = mclk_hz / pdl_tdm_tx_config.clkDiv;
    lrck_hz = bclk_hz /
        ((uint32_t)pdl_tdm_tx_config.channelNum *
         (uint32_t)pdl_tdm_tx_config.channelSize);
    rt_kprintf("[retro-go] PDL I2S clock: divider=%u+%u/32 (/12), "
               "MCLK=%u Hz BCLK=%u Hz LRCK=%u Hz\n",
               (unsigned)readback[0], (unsigned)readback[1],
               (unsigned)mclk_hz, (unsigned)bclk_hz, (unsigned)lrck_hz);
    return readback[0] == 11u && readback[1] == 0u &&
           mclk_hz == 4096000u && bclk_hz == 512000u &&
           lrck_hz == RETRO_GO_AUDIO_RATE;
}

static bool pdl_audio_start(void)
{
    rt_base_t level;

    if (audio_hw_running)
    {
        return true;
    }
    level = rt_hw_interrupt_disable();
    audio_hw_running = true;
    /* Keep the trigger masked while the empty FIFO is enabled and primed.
     * Otherwise an empty-FIFO edge can remain pending and cause the ISR to
     * append another 64 words after the 128-word startup fill. */
    Cy_AudioTDM_SetTxInterruptMask(TDM_STRUCT0_TX, 0u);
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    NVIC_ClearPendingIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);
    Cy_AudioTDM_EnableTx(TDM_STRUCT0_TX);
    pdl_write_mono_frames(RETRO_GO_TDM_STARTUP_MONO_FRAMES);
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    NVIC_ClearPendingIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);
    Cy_AudioTDM_SetTxInterruptMask(
        TDM_STRUCT0_TX,
        CY_TDM_INTR_TX_FIFO_TRIGGER | CY_TDM_INTR_TX_FIFO_UNDERFLOW |
        CY_TDM_INTR_TX_IF_UNDERFLOW);
    Cy_AudioTDM_ActivateTx(TDM_STRUCT0_TX);
    rt_hw_interrupt_enable(level);
    return true;
}

bool retro_go_audio_init(void)
{
    cy_stc_sysint_t interrupt_config =
    {
        .intrSrc = CYBSP_TDM_CONTROLLER_0_TX_IRQ,
        .intrPriority = 2u,
    };

    if (audio_ready)
    {
        return true;
    }
    prepare_pdl_tdm_config();
    if (!verify_generated_tdm_contract() || !configure_exact_i2s_clock())
    {
        return false;
    }
    if (Cy_SysInt_Init(&interrupt_config, retro_go_audio_tdm_isr) !=
        CY_SYSINT_SUCCESS)
    {
        rt_kprintf("[retro-go] PDL TDM interrupt setup failed\n");
        return false;
    }
    if (Cy_AudioTDM_Init(TDM_STRUCT0, &pdl_tdm_config) !=
        CY_TDM_SUCCESS)
    {
        rt_kprintf("[retro-go] PDL TDM init failed\n");
        return false;
    }
    Cy_AudioTDM_SetTxInterruptMask(TDM_STRUCT0_TX, 0u);
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    NVIC_ClearPendingIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);
    NVIC_EnableIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);

    if (es8388_init("i2c0", RT_NULL) != RT_EOK)
    {
        NVIC_DisableIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);
        Cy_AudioTDM_DeInit(TDM_STRUCT0);
        rt_kprintf("[retro-go] ES8388 direct-PDL init failed\n");
        return false;
    }
    if (es8388_start(ES_MODE_DAC) != RT_EOK)
    {
        (void)es8388_stop(ES_MODE_DAC);
        NVIC_DisableIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);
        Cy_AudioTDM_DeInit(TDM_STRUCT0);
        rt_kprintf("[retro-go] ES8388 direct-PDL start failed\n");
        return false;
    }
    es8388_volume_set(70u);

    audio_write_index = 0u;
    audio_read_index = 0u;
    audio_source_index = 0u;
    audio_output_index = 0u;
    audio_hardware_underflows = 0u;
    audio_overflow_reported = false;
    audio_playback_started = false;
    audio_hw_running = false;
    memset(&continuity, 0, sizeof(continuity));
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
    memset(&wsola, 0, sizeof(wsola));
    wsola_output_write_index = 0u;
    wsola_output_read_index = 0u;
#endif
    continuity.phase_q16 = 0u;
    continuity.step_q16 = RETRO_GO_AUDIO_Q16_ONE;
    continuity.feedforward_q16 = RETRO_GO_AUDIO_Q16_ONE;
    continuity.gain_q15 = 32767;
    continuity.control_started_ms = rt_tick_get_millisecond();
    audio_ready = true;
    rt_kprintf("[retro-go] PDL audio ready: input channels=1 mono s16, "
               "TDM=2x16 dual-mono MCLK=256fs, FIFO=%u frames, "
               "ring=%u, preroll=%u"
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
               ", WSOLA=%u/%u search=+-%u out-target=%u"
#endif
               "\n",
               RETRO_GO_TDM_FIFO_MONO_FRAMES,
               RETRO_GO_AUDIO_RING_SAMPLES,
               BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
               , RETRO_GO_WSOLA_GRAIN_SAMPLES,
               RETRO_GO_WSOLA_HOP_SAMPLES,
               RETRO_GO_WSOLA_SEARCH_SAMPLES,
               BSP_RETRO_GO_AUDIO_OUTPUT_TARGET_SAMPLES
#endif
               );
    return true;
}

void retro_go_audio_submit(const int16_t *samples, size_t sample_count)
{
    rt_base_t level;
    uint32_t write_snapshot;
    uint32_t read_snapshot;
    uint32_t used;
    size_t free_samples;
    size_t accepted;
    size_t first;
    bool start_playback = false;
    bool report_overflow = false;

    if (!audio_ready || samples == NULL || sample_count == 0u)
    {
        return;
    }
    level = rt_hw_interrupt_disable();
    write_snapshot = audio_write_index;
    read_snapshot = audio_read_index;
    rt_hw_interrupt_enable(level);
    used = write_snapshot - read_snapshot;
    free_samples = RETRO_GO_AUDIO_RING_SAMPLES - used;
    accepted = sample_count < free_samples ? sample_count : free_samples;
    first = accepted;
    if (first > RETRO_GO_AUDIO_RING_SAMPLES -
                    (write_snapshot & RETRO_GO_AUDIO_RING_MASK))
    {
        first = RETRO_GO_AUDIO_RING_SAMPLES -
                (write_snapshot & RETRO_GO_AUDIO_RING_MASK);
    }
    if (first != 0u)
    {
        memcpy(&audio_ring[write_snapshot & RETRO_GO_AUDIO_RING_MASK],
               samples, first * sizeof(samples[0]));
    }
    if (accepted > first)
    {
        memcpy(audio_ring, samples + first,
               (accepted - first) * sizeof(samples[0]));
    }
    __DMB();
    level = rt_hw_interrupt_disable();
    if (audio_source_index == 0u)
    {
        continuity.control_started_ms = rt_tick_get_millisecond();
        continuity.control_source_index = (uint32_t)sample_count;
        continuity.control_output_index = 0u;
    }
    audio_source_index += (uint32_t)sample_count;
    audio_write_index = write_snapshot + (uint32_t)accepted;
#ifndef BSP_RETRO_GO_AUDIO_WSOLA
    if (!audio_playback_started &&
        (audio_write_index - audio_read_index) >=
            BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES)
    {
        audio_playback_started = true;
        start_playback = true;
    }
#endif
    if (accepted != sample_count && !audio_overflow_reported)
    {
        audio_overflow_reported = true;
        report_overflow = true;
    }
    rt_hw_interrupt_enable(level);

    continuity_update_control();
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
    wsola_fill_output_ring();
    level = rt_hw_interrupt_disable();
    if (!audio_playback_started &&
        (wsola_output_write_index - wsola_output_read_index) >=
            BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES)
    {
        audio_playback_started = true;
        start_playback = true;
    }
    rt_hw_interrupt_enable(level);
#endif
    if (start_playback)
    {
        (void)pdl_audio_start();
    }
    if (report_overflow)
    {
        rt_kprintf("[retro-go] PDL audio ring overrun; dropped=%u samples\n",
                   (unsigned)(sample_count - accepted));
    }
}

size_t retro_go_audio_buffered_samples(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
    size_t result = audio_ready ?
        (size_t)(wsola_output_write_index - wsola_output_read_index) : 0u;
#else
    size_t result = audio_ready ?
        (size_t)(audio_write_index - audio_read_index) : 0u;
#endif

    rt_hw_interrupt_enable(level);
    return result;
}

uint32_t retro_go_audio_submitted_samples(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
    uint32_t result = wsola_output_write_index;
#else
    uint32_t result = audio_write_index;
#endif

    rt_hw_interrupt_enable(level);
    return result;
}

void retro_go_audio_get_stats(retro_go_audio_stats_t *stats)
{
    rt_base_t level;
    uint32_t available;
    uint32_t step_q16;
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
    uint32_t output_available;
#endif

    if (stats == NULL)
    {
        return;
    }
    memset(stats, 0, sizeof(*stats));
    level = rt_hw_interrupt_disable();
    available = audio_ready ? audio_write_index - audio_read_index : 0u;
    step_q16 = continuity.step_q16 != 0u ?
        continuity.step_q16 : RETRO_GO_AUDIO_Q16_ONE;
    stats->source_buffer_samples = available;
    stats->input_rate_hz = continuity.input_rate_hz;
    stats->output_rate_hz = continuity.output_rate_hz;
    stats->concealed_samples = continuity.concealed_samples;
    stats->underrun_events = continuity.underrun_events;
    stats->hardware_underflows = audio_hardware_underflows;
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
    output_available = wsola_output_write_index - wsola_output_read_index;
    stats->output_buffer_samples = output_available;
#endif
    rt_hw_interrupt_enable(level);
    stats->stretch_permille =
        (uint32_t)(((uint64_t)step_q16 * 1000u) >> 16u);
    stats->source_buffer_ms = step_q16 != 0u ?
        (uint32_t)(((uint64_t)available * 1000u *
                    RETRO_GO_AUDIO_Q16_ONE) /
                   ((uint64_t)RETRO_GO_AUDIO_RATE * step_q16)) : 0u;
#ifdef BSP_RETRO_GO_AUDIO_WSOLA
    stats->output_buffer_ms =
        (uint32_t)(((uint64_t)output_available * 1000u) /
                   RETRO_GO_AUDIO_RATE);
#endif
}

void retro_go_audio_deinit(void)
{
    rt_base_t level;
    bool was_running;

    if (!audio_ready)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    was_running = audio_hw_running;
    audio_ready = false;
    audio_hw_running = false;
    Cy_AudioTDM_SetTxInterruptMask(TDM_STRUCT0_TX, 0u);
    NVIC_DisableIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);
    NVIC_ClearPendingIRQ(CYBSP_TDM_CONTROLLER_0_TX_IRQ);
    rt_hw_interrupt_enable(level);
    if (was_running)
    {
        Cy_AudioTDM_DeActivateTx(TDM_STRUCT0_TX);
        Cy_AudioTDM_DisableTx(TDM_STRUCT0_TX);
    }
    audio_playback_started = false;
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    Cy_AudioTDM_DeInit(TDM_STRUCT0);
    (void)es8388_stop(ES_MODE_DAC);
}
