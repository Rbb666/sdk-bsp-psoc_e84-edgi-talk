#include "pal_audio_resampler.h"

#include <string.h>

void pal_audio_resampler_init(pal_audio_resampler_t *resampler,
                              uint32_t source_rate,
                              uint32_t output_rate,
                              uint32_t tick_rate)
{
    if (resampler == NULL)
    {
        return;
    }
    memset(resampler, 0, sizeof(*resampler));
    resampler->source_rate = source_rate;
    resampler->output_rate = output_rate;
    resampler->tick_rate = tick_rate;
}

size_t pal_audio_resampler_process_tick(
    pal_audio_resampler_t *resampler, const int16_t *source,
    size_t source_count, int16_t *output, size_t output_capacity)
{
    uint64_t tick_accumulator;
    uint64_t numerator;
    size_t frames;
    size_t i;

    if (resampler == NULL || source == NULL || source_count == 0u ||
        output == NULL || resampler->source_rate == 0u ||
        resampler->output_rate == 0u || resampler->tick_rate == 0u)
    {
        return 0u;
    }
    tick_accumulator = (uint64_t)resampler->tick_phase +
                       resampler->output_rate;
    frames = (size_t)(tick_accumulator / resampler->tick_rate);
    if (frames == 0u || frames > output_capacity)
    {
        return 0u;
    }

    numerator = resampler->source_position_numerator;
    for (i = 0u; i < frames; ++i)
    {
        uint64_t source_index = numerator / resampler->output_rate;
        uint32_t fraction = (uint32_t)(numerator % resampler->output_rate);
        int32_t first;
        int32_t second;

        if (source_index < resampler->source_base)
        {
            uint64_t distance = resampler->source_base - source_index;
            size_t history;

            if (distance == 0u || distance > resampler->previous_count)
            {
                return 0u;
            }
            history = resampler->previous_count - (size_t)distance;
            first = resampler->previous_samples[history];
            second = history + 1u < resampler->previous_count
                         ? resampler->previous_samples[history + 1u]
                         : source[0];
        }
        else
        {
            size_t local = (size_t)(source_index - resampler->source_base);

            if (local >= source_count ||
                (fraction != 0u && local + 1u >= source_count))
            {
                return 0u;
            }
            first = source[local];
            second = local + 1u < source_count ? source[local + 1u] : first;
        }
        output[i] = (int16_t)(
            first + ((int64_t)(second - first) * fraction) /
                        resampler->output_rate);
        numerator += resampler->source_rate;
    }

    resampler->tick_phase =
        (uint32_t)(tick_accumulator % resampler->tick_rate);
    resampler->source_position_numerator = numerator;
    resampler->source_base += source_count;
    if (source_count == 1u)
    {
        resampler->previous_samples[0] = source[0];
        resampler->previous_count = 1u;
    }
    else
    {
        resampler->previous_samples[0] = source[source_count - 2u];
        resampler->previous_samples[1] = source[source_count - 1u];
        resampler->previous_count = 2u;
    }
    return frames;
}
