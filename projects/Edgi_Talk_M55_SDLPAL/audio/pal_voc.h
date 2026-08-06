#ifndef PAL_VOC_H
#define PAL_VOC_H

#include <stddef.h>
#include <stdint.h>

typedef struct pal_voc_view
{
    const uint8_t *data;
    size_t size;
    size_t block_offset;
} pal_voc_view_t;

typedef struct pal_voc_stream
{
    const uint8_t *data;
    size_t size;
    size_t cursor;
    const uint8_t *block_data;
    size_t block_remaining;
    uint32_t silence_remaining;
    uint32_t current_block_rate;
    uint32_t pending_extended_rate;
    uint32_t output_rate;
    uint32_t current_rate;
    uint32_t next_rate;
    uint64_t phase_q32;
    uint64_t step_q32;
    int16_t current_sample;
    int16_t next_sample;
    uint8_t next_valid;
    uint8_t active;
} pal_voc_stream_t;

int pal_voc_open(pal_voc_view_t *view, const void *data, size_t size);
int pal_voc_stream_start(pal_voc_stream_t *stream,
                         const pal_voc_view_t *view,
                         uint32_t output_rate);
size_t pal_voc_stream_render(pal_voc_stream_t *stream, int16_t *output,
                             size_t sample_count);
int pal_voc_stream_active(const pal_voc_stream_t *stream);

#endif
