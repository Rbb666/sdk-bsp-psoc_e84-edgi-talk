#include "pal_audio_resampler.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define SOURCE_RATE 22050u
#define OUTPUT_RATE 16000u
#define TICK_SAMPLES 315u
#define TICK_COUNT 70u
#define OUTPUT_SAMPLES 16000u

static int16_t source_sample(uint32_t index)
{
    return (int16_t)((index * 37u + 11u) & 0x7fffu);
}

static int16_t expected_sample(uint32_t output_index)
{
    uint64_t numerator = (uint64_t)output_index * SOURCE_RATE;
    uint32_t source_index = (uint32_t)(numerator / OUTPUT_RATE);
    uint32_t fraction = (uint32_t)(numerator % OUTPUT_RATE);
    int32_t first = source_sample(source_index);
    int32_t second = source_sample(source_index + 1u);

    return (int16_t)(first +
                     ((int64_t)(second - first) * fraction) / OUTPUT_RATE);
}

static void test_continuous_fixed_ratio(void)
{
    pal_audio_resampler_t resampler;
    int16_t input[TICK_SAMPLES];
    int16_t output[229];
    uint32_t produced_total = 0u;
    uint32_t source_base = 0u;

    pal_audio_resampler_init(&resampler, SOURCE_RATE, OUTPUT_RATE, 70u);
    for (uint32_t tick = 0u; tick < TICK_COUNT; ++tick)
    {
        size_t produced;

        for (uint32_t i = 0u; i < TICK_SAMPLES; ++i)
        {
            input[i] = source_sample(source_base + i);
        }
        produced = pal_audio_resampler_process_tick(
            &resampler, input, TICK_SAMPLES, output,
            sizeof(output) / sizeof(output[0]));
        assert(produced == 228u || produced == 229u);
        for (size_t i = 0u; i < produced; ++i)
        {
            assert(output[i] == expected_sample(produced_total + i));
        }
        produced_total += (uint32_t)produced;
        source_base += TICK_SAMPLES;
    }
    assert(produced_total == OUTPUT_SAMPLES);
}

int main(void)
{
    test_continuous_fixed_ratio();
    puts("audio_resampler: PASS");
    return 0;
}
