#ifndef RETRO_GO_AUDIO_H
#define RETRO_GO_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct retro_go_audio_stats
{
    uint32_t input_rate_hz;
    uint32_t output_rate_hz;
    uint32_t stretch_permille;
    uint32_t source_buffer_samples;
    uint32_t source_buffer_ms;
    uint32_t output_buffer_samples;
    uint32_t output_buffer_ms;
    uint32_t concealed_samples;
    uint32_t underrun_events;
    uint32_t hardware_underflows;
} retro_go_audio_stats_t;

bool retro_go_audio_init(void);
void retro_go_audio_submit(const int16_t *samples, size_t sample_count);
size_t retro_go_audio_buffered_samples(void);
uint32_t retro_go_audio_submitted_samples(void);
void retro_go_audio_get_stats(retro_go_audio_stats_t *stats);
void retro_go_audio_deinit(void);

#endif
