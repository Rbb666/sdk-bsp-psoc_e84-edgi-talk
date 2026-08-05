#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "pal_memory_policy.h"

typedef struct fake_allocator
{
    unsigned char storage[64];
    size_t alloc_calls;
    size_t free_calls;
    int fail;
} fake_allocator_t;

static void *fake_alloc(void *context, size_t size)
{
    fake_allocator_t *allocator = (fake_allocator_t *)context;

    ++allocator->alloc_calls;
    if (allocator->fail || size > sizeof(allocator->storage))
    {
        return NULL;
    }
    return allocator->storage;
}

static void fake_free(void *context, void *pointer)
{
    fake_allocator_t *allocator = (fake_allocator_t *)context;

    assert(pointer == allocator->storage);
    ++allocator->free_calls;
}

int main(void)
{
    fake_allocator_t hot = {{0}, 0u, 0u, 0};
    fake_allocator_t cold = {{0}, 0u, 0u, 0};
    pal_memory_stats_t stats;
    void *first;
    void *second;

    pal_memory_policy_configure(fake_alloc, fake_free, &hot,
                                fake_alloc, fake_free, &cold);

    first = pal_hot_alloc(24u, PAL_MEMORY_TAG_SURFACE);
    second = pal_hot_alloc(16u, PAL_MEMORY_TAG_SURFACE);
    assert(first != NULL);
    assert(second != NULL);
    assert(hot.alloc_calls == 2u);
    assert(cold.alloc_calls == 0u);

    pal_memory_stats_get(PAL_MEMORY_CLASS_HOT, &stats);
    assert(stats.current_bytes == 40u);
    assert(stats.peak_bytes == 40u);
    assert(stats.tags[PAL_MEMORY_TAG_SURFACE].current_bytes == 40u);
    assert(stats.tags[PAL_MEMORY_TAG_SURFACE].peak_bytes == 40u);

    pal_hot_free(first, 24u, PAL_MEMORY_TAG_SURFACE);
    pal_hot_free(second, 16u, PAL_MEMORY_TAG_SURFACE);
    pal_memory_stats_get(PAL_MEMORY_CLASS_HOT, &stats);
    assert(stats.current_bytes == 0u);
    assert(stats.peak_bytes == 40u);
    assert(hot.free_calls == 2u);

    hot.fail = 1;
    assert(pal_hot_alloc(8u, PAL_MEMORY_TAG_GENERAL) == NULL);
    assert(hot.alloc_calls == 3u);
    assert(cold.alloc_calls == 0u);
    pal_memory_stats_get(PAL_MEMORY_CLASS_HOT, &stats);
    assert(stats.failed_allocations == 1u);

    first = pal_cold_alloc(32u, PAL_MEMORY_TAG_RESOURCE);
    assert(first != NULL);
    assert(cold.alloc_calls == 1u);
    pal_memory_stats_get(PAL_MEMORY_CLASS_COLD, &stats);
    assert(stats.current_bytes == 32u);
    assert(stats.tags[PAL_MEMORY_TAG_RESOURCE].peak_bytes == 32u);
    pal_cold_free(first, 32u, PAL_MEMORY_TAG_RESOURCE);
    assert(cold.free_calls == 1u);

    return 0;
}
