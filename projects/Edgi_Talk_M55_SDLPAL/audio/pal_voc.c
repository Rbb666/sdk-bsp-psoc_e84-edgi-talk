#include "pal_voc.h"

#include <string.h>

#define PAL_VOC_HEADER_BYTES 26u
#define PAL_VOC_PHASE_ONE (UINT64_C(1) << 32)

static const uint8_t voc_signature[20] = {
    'C', 'r', 'e', 'a', 't', 'i', 'v', 'e', ' ', 'V',
    'o', 'i', 'c', 'e', ' ', 'F', 'i', 'l', 'e', 0x1a};

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_le24(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int valid_rate(uint32_t rate)
{
    return rate >= 1000u && rate <= 96000u;
}

static uint32_t rate_from_byte(uint8_t time_constant)
{
    uint32_t denominator = 256u - (uint32_t)time_constant;

    return denominator != 0u ? 1000000u / denominator : 0u;
}

static uint32_t rate_from_extended(uint16_t time_constant, uint8_t channels)
{
    uint32_t denominator = 65536u - (uint32_t)time_constant;
    uint32_t rate;

    if (denominator == 0u || channels == 0u)
    {
        return 0u;
    }
    rate = 256000000u / denominator;
    return rate / channels;
}

static int validate_blocks(const uint8_t *data, size_t size, size_t offset)
{
    uint32_t current_rate = 0u;
    uint32_t extended_rate = 0u;
    int found_audio = 0;

    while (offset < size)
    {
        uint8_t type = data[offset++];
        uint32_t length;
        const uint8_t *payload;

        if (type == 0u)
        {
            return found_audio;
        }
        if (size - offset < 3u)
        {
            return 0;
        }
        length = read_le24(data + offset);
        offset += 3u;
        if ((size_t)length > size - offset)
        {
            return 0;
        }
        payload = data + offset;

        switch (type)
        {
        case 1u:
            if (length < 2u || payload[1] != 0u)
            {
                return 0;
            }
            current_rate = extended_rate != 0u
                               ? extended_rate
                               : rate_from_byte(payload[0]);
            extended_rate = 0u;
            if (!valid_rate(current_rate))
            {
                return 0;
            }
            found_audio = 1;
            break;
        case 2u:
            if (current_rate == 0u)
            {
                return 0;
            }
            found_audio = 1;
            break;
        case 3u:
            if (length < 3u || !valid_rate(rate_from_byte(payload[2])))
            {
                return 0;
            }
            current_rate = rate_from_byte(payload[2]);
            found_audio = 1;
            break;
        case 8u:
            if (length < 4u || payload[2] != 0u || payload[3] != 0u)
            {
                return 0;
            }
            extended_rate = rate_from_extended(read_le16(payload), 1u);
            if (!valid_rate(extended_rate))
            {
                return 0;
            }
            break;
        case 9u:
            if (length < 12u || payload[4] != 8u || payload[5] != 1u ||
                read_le16(payload + 6) != 0u ||
                !valid_rate(read_le32(payload)))
            {
                return 0;
            }
            current_rate = read_le32(payload);
            found_audio = 1;
            break;
        default:
            break;
        }
        offset += length;
    }
    return 0;
}

int pal_voc_open(pal_voc_view_t *view, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t offset;

    if (view == NULL)
    {
        return 0;
    }
    memset(view, 0, sizeof(*view));
    if (bytes == NULL || size < PAL_VOC_HEADER_BYTES ||
        memcmp(bytes, voc_signature, sizeof(voc_signature)) != 0)
    {
        return 0;
    }
    offset = read_le16(bytes + 20u);
    if (offset < PAL_VOC_HEADER_BYTES || offset >= size ||
        !validate_blocks(bytes, size, offset))
    {
        return 0;
    }
    view->data = bytes;
    view->size = size;
    view->block_offset = offset;
    return 1;
}

static int next_source_sample(pal_voc_stream_t *stream, int16_t *sample,
                              uint32_t *rate)
{
    while (stream->cursor < stream->size ||
           stream->block_remaining != 0u ||
           stream->silence_remaining != 0u)
    {
        if (stream->block_remaining != 0u)
        {
            *sample = (int16_t)(((int32_t)*stream->block_data++ - 128) * 256);
            --stream->block_remaining;
            *rate = stream->current_block_rate;
            return 1;
        }
        if (stream->silence_remaining != 0u)
        {
            --stream->silence_remaining;
            *sample = 0;
            *rate = stream->current_block_rate;
            return 1;
        }

        {
            uint8_t type = stream->data[stream->cursor++];
            uint32_t length;
            const uint8_t *payload;

            if (type == 0u)
            {
                return 0;
            }
            length = read_le24(stream->data + stream->cursor);
            stream->cursor += 3u;
            payload = stream->data + stream->cursor;
            stream->cursor += length;

            switch (type)
            {
            case 1u:
                stream->current_block_rate =
                    stream->pending_extended_rate != 0u
                        ? stream->pending_extended_rate
                        : rate_from_byte(payload[0]);
                stream->pending_extended_rate = 0u;
                stream->block_data = payload + 2u;
                stream->block_remaining = length - 2u;
                break;
            case 2u:
                stream->block_data = payload;
                stream->block_remaining = length;
                break;
            case 3u:
                stream->current_block_rate = rate_from_byte(payload[2]);
                stream->silence_remaining =
                    (uint32_t)read_le16(payload) + 1u;
                break;
            case 8u:
                stream->pending_extended_rate =
                    rate_from_extended(read_le16(payload), 1u);
                break;
            case 9u:
                stream->current_block_rate = read_le32(payload);
                stream->block_data = payload + 12u;
                stream->block_remaining = length - 12u;
                break;
            default:
                break;
            }
        }
    }
    return 0;
}

static uint64_t rate_step(uint32_t source_rate, uint32_t output_rate)
{
    return ((uint64_t)source_rate << 32) / output_rate;
}

int pal_voc_stream_start(pal_voc_stream_t *stream,
                         const pal_voc_view_t *view,
                         uint32_t output_rate)
{
    if (stream == NULL || view == NULL || view->data == NULL ||
        output_rate == 0u)
    {
        return 0;
    }
    memset(stream, 0, sizeof(*stream));
    stream->data = view->data;
    stream->size = view->size;
    stream->cursor = view->block_offset;
    stream->output_rate = output_rate;
    if (!next_source_sample(stream, &stream->current_sample,
                            &stream->current_rate))
    {
        return 0;
    }
    stream->next_valid = (uint8_t)next_source_sample(
        stream, &stream->next_sample, &stream->next_rate);
    if (!stream->next_valid)
    {
        stream->next_sample = stream->current_sample;
        stream->next_rate = stream->current_rate;
    }
    stream->step_q32 = rate_step(stream->current_rate, output_rate);
    stream->active = 1u;
    return 1;
}

size_t pal_voc_stream_render(pal_voc_stream_t *stream, int16_t *output,
                             size_t sample_count)
{
    size_t rendered = 0u;

    if (stream == NULL || output == NULL)
    {
        return 0u;
    }
    while (rendered < sample_count && stream->active)
    {
        int32_t difference =
            (int32_t)stream->next_sample - stream->current_sample;
        int64_t interpolated =
            (int64_t)difference * (int64_t)stream->phase_q32;

        output[rendered++] = (int16_t)(
            (int32_t)stream->current_sample +
            (int32_t)(interpolated / (int64_t)PAL_VOC_PHASE_ONE));
        stream->phase_q32 += stream->step_q32;

        while (stream->phase_q32 >= PAL_VOC_PHASE_ONE && stream->active)
        {
            stream->phase_q32 -= PAL_VOC_PHASE_ONE;
            if (!stream->next_valid)
            {
                stream->active = 0u;
                break;
            }

            stream->current_sample = stream->next_sample;
            stream->current_rate = stream->next_rate;
            stream->next_valid = (uint8_t)next_source_sample(
                stream, &stream->next_sample, &stream->next_rate);
            if (!stream->next_valid)
            {
                stream->next_sample = stream->current_sample;
                stream->next_rate = stream->current_rate;
            }
            stream->step_q32 = rate_step(stream->current_rate,
                                         stream->output_rate);
        }
    }
    if (rendered < sample_count)
    {
        memset(output + rendered, 0,
               (sample_count - rendered) * sizeof(*output));
    }
    return rendered;
}

int pal_voc_stream_active(const pal_voc_stream_t *stream)
{
    return stream != NULL && stream->active != 0u;
}
