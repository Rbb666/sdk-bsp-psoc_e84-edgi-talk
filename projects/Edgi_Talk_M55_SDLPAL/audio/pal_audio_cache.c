#include "pal_audio_cache.h"

#include <string.h>

static void clear_handle(pal_audio_cache_handle_t *handle)
{
    if (handle == NULL)
    {
        return;
    }
    memset(handle, 0, sizeof(*handle));
    handle->slot = PAL_AUDIO_CACHE_INVALID_SLOT;
}

static uint64_t next_stamp(pal_audio_cache_t *cache)
{
    ++cache->clock;
    if (cache->clock == 0u)
    {
        size_t i;

        cache->clock = 1u;
        for (i = 0u; i < PAL_AUDIO_CACHE_MAX_ENTRIES; ++i)
        {
            if (cache->entries[i].occupied)
            {
                cache->entries[i].stamp = 1u;
            }
        }
    }
    return cache->clock;
}

static pal_audio_cache_entry_t *find_entry(pal_audio_cache_t *cache,
                                           uint8_t archive,
                                           uint16_t chunk,
                                           uint16_t *slot)
{
    uint16_t i;

    for (i = 0u; i < PAL_AUDIO_CACHE_MAX_ENTRIES; ++i)
    {
        pal_audio_cache_entry_t *entry = &cache->entries[i];

        if (entry->occupied && entry->archive == archive &&
            entry->chunk == chunk)
        {
            if (slot != NULL)
            {
                *slot = i;
            }
            return entry;
        }
    }
    return NULL;
}

static uint16_t find_free_slot(const pal_audio_cache_t *cache)
{
    uint16_t i;

    for (i = 0u; i < PAL_AUDIO_CACHE_MAX_ENTRIES; ++i)
    {
        if (!cache->entries[i].occupied)
        {
            return i;
        }
    }
    return PAL_AUDIO_CACHE_INVALID_SLOT;
}

static uint16_t find_lru_slot(const pal_audio_cache_t *cache)
{
    uint16_t candidate = PAL_AUDIO_CACHE_INVALID_SLOT;
    uint16_t i;

    for (i = 0u; i < PAL_AUDIO_CACHE_MAX_ENTRIES; ++i)
    {
        const pal_audio_cache_entry_t *entry = &cache->entries[i];

        if (!entry->occupied || entry->references != 0u)
        {
            continue;
        }
        if (candidate == PAL_AUDIO_CACHE_INVALID_SLOT ||
            entry->stamp < cache->entries[candidate].stamp)
        {
            candidate = i;
        }
    }
    return candidate;
}

static int evict_slot(pal_audio_cache_t *cache, uint16_t slot)
{
    pal_audio_cache_entry_t *entry;
    uint32_t generation;

    if (slot >= PAL_AUDIO_CACHE_MAX_ENTRIES)
    {
        return 0;
    }
    entry = &cache->entries[slot];
    if (!entry->occupied || entry->references != 0u)
    {
        return 0;
    }

    cache->free(cache->allocator_context, entry->data);
    cache->metrics.current_bytes -= entry->size;
    ++cache->metrics.evictions;
    generation = entry->generation;
    memset(entry, 0, sizeof(*entry));
    entry->generation = generation;
    return 1;
}

static int make_room(pal_audio_cache_t *cache, size_t size)
{
    while (cache->metrics.current_bytes > cache->limit - size)
    {
        uint16_t slot = find_lru_slot(cache);

        if (slot == PAL_AUDIO_CACHE_INVALID_SLOT || !evict_slot(cache, slot))
        {
            return 0;
        }
    }
    return 1;
}

static void set_handle(pal_audio_cache_handle_t *handle,
                       const pal_audio_cache_entry_t *entry,
                       uint16_t slot)
{
    handle->data = entry->data;
    handle->size = entry->size;
    handle->generation = entry->generation;
    handle->slot = slot;
}

void pal_audio_cache_init(pal_audio_cache_t *cache, size_t limit,
                          pal_audio_cache_size_fn get_size,
                          pal_audio_cache_load_fn load,
                          void *source_context,
                          pal_audio_cache_alloc_fn alloc,
                          pal_audio_cache_free_fn free_fn,
                          void *allocator_context)
{
    if (cache == NULL)
    {
        return;
    }
    memset(cache, 0, sizeof(*cache));
    cache->limit = limit == 0u || limit > PAL_AUDIO_CACHE_MAX_BYTES
                       ? PAL_AUDIO_CACHE_MAX_BYTES
                       : limit;
    cache->get_size = get_size;
    cache->load = load;
    cache->source_context = source_context;
    cache->alloc = alloc;
    cache->free = free_fn;
    cache->allocator_context = allocator_context;
}

int pal_audio_cache_acquire(pal_audio_cache_t *cache, uint8_t archive,
                            uint16_t chunk,
                            pal_audio_cache_handle_t *handle)
{
    pal_audio_cache_entry_t *entry;
    size_t size = 0u;
    uint16_t slot = PAL_AUDIO_CACHE_INVALID_SLOT;
    uint8_t *data;

    clear_handle(handle);
    if (cache == NULL || handle == NULL || cache->get_size == NULL ||
        cache->load == NULL || cache->alloc == NULL || cache->free == NULL)
    {
        return 0;
    }

    entry = find_entry(cache, archive, chunk, &slot);
    if (entry != NULL)
    {
        if (entry->references == UINT16_MAX)
        {
            ++cache->metrics.failures;
            return 0;
        }
        ++entry->references;
        entry->stamp = next_stamp(cache);
        ++cache->metrics.hits;
        set_handle(handle, entry, slot);
        return 1;
    }

    ++cache->metrics.misses;
    if (!cache->get_size(cache->source_context, archive, chunk, &size) ||
        size == 0u || size > cache->limit || !make_room(cache, size))
    {
        ++cache->metrics.failures;
        return 0;
    }

    slot = find_free_slot(cache);
    if (slot == PAL_AUDIO_CACHE_INVALID_SLOT)
    {
        slot = find_lru_slot(cache);
        if (slot == PAL_AUDIO_CACHE_INVALID_SLOT || !evict_slot(cache, slot))
        {
            ++cache->metrics.failures;
            return 0;
        }
    }

    data = (uint8_t *)cache->alloc(cache->allocator_context, size);
    if (data == NULL)
    {
        ++cache->metrics.failures;
        return 0;
    }
    if (!cache->load(cache->source_context, archive, chunk, data, size))
    {
        cache->free(cache->allocator_context, data);
        ++cache->metrics.failures;
        return 0;
    }

    entry = &cache->entries[slot];
    ++entry->generation;
    if (entry->generation == 0u)
    {
        entry->generation = 1u;
    }
    entry->data = data;
    entry->size = size;
    entry->stamp = next_stamp(cache);
    entry->references = 1u;
    entry->chunk = chunk;
    entry->archive = archive;
    entry->occupied = 1u;
    cache->metrics.current_bytes += size;
    if (cache->metrics.current_bytes > cache->metrics.peak_bytes)
    {
        cache->metrics.peak_bytes = cache->metrics.current_bytes;
    }
    set_handle(handle, entry, slot);
    return 1;
}

void pal_audio_cache_release(pal_audio_cache_t *cache,
                             pal_audio_cache_handle_t *handle)
{
    if (cache != NULL && handle != NULL &&
        handle->slot < PAL_AUDIO_CACHE_MAX_ENTRIES)
    {
        pal_audio_cache_entry_t *entry = &cache->entries[handle->slot];

        if (entry->occupied && entry->generation == handle->generation &&
            entry->data == handle->data && entry->references != 0u)
        {
            --entry->references;
            entry->stamp = next_stamp(cache);
        }
    }
    clear_handle(handle);
}

void pal_audio_cache_clear(pal_audio_cache_t *cache)
{
    uint16_t i;

    if (cache == NULL || cache->free == NULL)
    {
        return;
    }
    for (i = 0u; i < PAL_AUDIO_CACHE_MAX_ENTRIES; ++i)
    {
        pal_audio_cache_entry_t *entry = &cache->entries[i];

        if (entry->occupied)
        {
            uint32_t generation = entry->generation;

            cache->free(cache->allocator_context, entry->data);
            memset(entry, 0, sizeof(*entry));
            entry->generation = generation + 1u;
            if (entry->generation == 0u)
            {
                entry->generation = 1u;
            }
        }
    }
    cache->metrics.current_bytes = 0u;
}

size_t pal_audio_cache_limit(const pal_audio_cache_t *cache)
{
    return cache != NULL ? cache->limit : 0u;
}

void pal_audio_cache_metrics_get(const pal_audio_cache_t *cache,
                                 pal_audio_cache_metrics_t *metrics)
{
    if (metrics == NULL)
    {
        return;
    }
    if (cache == NULL)
    {
        memset(metrics, 0, sizeof(*metrics));
        return;
    }
    *metrics = cache->metrics;
}
