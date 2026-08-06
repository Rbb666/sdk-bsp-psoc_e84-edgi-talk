#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "pal_surface_storage.h"

static unsigned hot_allocations;
static unsigned cold_allocations;

static void *test_hot_alloc(void *context, size_t size)
{
    (void)context;
    (void)size;
    ++hot_allocations;
    return NULL;
}

static void test_hot_free(void *context, void *pointer)
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
    free(pointer);
}

int main(void)
{
    pal_surface_storage_kind_t first_kind;
    pal_surface_storage_kind_t second_kind;
    pal_surface_storage_kind_t third_kind;
    void *first;
    void *second;
    void *third;

    pal_memory_policy_configure(test_hot_alloc, test_hot_free, NULL,
                                test_cold_alloc, test_cold_free, NULL);

    first = pal_surface_alloc(64000u, PAL_MEMORY_TAG_SURFACE, &first_kind);
    second = pal_surface_alloc(64000u, PAL_MEMORY_TAG_SURFACE, &second_kind);
    third = pal_surface_alloc(64000u, PAL_MEMORY_TAG_SURFACE, &third_kind);

    assert(first != NULL && first_kind == PAL_SURFACE_STORAGE_GFX);
    assert(second != NULL && second_kind == PAL_SURFACE_STORAGE_GFX);
    assert(third != NULL && third_kind == PAL_SURFACE_STORAGE_COLD);
    assert(hot_allocations == 3u);
    assert(cold_allocations == 1u);

    pal_surface_free(first, 64000u, PAL_MEMORY_TAG_SURFACE, first_kind);
    pal_surface_free(second, 64000u, PAL_MEMORY_TAG_SURFACE, second_kind);
    pal_surface_free(third, 64000u, PAL_MEMORY_TAG_SURFACE, third_kind);

    puts("surface_storage: PASS");
    return 0;
}
