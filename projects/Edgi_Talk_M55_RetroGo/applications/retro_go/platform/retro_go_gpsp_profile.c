#include "retro_go_gpsp_profile.h"

#include "retro_go_gpsp_prelude.h"
#include "retro_go_platform.h"
#include "retro_go_time.h"

#include <string.h>

#define RETRO_GO_GPSP_PAGE_BYTES (32u * 1024u)

static retro_go_gpsp_profile_stats_t profile_stats RETRO_GO_SOCMEM;
static bool profile_active;
static uint32_t profile_update_depth;

extern u32 function_cc __real_update_gba(int remaining_cycles);
extern void __real_update_scanline(void);
extern cpu_alert_type __real_dma_transfer(unsigned dma_chan, int *cycles);
extern void __real_render_gbc_sound(void);
extern u8 *__real_load_gamepak_page(u32 physical_index);

static uint32_t profile_elapsed(uint32_t started)
{
    return retro_go_time_elapsed_cycles(started);
}

RETRO_GO_ITCM u32 function_cc __wrap_update_gba(int remaining_cycles)
{
    uint32_t started;
    u32 result;

    if (!profile_active)
    {
        return __real_update_gba(remaining_cycles);
    }
    started = retro_go_time_now_cycles();
    if ((reg[REG_CPSR] & 0x20u) != 0u)
    {
        ++profile_stats.thumb_update_calls;
    }
    else
    {
        ++profile_stats.arm_update_calls;
    }
    ++profile_update_depth;
    result = __real_update_gba(remaining_cycles);
    --profile_update_depth;
    profile_stats.update_cycles += profile_elapsed(started);
    ++profile_stats.update_calls;
    return result;
}

RETRO_GO_ITCM void __wrap_update_scanline(void)
{
    uint32_t started;

    if (!profile_active)
    {
        __real_update_scanline();
        return;
    }
    started = retro_go_time_now_cycles();
    __real_update_scanline();
    profile_stats.scanline_cycles += profile_elapsed(started);
    ++profile_stats.scanline_calls;
}

RETRO_GO_ITCM cpu_alert_type __wrap_dma_transfer(
    unsigned dma_chan, int *cycles)
{
    uint32_t started;
    cpu_alert_type result;

    if (!profile_active)
    {
        return __real_dma_transfer(dma_chan, cycles);
    }
    started = retro_go_time_now_cycles();
    result = __real_dma_transfer(dma_chan, cycles);
    profile_stats.dma_cycles += profile_elapsed(started);
    ++profile_stats.dma_calls;
    return result;
}

RETRO_GO_ITCM void __wrap_render_gbc_sound(void)
{
    uint32_t started;

    if (!profile_active)
    {
        __real_render_gbc_sound();
        return;
    }
    started = retro_go_time_now_cycles();
    __real_render_gbc_sound();
    profile_stats.sound_cycles += profile_elapsed(started);
    ++profile_stats.sound_calls;
}

RETRO_GO_ITCM u8 *__wrap_load_gamepak_page(u32 physical_index)
{
    uint32_t started;
    uint32_t elapsed;
    bool in_update;
    u8 *result;

    if (!profile_active)
    {
        return __real_load_gamepak_page(physical_index);
    }
    in_update = profile_update_depth != 0u;
    started = retro_go_time_now_cycles();
    result = __real_load_gamepak_page(physical_index);
    elapsed = profile_elapsed(started);
    profile_stats.page_cycles += elapsed;
    if (in_update)
    {
        ++profile_stats.page_misses_in_update;
    }
    else
    {
        profile_stats.page_outside_update_cycles += elapsed;
    }
    ++profile_stats.page_misses;
    profile_stats.page_bytes += RETRO_GO_GPSP_PAGE_BYTES;
    if (elapsed > profile_stats.page_max_cycles)
    {
        profile_stats.page_max_cycles = elapsed;
    }
    return result;
}

void retro_go_gpsp_profile_reset(void)
{
    memset(&profile_stats, 0, sizeof(profile_stats));
    profile_update_depth = 0u;
}

void retro_go_gpsp_profile_set_active(bool active)
{
    profile_active = active;
}

void retro_go_gpsp_profile_snapshot(retro_go_gpsp_profile_stats_t *stats)
{
    if (stats != NULL)
    {
        *stats = profile_stats;
    }
}

void retro_go_gpsp_profile_delta(
    const retro_go_gpsp_profile_stats_t *current,
    const retro_go_gpsp_profile_stats_t *previous,
    retro_go_gpsp_profile_stats_t *delta)
{
    if (current == NULL || previous == NULL || delta == NULL)
    {
        return;
    }
#define PROFILE_DELTA(field) delta->field = current->field - previous->field
    PROFILE_DELTA(update_cycles);
    PROFILE_DELTA(scanline_cycles);
    PROFILE_DELTA(dma_cycles);
    PROFILE_DELTA(sound_cycles);
    PROFILE_DELTA(page_cycles);
    PROFILE_DELTA(page_outside_update_cycles);
    PROFILE_DELTA(update_calls);
    PROFILE_DELTA(arm_update_calls);
    PROFILE_DELTA(thumb_update_calls);
    PROFILE_DELTA(scanline_calls);
    PROFILE_DELTA(dma_calls);
    PROFILE_DELTA(sound_calls);
    PROFILE_DELTA(page_misses);
    PROFILE_DELTA(page_misses_in_update);
    PROFILE_DELTA(page_bytes);
    delta->page_max_cycles = current->page_max_cycles;
#undef PROFILE_DELTA
}
