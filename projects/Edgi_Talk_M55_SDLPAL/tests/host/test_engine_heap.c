#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pal_engine_heap.h"
#include "pal_memory_policy.h"

static unsigned primary_malloc_calls;
static unsigned primary_calloc_calls;
static unsigned primary_realloc_calls;
static unsigned primary_free_calls;
static unsigned cold_allocations;
static unsigned cold_frees;
static int primary_fail = 1;

static void *test_primary_malloc(size_t size)
{
    ++primary_malloc_calls;
    return primary_fail ? NULL : malloc(size);
}

static void *test_primary_calloc(size_t count, size_t size)
{
    ++primary_calloc_calls;
    return primary_fail ? NULL : calloc(count, size);
}

static void *test_primary_realloc(void *pointer, size_t size)
{
    ++primary_realloc_calls;
    return primary_fail ? NULL : realloc(pointer, size);
}

static void test_primary_free(void *pointer)
{
    ++primary_free_calls;
    free(pointer);
}

static void *test_policy_fail_alloc(void *context, size_t size)
{
    (void)context;
    (void)size;
    return NULL;
}

static void test_policy_fail_free(void *context, void *pointer)
{
    (void)context;
    (void)pointer;
}

static void *test_cold_alloc(void *context, size_t size)
{
    (void)context;
    ++cold_allocations;
    return malloc(size);
}

static void test_cold_free(void *context, void *pointer)
{
    (void)context;
    ++cold_frees;
    free(pointer);
}

int main(void)
{
    const pal_engine_heap_primary_ops_t primary_ops = {
        test_primary_malloc,
        test_primary_calloc,
        test_primary_realloc,
        test_primary_free,
    };
    pal_memory_stats_t cold_stats;
    unsigned char *first;
    unsigned char *second;
    unsigned char *third;
    unsigned char *zeroed;
    unsigned char *resized;
    unsigned char *primary;
    void *large_fallback;
    size_t i;

    pal_engine_heap_set_primary_ops(&primary_ops);
    pal_memory_policy_configure(test_policy_fail_alloc,
                                test_policy_fail_free, NULL,
                                test_cold_alloc, test_cold_free, NULL);

    first = (unsigned char *)pal_engine_heap_malloc(21246u);
    second = (unsigned char *)pal_engine_heap_malloc(64u * 1024u);
    third = (unsigned char *)pal_engine_heap_malloc(21246u);
    assert(first != NULL);
    assert(second != NULL);
    assert(third != NULL);
    assert(cold_allocations == 1u);

    zeroed = (unsigned char *)pal_engine_heap_calloc(53u, 401u);
    assert(zeroed != NULL);
    for (i = 0u; i < 53u * 401u; ++i)
    {
        assert(zeroed[i] == 0u);
    }

    memset(third, 0x5a, 21246u);
    resized = (unsigned char *)pal_engine_heap_realloc(third, 30000u);
    assert(resized != NULL);
    for (i = 0u; i < 21246u; ++i)
    {
        assert(resized[i] == 0x5a);
    }
    assert(cold_allocations == 3u);
    assert(cold_frees == 1u);

    pal_engine_heap_free(first);
    pal_engine_heap_free(second);
    pal_engine_heap_free(zeroed);
    pal_engine_heap_free(resized);
    assert(cold_frees == 3u);
    pal_memory_stats_get(PAL_MEMORY_CLASS_COLD, &cold_stats);
    assert(cold_stats.current_bytes == 0u);

    assert(pal_engine_heap_calloc(SIZE_MAX, 2u) == NULL);

    large_fallback = pal_engine_heap_fallback_malloc(180u * 1024u);
    assert(large_fallback != NULL);
    assert(cold_allocations == 4u);
    pal_engine_heap_free(large_fallback);
    assert(cold_frees == 4u);

    primary_fail = 0;
    primary = (unsigned char *)pal_engine_heap_malloc(32u);
    assert(primary != NULL);
    primary[0] = 0x33u;
    primary = (unsigned char *)pal_engine_heap_realloc(primary, 64u);
    assert(primary != NULL && primary[0] == 0x33u);
    pal_engine_heap_free(primary);
    assert(primary_malloc_calls >= 4u);
    assert(primary_calloc_calls == 1u);
    assert(primary_realloc_calls == 1u);
    assert(primary_free_calls == 1u);

    puts("engine_heap: PASS");
    return 0;
}
