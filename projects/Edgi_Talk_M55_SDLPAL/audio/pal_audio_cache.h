#ifndef PAL_AUDIO_CACHE_H
#define PAL_AUDIO_CACHE_H

#include <stddef.h>
#include <stdint.h>

#define PAL_AUDIO_CACHE_MAX_ENTRIES 16u
#define PAL_AUDIO_CACHE_MAX_BYTES (1024u * 1024u)
#define PAL_AUDIO_CACHE_INVALID_SLOT UINT16_MAX

typedef int (*pal_audio_cache_size_fn)(void *context, uint8_t archive,
                                       uint16_t chunk, size_t *size);
typedef int (*pal_audio_cache_load_fn)(void *context, uint8_t archive,
                                       uint16_t chunk, void *destination,
                                       size_t size);
typedef void *(*pal_audio_cache_alloc_fn)(void *context, size_t size);
typedef void (*pal_audio_cache_free_fn)(void *context, void *pointer);

typedef struct pal_audio_cache_handle
{
    const uint8_t *data;
    size_t size;
    uint32_t generation;
    uint16_t slot;
} pal_audio_cache_handle_t;

typedef struct pal_audio_cache_metrics
{
    size_t current_bytes;
    size_t peak_bytes;
    uint32_t hits;
    uint32_t misses;
    uint32_t evictions;
    uint32_t failures;
} pal_audio_cache_metrics_t;

typedef struct pal_audio_cache_entry
{
    uint8_t *data;
    size_t size;
    uint64_t stamp;
    uint32_t generation;
    uint16_t references;
    uint16_t chunk;
    uint8_t archive;
    uint8_t occupied;
} pal_audio_cache_entry_t;

typedef struct pal_audio_cache
{
    pal_audio_cache_entry_t entries[PAL_AUDIO_CACHE_MAX_ENTRIES];
    pal_audio_cache_size_fn get_size;
    pal_audio_cache_load_fn load;
    pal_audio_cache_alloc_fn alloc;
    pal_audio_cache_free_fn free;
    void *source_context;
    void *allocator_context;
    size_t limit;
    uint64_t clock;
    pal_audio_cache_metrics_t metrics;
} pal_audio_cache_t;

void pal_audio_cache_init(pal_audio_cache_t *cache, size_t limit,
                          pal_audio_cache_size_fn get_size,
                          pal_audio_cache_load_fn load,
                          void *source_context,
                          pal_audio_cache_alloc_fn alloc,
                          pal_audio_cache_free_fn free_fn,
                          void *allocator_context);
int pal_audio_cache_acquire(pal_audio_cache_t *cache, uint8_t archive,
                            uint16_t chunk,
                            pal_audio_cache_handle_t *handle);
void pal_audio_cache_release(pal_audio_cache_t *cache,
                             pal_audio_cache_handle_t *handle);
void pal_audio_cache_clear(pal_audio_cache_t *cache);
size_t pal_audio_cache_limit(const pal_audio_cache_t *cache);
void pal_audio_cache_metrics_get(const pal_audio_cache_t *cache,
                                 pal_audio_cache_metrics_t *metrics);

#endif
