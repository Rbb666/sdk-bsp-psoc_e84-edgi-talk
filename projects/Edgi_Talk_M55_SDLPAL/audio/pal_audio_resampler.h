#ifndef PAL_AUDIO_RESAMPLER_H
#define PAL_AUDIO_RESAMPLER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pal_audio_resampler
{
    uint64_t source_position_numerator;
    uint64_t source_base;
    uint32_t source_rate;
    uint32_t output_rate;
    uint32_t tick_rate;
    uint32_t tick_phase;
    int16_t previous_samples[2];
    uint8_t previous_count;
} pal_audio_resampler_t;

void pal_audio_resampler_init(pal_audio_resampler_t *resampler,
                              uint32_t source_rate,
                              uint32_t output_rate,
                              uint32_t tick_rate);
size_t pal_audio_resampler_process_tick(
    pal_audio_resampler_t *resampler, const int16_t *source,
    size_t source_count, int16_t *output, size_t output_capacity);

#ifdef __cplusplus
}
#endif

#endif
