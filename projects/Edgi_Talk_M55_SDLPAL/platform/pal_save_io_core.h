#ifndef PAL_SAVE_IO_CORE_H
#define PAL_SAVE_IO_CORE_H

#include <stddef.h>

#define PAL_SAVE_IO_CHUNK_BYTES 512u

typedef size_t (*pal_save_sink_fn)(const void *data, size_t bytes,
                                   void *context);
typedef void (*pal_save_yield_fn)(void *context);

size_t pal_save_write_chunked(const void *data, size_t element_size,
                              size_t element_count,
                              pal_save_sink_fn sink, void *sink_context,
                              pal_save_yield_fn yield, void *yield_context);

#endif
