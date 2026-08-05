#include "pal_memory_policy.h"

#include <string.h>

typedef struct pal_memory_allocator
{
    pal_memory_alloc_fn alloc;
    pal_memory_free_fn free;
    void *context;
    pal_memory_stats_t stats;
} pal_memory_allocator_t;

static pal_memory_allocator_t allocators[PAL_MEMORY_CLASS_COUNT];

static pal_memory_tag_t normalize_tag(pal_memory_tag_t tag)
{
    return tag >= 0 && tag < PAL_MEMORY_TAG_COUNT
               ? tag
               : PAL_MEMORY_TAG_GENERAL;
}

static void counter_add(pal_memory_counter_t *counter, size_t size)
{
    counter->current_bytes += size;
    if (counter->current_bytes > counter->peak_bytes)
    {
        counter->peak_bytes = counter->current_bytes;
    }
}

static void counter_subtract(pal_memory_counter_t *counter, size_t size)
{
    if (size >= counter->current_bytes)
    {
        counter->current_bytes = 0u;
    }
    else
    {
        counter->current_bytes -= size;
    }
}

static void *allocate_from(pal_memory_class_t memory_class, size_t size,
                           pal_memory_tag_t tag)
{
    pal_memory_allocator_t *allocator = &allocators[memory_class];
    pal_memory_counter_t total;
    void *pointer;

    if (size == 0u || allocator->alloc == NULL)
    {
        ++allocator->stats.failed_allocations;
        return NULL;
    }

    pointer = allocator->alloc(allocator->context, size);
    if (pointer == NULL)
    {
        ++allocator->stats.failed_allocations;
        return NULL;
    }

    tag = normalize_tag(tag);
    total.current_bytes = allocator->stats.current_bytes;
    total.peak_bytes = allocator->stats.peak_bytes;
    counter_add(&total, size);
    allocator->stats.current_bytes = total.current_bytes;
    allocator->stats.peak_bytes = total.peak_bytes;
    counter_add(&allocator->stats.tags[tag], size);
    ++allocator->stats.successful_allocations;
    return pointer;
}

static void free_to(pal_memory_class_t memory_class, void *pointer,
                    size_t size, pal_memory_tag_t tag)
{
    pal_memory_allocator_t *allocator = &allocators[memory_class];
    pal_memory_counter_t total;

    if (pointer == NULL || allocator->free == NULL)
    {
        return;
    }

    allocator->free(allocator->context, pointer);
    tag = normalize_tag(tag);
    total.current_bytes = allocator->stats.current_bytes;
    total.peak_bytes = allocator->stats.peak_bytes;
    counter_subtract(&total, size);
    allocator->stats.current_bytes = total.current_bytes;
    counter_subtract(&allocator->stats.tags[tag], size);
}

void pal_memory_policy_configure(pal_memory_alloc_fn hot_alloc,
                                 pal_memory_free_fn hot_free,
                                 void *hot_context,
                                 pal_memory_alloc_fn cold_alloc,
                                 pal_memory_free_fn cold_free,
                                 void *cold_context)
{
    memset(allocators, 0, sizeof(allocators));
    allocators[PAL_MEMORY_CLASS_HOT].alloc = hot_alloc;
    allocators[PAL_MEMORY_CLASS_HOT].free = hot_free;
    allocators[PAL_MEMORY_CLASS_HOT].context = hot_context;
    allocators[PAL_MEMORY_CLASS_COLD].alloc = cold_alloc;
    allocators[PAL_MEMORY_CLASS_COLD].free = cold_free;
    allocators[PAL_MEMORY_CLASS_COLD].context = cold_context;
}

void *pal_hot_alloc(size_t size, pal_memory_tag_t tag)
{
    return allocate_from(PAL_MEMORY_CLASS_HOT, size, tag);
}

void pal_hot_free(void *pointer, size_t size, pal_memory_tag_t tag)
{
    free_to(PAL_MEMORY_CLASS_HOT, pointer, size, tag);
}

void *pal_cold_alloc(size_t size, pal_memory_tag_t tag)
{
    return allocate_from(PAL_MEMORY_CLASS_COLD, size, tag);
}

void pal_cold_free(void *pointer, size_t size, pal_memory_tag_t tag)
{
    free_to(PAL_MEMORY_CLASS_COLD, pointer, size, tag);
}

void pal_memory_stats_get(pal_memory_class_t memory_class,
                          pal_memory_stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }
    if (memory_class < 0 || memory_class >= PAL_MEMORY_CLASS_COUNT)
    {
        memset(stats, 0, sizeof(*stats));
        return;
    }
    *stats = allocators[memory_class].stats;
}
