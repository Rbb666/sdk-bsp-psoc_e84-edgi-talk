#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pal_audio_mixer.h"
#include "pal_voc.h"

typedef struct voc_builder
{
    uint8_t data[256];
    size_t size;
} voc_builder_t;

static void put_le16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void voc_begin(voc_builder_t *builder)
{
    static const uint8_t signature[20] = {
        'C', 'r', 'e', 'a', 't', 'i', 'v', 'e', ' ', 'V',
        'o', 'i', 'c', 'e', ' ', 'F', 'i', 'l', 'e', 0x1a};

    memset(builder, 0, sizeof(*builder));
    memcpy(builder->data, signature, sizeof(signature));
    put_le16(builder->data + 20, 26u);
    put_le16(builder->data + 22, 0x0114u);
    put_le16(builder->data + 24, 0x111fu);
    builder->size = 26u;
}

static void voc_block(voc_builder_t *builder, uint8_t type,
                      const uint8_t *payload, size_t size)
{
    assert(builder->size + 4u + size < sizeof(builder->data));
    builder->data[builder->size++] = type;
    builder->data[builder->size++] = (uint8_t)size;
    builder->data[builder->size++] = (uint8_t)(size >> 8);
    builder->data[builder->size++] = (uint8_t)(size >> 16);
    memcpy(builder->data + builder->size, payload, size);
    builder->size += size;
}

static void voc_end(voc_builder_t *builder)
{
    builder->data[builder->size++] = 0u;
}

static void add_pcm(voc_builder_t *builder, uint8_t time_constant,
                    const uint8_t *samples, size_t count)
{
    uint8_t payload[66];

    assert(count <= sizeof(payload) - 2u);
    payload[0] = time_constant;
    payload[1] = 0u;
    memcpy(payload + 2, samples, count);
    voc_block(builder, 1u, payload, count + 2u);
}

static void test_parser_and_same_rate_pcm(void)
{
    const uint8_t samples[] = {128u, 255u, 0u, 128u};
    voc_builder_t builder;
    pal_voc_view_t view;
    pal_voc_stream_t stream;
    int16_t output[8] = {0};

    voc_begin(&builder);
    add_pcm(&builder, 131u, samples, sizeof(samples));
    voc_end(&builder);

    assert(pal_voc_open(&view, builder.data, builder.size));
    assert(pal_voc_stream_start(&stream, &view, 8000u));
    assert(pal_voc_stream_render(&stream, output, 8u) == 4u);
    assert(output[0] == 0);
    assert(output[1] == 32512);
    assert(output[2] == -32768);
    assert(output[3] == 0);
    assert(!pal_voc_stream_active(&stream));
}

static void test_continuation_and_silence_blocks(void)
{
    const uint8_t first[] = {255u};
    const uint8_t continued[] = {0u};
    const uint8_t silence[] = {1u, 0u, 131u};
    voc_builder_t builder;
    pal_voc_view_t view;
    pal_voc_stream_t stream;
    int16_t output[8] = {1, 1, 1, 1, 1, 1, 1, 1};

    voc_begin(&builder);
    add_pcm(&builder, 131u, first, sizeof(first));
    voc_block(&builder, 2u, continued, sizeof(continued));
    voc_block(&builder, 3u, silence, sizeof(silence));
    voc_end(&builder);

    assert(pal_voc_open(&view, builder.data, builder.size));
    assert(pal_voc_stream_start(&stream, &view, 8000u));
    assert(pal_voc_stream_render(&stream, output, 8u) == 4u);
    assert(output[0] == 32512);
    assert(output[1] == -32768);
    assert(output[2] == 0);
    assert(output[3] == 0);
}

static void test_extended_rate_and_corrupt_inputs(void)
{
    const uint8_t extended[] = {0x00u, 0x83u, 0u, 0u};
    const uint8_t samples[] = {128u, 255u};
    voc_builder_t builder;
    pal_voc_view_t view;

    voc_begin(&builder);
    voc_block(&builder, 8u, extended, sizeof(extended));
    add_pcm(&builder, 0u, samples, sizeof(samples));
    voc_end(&builder);
    assert(pal_voc_open(&view, builder.data, builder.size));

    builder.data[builder.size - 1u] = 1u;
    assert(!pal_voc_open(&view, builder.data, builder.size));

    voc_begin(&builder);
    add_pcm(&builder, 131u, samples, sizeof(samples));
    builder.data[31] = 1u;
    voc_end(&builder);
    assert(!pal_voc_open(&view, builder.data, builder.size));

    voc_begin(&builder);
    add_pcm(&builder, 131u, samples, sizeof(samples));
    voc_end(&builder);
    assert(!pal_voc_open(&view, builder.data, builder.size - 2u));
}

static void test_fixed_point_upsampling(void)
{
    const uint8_t samples[] = {128u, 255u, 128u};
    voc_builder_t builder;
    pal_voc_view_t view;
    pal_voc_stream_t stream;
    int16_t output[8] = {0};
    size_t rendered;

    voc_begin(&builder);
    add_pcm(&builder, 131u, samples, sizeof(samples));
    voc_end(&builder);
    assert(pal_voc_open(&view, builder.data, builder.size));
    assert(pal_voc_stream_start(&stream, &view, 16000u));
    rendered = pal_voc_stream_render(&stream, output, 8u);
    assert(rendered >= 5u);
    assert(output[0] == 0);
    assert(output[1] >= 16255 && output[1] <= 16257);
    assert(output[2] == 32512);
    assert(output[3] >= 16255 && output[3] <= 16257);
    assert(output[4] == 0);
}

static void test_four_voice_mixer_and_saturation(void)
{
    uint8_t loud[18];
    voc_builder_t builder;
    pal_audio_mixer_t mixer;
    pal_audio_mixer_metrics_t metrics;
    int16_t music[8];
    int16_t output[8];
    unsigned i;

    memset(loud, 255, sizeof(loud));
    voc_begin(&builder);
    add_pcm(&builder, 131u, loud, sizeof(loud));
    voc_end(&builder);
    for (i = 0u; i < 8u; ++i)
    {
        music[i] = 30000;
    }

    pal_audio_mixer_init(&mixer, 8000u);
    pal_audio_mixer_set_volume(&mixer, PAL_AUDIO_GAIN_ONE,
                               PAL_AUDIO_GAIN_ONE);
    for (i = 0u; i < PAL_AUDIO_MIXER_MAX_VOICES; ++i)
    {
        assert(pal_audio_mixer_start_voc(&mixer, builder.data, builder.size));
    }
    assert(pal_audio_mixer_active_voices(&mixer) == 4u);
    assert(pal_audio_mixer_start_voc(&mixer, builder.data, builder.size));
    assert(pal_audio_mixer_active_voices(&mixer) == 4u);

    pal_audio_mixer_render(&mixer, music, output, 8u);
    for (i = 0u; i < 8u; ++i)
    {
        assert(output[i] == 32767);
    }
    pal_audio_mixer_metrics_get(&mixer, &metrics);
    assert(metrics.peak_voices == 4u);
    assert(metrics.replaced_voices == 1u);
}

int main(void)
{
    test_parser_and_same_rate_pcm();
    test_continuation_and_silence_blocks();
    test_extended_rate_and_corrupt_inputs();
    test_fixed_point_upsampling();
    test_four_voice_mixer_and_saturation();
    puts("voc_mixer: PASS");
    return 0;
}
