#ifndef PAL_AUDIO_DIAGNOSTICS_H
#define PAL_AUDIO_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pal_audio_diagnostics
{
    size_t cache_current_bytes;
    size_t cache_peak_bytes;
    uint32_t rendered_blocks;
    uint32_t written_blocks;
    uint32_t short_writes;
    uint32_t silence_recoveries;
    uint32_t render_last_us;
    uint32_t render_max_us;
    uint32_t write_last_us;
    uint32_t write_max_us;
    uint32_t hardware_underruns;
    uint32_t active_voices;
    uint32_t peak_voices;
    uint32_t replaced_voices;
    uint32_t rejected_voices;
    uint32_t sound_drops;
    uint32_t music_coalesces;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t cache_evictions;
    uint32_t cache_failures;
    uint32_t rendered_rix_ticks;
    uint32_t completed_rix_loops;
    uint32_t failed_rix_tracks;
    uint32_t stale_music_commands;
    uint32_t release_overflows;
    uint32_t audio_stack_used_bytes;
    uint32_t audio_stack_total_bytes;
    int16_t current_music;
    uint8_t opened;
    uint8_t music_enabled;
    uint8_t sound_enabled;
    uint8_t rix_playing;
} pal_audio_diagnostics_t;

void pal_audio_diagnostics_get(pal_audio_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
