#include "retro_go_gba.h"

#include "retro_go_audio.h"
#include "retro_go_display.h"
#include "retro_go_input.h"
#include "retro_go_perf.h"
#include "retro_go_platform.h"
#include "retro_go_storage.h"
#include "retro_go_time.h"
#include "retro_go_gpsp_prelude.h"
#ifdef BSP_RETRO_GO_GBA_DEEP_PROFILE
#include "retro_go_gpsp_profile.h"
#endif

#include "common.h"
#include "cpu.h"
#include "gba_memory.h"
#include "input.h"
#include "main.h"
#include "savestate.h"
#include "sound.h"
#include "video.h"

#include <rtthread.h>

#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 1)
#include <arm_mve.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RETRO_GO_GBA_FRAME_US 16743u
#ifndef BSP_RETRO_GO_GBA_LATE_SKIP_US
#define BSP_RETRO_GO_GBA_LATE_SKIP_US 6000u
#endif
#ifndef BSP_RETRO_GO_GBA_MIN_DRAW_FRAMES_AFTER_SKIP
#define BSP_RETRO_GO_GBA_MIN_DRAW_FRAMES_AFTER_SKIP 2u
#endif
#define RETRO_GO_GBA_NATIVE_AUDIO_RATE GBA_SOUND_FREQUENCY
#define RETRO_GO_GBA_OUTPUT_AUDIO_RATE 16000u
#define RETRO_GO_GBA_AUDIO_READ_FRAMES 512u
#define RETRO_GO_GBA_AUDIO_BLOCK_SAMPLES 256u
#define RETRO_GO_GBA_STATE_BYTES (416u * 1024u)
#define RETRO_GO_GBA_FIR_TAPS 24u

#ifndef BSP_RETRO_GO_AUTOSAVE_SECONDS
#define BSP_RETRO_GO_AUTOSAVE_SECONDS 10
#endif

#ifndef BSP_RETRO_GO_MAX_FRAME_SKIP
#define BSP_RETRO_GO_MAX_FRAME_SKIP 2
#endif

#ifndef BSP_RETRO_GO_GBA_MAX_FRAME_SKIP
#define BSP_RETRO_GO_GBA_MAX_FRAME_SKIP BSP_RETRO_GO_MAX_FRAME_SKIP
#endif

#ifndef BSP_RETRO_GO_DISPLAY_YIELD_MS
#define BSP_RETRO_GO_DISPLAY_YIELD_MS 2
#endif

int dynarec_enable = 0;
boot_mode selected_boot_mode = boot_game;
int sprite_limit = 1;
u32 idle_loop_target_pc = 0xffffffffu;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets;
u32 skip_next_frame;
u32 num_skipped_frames;
u32 netplay_num_clients;
u32 netplay_client_id;

extern u32 gamepak_buffer_count;
extern u8 *gamepak_buffers[32];

static uint16_t gba_buttons;
static int16_t stereo_frames[RETRO_GO_GBA_AUDIO_READ_FRAMES * 2u]
    RETRO_GO_SOCMEM;
static int16_t native_mono[RETRO_GO_GBA_AUDIO_READ_FRAMES]
    RETRO_GO_DTCM;
static int16_t mono_block[RETRO_GO_GBA_AUDIO_BLOCK_SAMPLES]
    RETRO_GO_DTCM;
static size_t mono_block_count;
static uint32_t resample_phase;
static int16_t resample_previous;
static bool resample_previous_valid;
#ifdef BSP_RETRO_GO_GBA_PERF_STATS
static uint32_t gba_audio_peak RETRO_GO_SOCMEM;
static uint32_t gba_audio_hot_samples RETRO_GO_SOCMEM;
#endif
#ifdef BSP_RETRO_GO_GBA_FIR_RESAMPLER
static int16_t fir_history[RETRO_GO_GBA_FIR_TAPS] RETRO_GO_DTCM;
static unsigned fir_history_index;
static const int16_t fir_coefficients[RETRO_GO_GBA_FIR_TAPS] =
{
    -14, 90, 86, -202, -369, 305, 1048, -101,
    -2377, -1154, 5853, 13220, 13218, 5853, -1154, -2377,
    -101, 1048, 305, -369, -202, 86, 90, -14,
};
#endif
static uint32_t saved_signature;

static uint32_t backup_signature(void)
{
    uint32_t signature = 2166136261u;
    size_t index;

    for (index = 0u; index < sizeof(gamepak_backup); ++index)
    {
        signature ^= gamepak_backup[index];
        signature *= 16777619u;
    }
    return signature;
}

static void gpsp_set_buttons(uint16_t buttons)
{
    gba_buttons = buttons;
}

u32 update_input(void)
{
    u16 p1cnt;

    write_ioreg(REG_P1, (~gba_buttons) & 0x3ffu);
    p1cnt = read_ioreg(REG_P1CNT);
    if ((p1cnt & 0x4000u) != 0u)
    {
        u16 key_mask = p1cnt & 0x3ffu;
        u16 pressed = gba_buttons & key_mask;

        if (((p1cnt & 0x8000u) != 0u && pressed == key_mask) ||
            ((p1cnt & 0x8000u) == 0u && pressed != 0u))
        {
            flag_interrupt(IRQ_KEYPAD);
        }
    }
    return 0u;
}

static bool gpsp_init_core(void)
{
    init_gamepak_buffer();
    if (gamepak_buffer_count == 0u)
    {
        return false;
    }
    init_sound();
    cheat_clear();
    gba_screen_pixels = retro_go_display_gba_framebuffer();
    return gba_screen_pixels != NULL;
}

static int gpsp_load_rom(const char *rom_path)
{
    extern u8 open_gba_bios_rom[];

    memcpy(bios_rom, open_gba_bios_rom, 16u * 1024u);
    memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
    if (load_gamepak(NULL, rom_path, FEAT_AUTODETECT, FEAT_DISABLE,
                     SERIAL_MODE_AUTO) != 0u)
    {
        return -1;
    }
    reset_gba();
    return 0;
}

static uint16_t *gpsp_run_frame(bool draw)
{
    skip_next_frame = draw ? 0u : 1u;
    update_input();
    clear_gamepak_stickybits();
    execute_arm(execute_cycles);
    return gba_screen_pixels;
}

static int gpsp_load_save(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL)
    {
        return -1;
    }
    memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
    (void)fread(gamepak_backup, 1u, sizeof(gamepak_backup), file);
    fclose(file);
    return 0;
}

static int gpsp_write_save(const char *path)
{
    char temporary[RETRO_GO_ROM_PATH_CAPACITY + 5u];
    uint32_t signature = backup_signature();
    int temporary_length;
    FILE *file;
    size_t written;

    if (signature == saved_signature)
    {
        return 0;
    }
    temporary_length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (temporary_length <= 0 ||
        (size_t)temporary_length >= sizeof(temporary))
    {
        return -1;
    }
    file = fopen(temporary, "wb");
    if (file == NULL)
    {
        return -1;
    }
    written = fwrite(gamepak_backup, 1u, sizeof(gamepak_backup), file);
    fclose(file);
    if (written != sizeof(gamepak_backup))
    {
        (void)remove(temporary);
        return -1;
    }
    if (rename(temporary, path) != 0)
    {
        (void)remove(path);
        if (rename(temporary, path) != 0)
        {
            (void)remove(temporary);
            return -1;
        }
    }
    saved_signature = signature;
    return 0;
}

static void gpsp_deinit_core(void)
{
    memory_term();
    gba_screen_pixels = NULL;
}

static uint16_t buttons_to_gba(uint32_t buttons)
{
    uint16_t result = 0u;

    if ((buttons & RETRO_GO_BUTTON_A) != 0u) result |= 1u << 0;
    if ((buttons & RETRO_GO_BUTTON_B) != 0u) result |= 1u << 1;
    if ((buttons & RETRO_GO_BUTTON_SELECT) != 0u) result |= 1u << 2;
    if ((buttons & RETRO_GO_BUTTON_START) != 0u) result |= 1u << 3;
    if ((buttons & RETRO_GO_BUTTON_RIGHT) != 0u) result |= 1u << 4;
    if ((buttons & RETRO_GO_BUTTON_LEFT) != 0u) result |= 1u << 5;
    if ((buttons & RETRO_GO_BUTTON_UP) != 0u) result |= 1u << 6;
    if ((buttons & RETRO_GO_BUTTON_DOWN) != 0u) result |= 1u << 7;
    if ((buttons & RETRO_GO_BUTTON_R) != 0u) result |= 1u << 8;
    if ((buttons & RETRO_GO_BUTTON_L) != 0u) result |= 1u << 9;
    return result;
}

static void submit_mono_sample(int16_t sample)
{
    mono_block[mono_block_count++] = sample;
    if (mono_block_count == RETRO_GO_GBA_AUDIO_BLOCK_SAMPLES)
    {
        retro_go_audio_submit(mono_block, mono_block_count);
        mono_block_count = 0u;
    }
}

static void downmix_stereo(unsigned frames)
{
    unsigned frame = 0u;

#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 1)
    for (; frame + 8u <= frames; frame += 8u)
    {
        int16x8x2_t stereo = vld2q_s16(&stereo_frames[frame * 2u]);
        int16x8_t mono = vhaddq_s16(stereo.val[0], stereo.val[1]);

        vst1q_s16(&native_mono[frame], mono);
    }
#endif
    for (; frame < frames; ++frame)
    {
        int32_t left = stereo_frames[frame * 2u];
        int32_t right = stereo_frames[frame * 2u + 1u];

        native_mono[frame] = (int16_t)((left + right) >> 1);
    }
}

static int16_t lowpass_mono_sample(int16_t sample)
{
#ifdef BSP_RETRO_GO_GBA_FIR_RESAMPLER
    int32_t accumulator = 0;
    unsigned tap;

    fir_history[fir_history_index] = sample;
    fir_history_index = (fir_history_index + 1u) % RETRO_GO_GBA_FIR_TAPS;
    for (tap = 0u; tap < RETRO_GO_GBA_FIR_TAPS; ++tap)
    {
        unsigned history = (fir_history_index + tap) %
                           RETRO_GO_GBA_FIR_TAPS;

        accumulator += (int32_t)fir_history[history] *
                       fir_coefficients[tap];
    }
    accumulator = (accumulator +
                   (accumulator >= 0 ? 16384 : -16384)) / 32768;
    if (accumulator > INT16_MAX) accumulator = INT16_MAX;
    if (accumulator < INT16_MIN) accumulator = INT16_MIN;
    return (int16_t)accumulator;
#else
    return sample;
#endif
}

static void resample_mono_sample(int16_t current)
{
#if RETRO_GO_GBA_NATIVE_AUDIO_RATE == RETRO_GO_GBA_OUTPUT_AUDIO_RATE
    submit_mono_sample(current);
#else
    uint32_t previous_phase;

    if (!resample_previous_valid)
    {
        resample_previous = current;
        resample_previous_valid = true;
        submit_mono_sample(current);
        return;
    }
    previous_phase = resample_phase;
    resample_phase += RETRO_GO_GBA_OUTPUT_AUDIO_RATE;
    if (resample_phase >= RETRO_GO_GBA_NATIVE_AUDIO_RATE)
    {
        uint32_t distance = RETRO_GO_GBA_NATIVE_AUDIO_RATE - previous_phase;
        uint32_t fraction_q16 = (uint32_t)(
            ((uint32_t)distance << 16u) /
            RETRO_GO_GBA_OUTPUT_AUDIO_RATE);
        int32_t interpolated = resample_previous + (int32_t)(
            ((int64_t)(current - resample_previous) * fraction_q16) >> 16u);

        if (interpolated > INT16_MAX) interpolated = INT16_MAX;
        if (interpolated < INT16_MIN) interpolated = INT16_MIN;
        submit_mono_sample((int16_t)interpolated);
        resample_phase -= RETRO_GO_GBA_NATIVE_AUDIO_RATE;
    }
    resample_previous = current;
#endif
}

static void drain_audio(void)
{
    unsigned frames;

    do
    {
        unsigned frame;

        frames = sound_read_samples(stereo_frames,
                                    RETRO_GO_GBA_AUDIO_READ_FRAMES);
        downmix_stereo(frames);
        for (frame = 0u; frame < frames; ++frame)
        {
#ifdef BSP_RETRO_GO_GBA_PERF_STATS
            int32_t magnitude = native_mono[frame] >= 0 ?
                native_mono[frame] : -(int32_t)native_mono[frame];

            if ((uint32_t)magnitude > gba_audio_peak)
            {
                gba_audio_peak = (uint32_t)magnitude;
            }
            if (magnitude >= 23000)
            {
                ++gba_audio_hot_samples;
            }
#endif
            resample_mono_sample(lowpass_mono_sample(native_mono[frame]));
        }
    } while (frames == RETRO_GO_GBA_AUDIO_READ_FRAMES);
}

static bool save_state(const char *path, void **state_buffer)
{
    FILE *file;

    if (*state_buffer == NULL)
    {
        *state_buffer = malloc(RETRO_GO_GBA_STATE_BYTES);
    }
    if (*state_buffer == NULL)
    {
        return false;
    }
    gba_save_state((u8 *)*state_buffer);
    file = fopen(path, "wb");
    if (file == NULL)
    {
        return false;
    }
    bool result = fwrite(*state_buffer, 1u, RETRO_GO_GBA_STATE_BYTES, file) ==
                  RETRO_GO_GBA_STATE_BYTES;
    fclose(file);
    return result;
}

static bool load_state(const char *path, void **state_buffer)
{
    FILE *file;
    bool result;

    if (*state_buffer == NULL)
    {
        *state_buffer = malloc(RETRO_GO_GBA_STATE_BYTES);
    }
    if (*state_buffer == NULL)
    {
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL)
    {
        return false;
    }
    result = fread(*state_buffer, 1u, RETRO_GO_GBA_STATE_BYTES, file) ==
             RETRO_GO_GBA_STATE_BYTES;
    fclose(file);
    return result && gba_load_state((const u8 *)*state_buffer);
}

bool retro_go_gba_is_rom(const char *path)
{
    const char *extension = path != NULL ? strrchr(path, '.') : NULL;

    return extension != NULL && strcasecmp(extension, ".gba") == 0;
}

int retro_go_gba_run(const char *rom_path, const char *sram_path,
                     const char *state_path)
{
    uint32_t next_frame_us;
    uint32_t last_autosave_ms;
    unsigned consecutive_skips = 0u;
    unsigned rendered_since_skip =
        BSP_RETRO_GO_GBA_MIN_DRAW_FRAMES_AFTER_SKIP;
    void *state_buffer = NULL;
    bool quit = false;
    int result = 0;
#ifdef BSP_RETRO_GO_GBA_PERF_STATS
    uint32_t stats_started_ms;
    uint32_t stats_frames = 0u;
    uint32_t stats_drawn = 0u;
    uint32_t stats_skipped = 0u;
    uint32_t stats_max_work_cycles = 0u;
    uint64_t stats_draw_core_cycles = 0u;
    uint64_t stats_skip_core_cycles = 0u;
    retro_go_display_stats_t stats_display_base;
    retro_go_audio_stats_t stats_audio_base;
#ifdef BSP_RETRO_GO_GBA_DEEP_PROFILE
    retro_go_gpsp_profile_stats_t stats_profile_base;
#endif
#endif

    mono_block_count = 0u;
    resample_phase = 0u;
    resample_previous = 0;
    resample_previous_valid = false;
#ifdef BSP_RETRO_GO_GBA_FIR_RESAMPLER
    memset(fir_history, 0, sizeof(fir_history));
    fir_history_index = 0u;
#endif
    if (!gpsp_init_core())
    {
        return -20;
    }
    if (gpsp_load_rom(rom_path) != 0)
    {
        gpsp_deinit_core();
        return -21;
    }
    (void)gpsp_load_save(sram_path);
    saved_signature = backup_signature();
    rt_kprintf("[retro-go] GBA interpreter running %s, ROM cache=%u MiB "
               "blocks=%p..%p\n",
               rom_path, (unsigned)gamepak_buffer_count,
               gamepak_buffer_count != 0u ? gamepak_buffers[0] : NULL,
               gamepak_buffer_count != 0u ?
                   gamepak_buffers[gamepak_buffer_count - 1u] : NULL);
#ifdef BSP_RETRO_GO_GBA_ARM_AL_FASTPATH
    rt_kprintf("[retro-go] GBA M55 ARM cond=AL fast path active\n");
#endif
#ifdef BSP_RETRO_GO_GBA_LAZY_CPSR_WRITEBACK
    rt_kprintf("[retro-go] GBA M55 dirty-guarded CPSR writeback active\n");
#endif
#ifdef BSP_RETRO_GO_GBA_FAST_RAM_WRITE_WRAPPER
    rt_kprintf("[retro-go] GBA M55 fastmem: compact EWRAM/IWRAM "
               "write wrapper active\n");
#endif
    rt_kprintf("[retro-go] GBA audio: native=%u Hz output=%u Hz, "
               "MVE-downmix=%u FIR=%u\n",
               (unsigned)sound_frequency,
               (unsigned)RETRO_GO_GBA_OUTPUT_AUDIO_RATE,
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 1)
               1u,
#else
               0u,
#endif
#ifdef BSP_RETRO_GO_GBA_FIR_RESAMPLER
               1u
#else
               0u
#endif
    );
    rt_kprintf("[retro-go] GBA keys: arrows/WASD, Z/J=A, X/K=B, "
               "Q=L, E=R, Enter=Start, Shift/Backspace=Select, "
               "Esc=save+launcher\n");
    retro_go_perf_begin("GBA", RETRO_GO_GBA_FRAME_US);
#ifdef BSP_RETRO_GO_GBA_DEEP_PROFILE
    retro_go_gpsp_profile_reset();
    retro_go_gpsp_profile_set_active(true);
#endif

    next_frame_us = rt_tick_get_millisecond() * 1000u;
    last_autosave_ms = rt_tick_get_millisecond();
#ifdef BSP_RETRO_GO_GBA_PERF_STATS
    gba_audio_peak = 0u;
    gba_audio_hot_samples = 0u;
    stats_started_ms = last_autosave_ms;
    retro_go_display_get_stats(&stats_display_base);
    retro_go_audio_get_stats(&stats_audio_base);
#ifdef BSP_RETRO_GO_GBA_DEEP_PROFILE
    retro_go_gpsp_profile_snapshot(&stats_profile_base);
#endif
#endif
    while (!quit)
    {
        uint32_t buttons = retro_go_input_buttons_get();
        uint32_t events = retro_go_input_events_take();
        bool fast_forward = retro_go_input_fast_forward_get();
        uint32_t now_us = rt_tick_get_millisecond() * 1000u;
        /* The millisecond RT tick can naturally overshoot the target by up
         * to 1 ms. Beyond a 2 ms slip, skip software rendering immediately
         * instead of waiting until a complete frame of debt accumulates. */
        bool late = (int32_t)(now_us - next_frame_us) >
                    (int32_t)BSP_RETRO_GO_GBA_LATE_SKIP_US;
        bool draw = true;
        uint16_t *framebuffer;
        uint32_t work_started_cycles = retro_go_time_now_cycles();
        uint32_t core_started_cycles;
        uint32_t core_cycles;

        gpsp_set_buttons(buttons_to_gba(buttons));
        if ((events & RETRO_GO_EVENT_SAVE) != 0u)
        {
            rt_kprintf("[retro-go] GBA save state: %d\n",
                       save_state(state_path, &state_buffer) ? 0 : -1);
        }
        if ((events & RETRO_GO_EVENT_LOAD) != 0u)
        {
            rt_kprintf("[retro-go] GBA load state: %d\n",
                       load_state(state_path, &state_buffer) ? 0 : -1);
        }
        if ((events & RETRO_GO_EVENT_RESET) != 0u)
        {
            reset_gba();
        }
        if ((events & RETRO_GO_EVENT_QUIT) != 0u)
        {
            quit = true;
            continue;
        }
        if (BSP_RETRO_GO_GBA_MAX_FRAME_SKIP > 0 &&
            (late || fast_forward) &&
            consecutive_skips <
                (unsigned)BSP_RETRO_GO_GBA_MAX_FRAME_SKIP &&
            rendered_since_skip >=
                (unsigned)BSP_RETRO_GO_GBA_MIN_DRAW_FRAMES_AFTER_SKIP)
        {
            draw = false;
            ++consecutive_skips;
            rendered_since_skip = 0u;
        }
        else
        {
            consecutive_skips = 0u;
            if (rendered_since_skip <
                (unsigned)BSP_RETRO_GO_GBA_MIN_DRAW_FRAMES_AFTER_SKIP)
            {
                ++rendered_since_skip;
            }
        }
        core_started_cycles = retro_go_time_now_cycles();
        framebuffer = gpsp_run_frame(draw);
        core_cycles = retro_go_time_elapsed_cycles(core_started_cycles);
        drain_audio();
        if (draw)
        {
            (void)retro_go_display_present_gba(framebuffer);
        }
        {
            uint32_t work_cycles =
                retro_go_time_elapsed_cycles(work_started_cycles);

            retro_go_perf_frame(draw, work_cycles);

#ifdef BSP_RETRO_GO_GBA_PERF_STATS
            uint32_t stats_now_ms = rt_tick_get_millisecond();
            uint32_t stats_elapsed_ms = stats_now_ms - stats_started_ms;

            ++stats_frames;
            if (draw)
            {
                ++stats_drawn;
                stats_draw_core_cycles += core_cycles;
            }
            else
            {
                ++stats_skipped;
                stats_skip_core_cycles += core_cycles;
            }
            if (work_cycles > stats_max_work_cycles)
            {
                stats_max_work_cycles = work_cycles;
            }
            if (stats_elapsed_ms >= 5000u)
            {
                uint32_t fps_tenths =
                    stats_frames * 10000u / stats_elapsed_ms;
                retro_go_audio_stats_t audio_stats;
                uint32_t draw_core_tenths = stats_drawn != 0u ?
                    retro_go_time_cycles_to_tenths_ms(
                        stats_draw_core_cycles) / stats_drawn : 0u;
                uint32_t skip_core_tenths = stats_skipped != 0u ?
                    retro_go_time_cycles_to_tenths_ms(
                        stats_skip_core_cycles) / stats_skipped : 0u;
                uint32_t max_work_tenths =
                    retro_go_time_cycles_to_tenths_ms(
                        stats_max_work_cycles);
                retro_go_display_stats_t display_stats;
                uint32_t display_completed;
                uint32_t display_tenths;
                uint32_t display_drops;
                uint32_t plc_delta;
                uint32_t concealed_delta;
                uint32_t i2s_underflow_delta;
#ifdef BSP_RETRO_GO_GBA_DEEP_PROFILE
                retro_go_gpsp_profile_stats_t profile_current;
                retro_go_gpsp_profile_stats_t profile_delta;
                uint64_t total_core_cycles =
                    stats_draw_core_cycles + stats_skip_core_cycles;
                uint64_t interpreter_cycles;
                uint32_t interpreter_tenths;
                uint32_t update_tenths;
                uint32_t ppu_tenths;
                uint32_t dma_tenths;
                uint32_t sound_tenths;
                uint32_t page_total_tenths;
                uint32_t page_max_tenths;
                uint32_t mode_samples;
                uint32_t arm_mode_percent;
                uint32_t thumb_mode_percent;
#endif

                retro_go_audio_get_stats(&audio_stats);
                retro_go_display_get_stats(&display_stats);
                display_completed = display_stats.completed_frames -
                    stats_display_base.completed_frames;
                display_tenths = display_completed != 0u ?
                    retro_go_time_cycles_to_tenths_ms(
                        display_stats.total_present_cycles -
                        stats_display_base.total_present_cycles) /
                            display_completed : 0u;
                display_drops =
                    display_stats.dropped_frames -
                    stats_display_base.dropped_frames +
                    display_stats.failed_frames -
                    stats_display_base.failed_frames;
                plc_delta = audio_stats.underrun_events -
                    stats_audio_base.underrun_events;
                concealed_delta = audio_stats.concealed_samples -
                    stats_audio_base.concealed_samples;
                i2s_underflow_delta = audio_stats.hardware_underflows -
                    stats_audio_base.hardware_underflows;
#ifdef BSP_RETRO_GO_GBA_DEEP_PROFILE
                retro_go_gpsp_profile_snapshot(&profile_current);
                retro_go_gpsp_profile_delta(
                    &profile_current, &stats_profile_base, &profile_delta);
                interpreter_cycles = total_core_cycles >
                    profile_delta.update_cycles +
                    profile_delta.page_outside_update_cycles ?
                    total_core_cycles - profile_delta.update_cycles -
                        profile_delta.page_outside_update_cycles : 0u;
                interpreter_tenths = stats_frames != 0u ?
                    retro_go_time_cycles_to_tenths_ms(interpreter_cycles) /
                        stats_frames : 0u;
                update_tenths = stats_frames != 0u ?
                    retro_go_time_cycles_to_tenths_ms(
                        profile_delta.update_cycles) / stats_frames : 0u;
                ppu_tenths = stats_drawn != 0u ?
                    retro_go_time_cycles_to_tenths_ms(
                        profile_delta.scanline_cycles) / stats_drawn : 0u;
                dma_tenths = stats_frames != 0u ?
                    retro_go_time_cycles_to_tenths_ms(
                        profile_delta.dma_cycles) / stats_frames : 0u;
                sound_tenths = stats_frames != 0u ?
                    retro_go_time_cycles_to_tenths_ms(
                        profile_delta.sound_cycles) / stats_frames : 0u;
                page_total_tenths = retro_go_time_cycles_to_tenths_ms(
                    profile_delta.page_cycles);
                page_max_tenths = retro_go_time_cycles_to_tenths_ms(
                    profile_delta.page_max_cycles);
                mode_samples = profile_delta.arm_update_calls +
                    profile_delta.thumb_update_calls;
                arm_mode_percent = mode_samples != 0u ?
                    profile_delta.arm_update_calls * 100u / mode_samples : 0u;
                thumb_mode_percent = mode_samples != 0u ?
                    100u - arm_mode_percent : 0u;
#endif

                rt_kprintf("[retro-go] GBA perf: %u.%u fps, draw=%u "
                           "skip=%u max=%u.%ums core=%u.%u/%u.%ums "
                           "blit=%u.%ums drop=%u ain=%u aout=%u str=%u.%u%% "
                           "buf=%ums obuf=%ums plc=%u(+%u) i2s=%u(+%u) "
                           "conceal=%u "
                           "peak=%u hot=%u\n",
                           fps_tenths / 10u, fps_tenths % 10u,
                           (unsigned)stats_drawn,
                           (unsigned)stats_skipped,
                           (unsigned)(max_work_tenths / 10u),
                           (unsigned)(max_work_tenths % 10u),
                           (unsigned)(draw_core_tenths / 10u),
                           (unsigned)(draw_core_tenths % 10u),
                           (unsigned)(skip_core_tenths / 10u),
                           (unsigned)(skip_core_tenths % 10u),
                           (unsigned)(display_tenths / 10u),
                           (unsigned)(display_tenths % 10u),
                           (unsigned)display_drops,
                           (unsigned)audio_stats.input_rate_hz,
                           (unsigned)audio_stats.output_rate_hz,
                           (unsigned)(audio_stats.stretch_permille / 10u),
                           (unsigned)(audio_stats.stretch_permille % 10u),
                           (unsigned)audio_stats.source_buffer_ms,
                           (unsigned)audio_stats.output_buffer_ms,
                           (unsigned)audio_stats.underrun_events,
                           (unsigned)plc_delta,
                           (unsigned)audio_stats.hardware_underflows,
                           (unsigned)i2s_underflow_delta,
                           (unsigned)concealed_delta,
                           (unsigned)gba_audio_peak,
                           (unsigned)gba_audio_hot_samples);
#ifdef BSP_RETRO_GO_GBA_DEEP_PROFILE
                rt_kprintf("[retro-go] GBA deep: int=%u.%ums upd=%u.%ums "
                           "ppu=%u.%ums dma=%u.%ums snd=%u.%ums "
                           "mode=A%u/T%u "
                           "page=%u(%u in-upd) total=%u.%ums max=%u.%ums "
                           "bytes=%uK\n",
                           (unsigned)(interpreter_tenths / 10u),
                           (unsigned)(interpreter_tenths % 10u),
                           (unsigned)(update_tenths / 10u),
                           (unsigned)(update_tenths % 10u),
                           (unsigned)(ppu_tenths / 10u),
                           (unsigned)(ppu_tenths % 10u),
                           (unsigned)(dma_tenths / 10u),
                           (unsigned)(dma_tenths % 10u),
                           (unsigned)(sound_tenths / 10u),
                           (unsigned)(sound_tenths % 10u),
                           (unsigned)arm_mode_percent,
                           (unsigned)thumb_mode_percent,
                           (unsigned)profile_delta.page_misses,
                           (unsigned)profile_delta.page_misses_in_update,
                           (unsigned)(page_total_tenths / 10u),
                           (unsigned)(page_total_tenths % 10u),
                           (unsigned)(page_max_tenths / 10u),
                           (unsigned)(page_max_tenths % 10u),
                           (unsigned)(profile_delta.page_bytes / 1024u));
                stats_profile_base = profile_current;
#endif
                stats_started_ms = stats_now_ms;
                stats_frames = 0u;
                stats_drawn = 0u;
                stats_skipped = 0u;
                stats_max_work_cycles = 0u;
                stats_draw_core_cycles = 0u;
                stats_skip_core_cycles = 0u;
                stats_display_base = display_stats;
                stats_audio_base = audio_stats;
                gba_audio_peak = 0u;
                gba_audio_hot_samples = 0u;
            }
#endif
        }

        if (BSP_RETRO_GO_AUTOSAVE_SECONDS > 0 &&
            (uint32_t)(rt_tick_get_millisecond() - last_autosave_ms) >=
                (uint32_t)BSP_RETRO_GO_AUTOSAVE_SECONDS * 1000u)
        {
            (void)gpsp_write_save(sram_path);
            last_autosave_ms = rt_tick_get_millisecond();
        }

        /* Give the present worker deterministic service without letting it
         * outrank audio, USB, HID, or the emulator main thread. */
#if defined(BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY) && \
    defined(RT_MAIN_THREAD_PRIORITY)
        if (retro_go_display_gba_busy())
        {
#if BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY == RT_MAIN_THREAD_PRIORITY
            /* Same-priority round-robin: hand the worker its 5-tick slice
             * immediately after publishing the newest frame. */
            rt_thread_yield();
#elif BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY > RT_MAIN_THREAD_PRIORITY
            rt_thread_mdelay(BSP_RETRO_GO_DISPLAY_YIELD_MS);
#endif
        }
#endif

        if (!fast_forward)
        {
            next_frame_us += RETRO_GO_GBA_FRAME_US;
            do
            {
                now_us = rt_tick_get_millisecond() * 1000u;
                if ((int32_t)(next_frame_us - now_us) > 1000)
                {
                    rt_thread_mdelay(1u);
                }
            } while ((int32_t)(next_frame_us - now_us) > 0);
            if ((int32_t)(now_us - next_frame_us) >
                (int32_t)(RETRO_GO_GBA_FRAME_US * 4u))
            {
                next_frame_us = now_us;
            }
        }
        else
        {
            next_frame_us = rt_tick_get_millisecond() * 1000u;
        }
    }

    (void)retro_go_display_wait_gba_idle(500u);
#ifdef BSP_RETRO_GO_GBA_DEEP_PROFILE
    retro_go_gpsp_profile_set_active(false);
#endif
    retro_go_perf_end();
    if (gpsp_write_save(sram_path) != 0)
    {
        result = -22;
    }
    free(state_buffer);
    gpsp_deinit_core();
    return result;
}
