#ifndef RETRO_GO_GPSP_PROFILE_H
#define RETRO_GO_GPSP_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct retro_go_gpsp_profile_stats
{
    uint64_t update_cycles;
    uint64_t scanline_cycles;
    uint64_t dma_cycles;
    uint64_t sound_cycles;
    uint64_t page_cycles;
    uint64_t page_outside_update_cycles;
    uint32_t update_calls;
    uint32_t arm_update_calls;
    uint32_t thumb_update_calls;
    uint32_t scanline_calls;
    uint32_t dma_calls;
    uint32_t sound_calls;
    uint32_t page_misses;
    uint32_t page_misses_in_update;
    uint32_t page_max_cycles;
    uint32_t page_bytes;
} retro_go_gpsp_profile_stats_t;

void retro_go_gpsp_profile_reset(void);
void retro_go_gpsp_profile_set_active(bool active);
void retro_go_gpsp_profile_snapshot(retro_go_gpsp_profile_stats_t *stats);
void retro_go_gpsp_profile_delta(
    const retro_go_gpsp_profile_stats_t *current,
    const retro_go_gpsp_profile_stats_t *previous,
    retro_go_gpsp_profile_stats_t *delta);

#endif
