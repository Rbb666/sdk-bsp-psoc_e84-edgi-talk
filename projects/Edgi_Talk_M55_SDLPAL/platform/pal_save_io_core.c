#include "pal_save_io_core.h"

#include <stdint.h>

size_t pal_save_write_chunked(const void *data, size_t element_size,
                              size_t element_count,
                              pal_save_sink_fn sink, void *sink_context,
                              pal_save_yield_fn yield, void *yield_context)
{
    const uint8_t *cursor = (const uint8_t *)data;
    size_t total_bytes;
    size_t written_bytes = 0u;

    if (cursor == NULL || sink == NULL ||
        element_size == 0u || element_count == 0u ||
        element_count > SIZE_MAX / element_size)
    {
        return 0u;
    }

    total_bytes = element_size * element_count;
    while (written_bytes < total_bytes)
    {
        size_t remaining = total_bytes - written_bytes;
        size_t request = remaining < PAL_SAVE_IO_CHUNK_BYTES ?
                         remaining : PAL_SAVE_IO_CHUNK_BYTES;
        size_t accepted = sink(cursor + written_bytes, request, sink_context);

        written_bytes += accepted;
        if (accepted != request)
        {
            break;
        }
        if (written_bytes < total_bytes && yield != NULL)
        {
            yield(yield_context);
        }
    }

    return written_bytes / element_size;
}
