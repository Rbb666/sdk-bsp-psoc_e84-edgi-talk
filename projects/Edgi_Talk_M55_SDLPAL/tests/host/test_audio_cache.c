#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pal_audio_cache.h"

typedef struct fake_resource
{
    size_t size;
    uint8_t seed;
    int fail_load;
} fake_resource_t;

typedef struct fake_context
{
    fake_resource_t resources[32];
    unsigned allocations;
    unsigned frees;
    int fail_alloc;
} fake_context_t;

static void *fake_alloc(void *context, size_t size)
{
    fake_context_t *fake = (fake_context_t *)context;

    ++fake->allocations;
    return fake->fail_alloc ? NULL : malloc(size);
}

static void fake_free(void *context, void *pointer)
{
    fake_context_t *fake = (fake_context_t *)context;

    ++fake->frees;
    free(pointer);
}

static int fake_size(void *context, uint8_t archive, uint16_t chunk,
                     size_t *size)
{
    fake_context_t *fake = (fake_context_t *)context;
    size_t index = (size_t)archive * 16u + chunk;

    if (size == NULL || index >= 32u || fake->resources[index].size == 0u)
    {
        return 0;
    }
    *size = fake->resources[index].size;
    return 1;
}

static int fake_load(void *context, uint8_t archive, uint16_t chunk,
                     void *destination, size_t size)
{
    fake_context_t *fake = (fake_context_t *)context;
    size_t index = (size_t)archive * 16u + chunk;
    fake_resource_t *resource;

    if (index >= 32u)
    {
        return 0;
    }
    resource = &fake->resources[index];
    if (resource->fail_load || resource->size != size)
    {
        return 0;
    }
    memset(destination, resource->seed, size);
    return 1;
}

static void set_resource(fake_context_t *fake, uint8_t archive,
                         uint16_t chunk, size_t size, uint8_t seed)
{
    size_t index = (size_t)archive * 16u + chunk;

    assert(index < 32u);
    fake->resources[index].size = size;
    fake->resources[index].seed = seed;
}

static void init_cache(pal_audio_cache_t *cache, fake_context_t *fake,
                       size_t limit)
{
    pal_audio_cache_init(cache, limit, fake_size, fake_load, fake,
                         fake_alloc, fake_free, fake);
}

static void test_hit_and_reference_lifecycle(void)
{
    pal_audio_cache_t cache;
    pal_audio_cache_handle_t first;
    pal_audio_cache_handle_t second;
    pal_audio_cache_metrics_t metrics;
    fake_context_t fake = {0};

    set_resource(&fake, 0u, 1u, 16u, 0x31u);
    init_cache(&cache, &fake, 64u);

    assert(pal_audio_cache_acquire(&cache, 0u, 1u, &first));
    assert(first.data != NULL);
    assert(first.size == 16u);
    assert(first.data[0] == 0x31u);
    assert(pal_audio_cache_acquire(&cache, 0u, 1u, &second));
    assert(second.data == first.data);
    assert(fake.allocations == 1u);

    pal_audio_cache_metrics_get(&cache, &metrics);
    assert(metrics.hits == 1u);
    assert(metrics.misses == 1u);
    assert(metrics.current_bytes == 16u);
    assert(metrics.peak_bytes == 16u);

    pal_audio_cache_release(&cache, &first);
    pal_audio_cache_release(&cache, &second);
    assert(first.data == NULL);
    assert(second.data == NULL);
    pal_audio_cache_clear(&cache);
    assert(fake.frees == 1u);
}

static void test_lru_evicts_only_inactive_entries(void)
{
    pal_audio_cache_t cache;
    pal_audio_cache_handle_t first;
    pal_audio_cache_handle_t second;
    pal_audio_cache_handle_t third;
    pal_audio_cache_handle_t hit;
    pal_audio_cache_metrics_t metrics;
    fake_context_t fake = {0};

    set_resource(&fake, 0u, 1u, 24u, 0x11u);
    set_resource(&fake, 0u, 2u, 24u, 0x22u);
    set_resource(&fake, 0u, 3u, 24u, 0x33u);
    init_cache(&cache, &fake, 64u);

    assert(pal_audio_cache_acquire(&cache, 0u, 1u, &first));
    assert(pal_audio_cache_acquire(&cache, 0u, 2u, &second));
    pal_audio_cache_release(&cache, &first);
    assert(pal_audio_cache_acquire(&cache, 0u, 2u, &hit));
    pal_audio_cache_release(&cache, &hit);
    pal_audio_cache_release(&cache, &second);

    assert(pal_audio_cache_acquire(&cache, 0u, 3u, &third));
    pal_audio_cache_metrics_get(&cache, &metrics);
    assert(metrics.evictions == 1u);
    assert(metrics.current_bytes == 48u);
    assert(third.data[0] == 0x33u);

    pal_audio_cache_release(&cache, &third);
    pal_audio_cache_clear(&cache);
    assert(fake.frees == 3u);
}

static void test_active_entry_blocks_overcommit(void)
{
    pal_audio_cache_t cache;
    pal_audio_cache_handle_t active;
    pal_audio_cache_handle_t rejected = {0};
    pal_audio_cache_metrics_t metrics;
    fake_context_t fake = {0};

    set_resource(&fake, 0u, 1u, 24u, 0x11u);
    set_resource(&fake, 0u, 2u, 16u, 0x22u);
    init_cache(&cache, &fake, 32u);

    assert(pal_audio_cache_acquire(&cache, 0u, 1u, &active));
    assert(!pal_audio_cache_acquire(&cache, 0u, 2u, &rejected));
    assert(rejected.data == NULL);
    pal_audio_cache_metrics_get(&cache, &metrics);
    assert(metrics.current_bytes == 24u);
    assert(metrics.failures == 1u);
    assert(fake.allocations == 1u);

    pal_audio_cache_release(&cache, &active);
    pal_audio_cache_clear(&cache);
}

static void test_load_and_allocate_failures_are_clean(void)
{
    pal_audio_cache_t cache;
    pal_audio_cache_handle_t handle = {0};
    pal_audio_cache_metrics_t metrics;
    fake_context_t fake = {0};

    set_resource(&fake, 0u, 1u, 8u, 0x11u);
    fake.resources[1].fail_load = 1;
    init_cache(&cache, &fake, 64u);
    assert(!pal_audio_cache_acquire(&cache, 0u, 1u, &handle));
    assert(fake.allocations == 1u);
    assert(fake.frees == 1u);

    fake.resources[1].fail_load = 0;
    fake.fail_alloc = 1;
    assert(!pal_audio_cache_acquire(&cache, 0u, 1u, &handle));
    pal_audio_cache_metrics_get(&cache, &metrics);
    assert(metrics.current_bytes == 0u);
    assert(metrics.failures == 2u);
}

static void test_limit_is_hard_capped_at_one_mib(void)
{
    pal_audio_cache_t cache;
    fake_context_t fake = {0};

    init_cache(&cache, &fake, PAL_AUDIO_CACHE_MAX_BYTES + 4096u);
    assert(pal_audio_cache_limit(&cache) == PAL_AUDIO_CACHE_MAX_BYTES);
}

int main(void)
{
    test_hit_and_reference_lifecycle();
    test_lru_evicts_only_inactive_entries();
    test_active_entry_blocks_overcommit();
    test_load_and_allocate_failures_are_clean();
    test_limit_is_hard_capped_at_one_mib();
    puts("audio_cache: PASS");
    return 0;
}
