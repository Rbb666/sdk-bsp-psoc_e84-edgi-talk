#include "pal_engine_heap.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pal_memory_policy.h"

#define PAL_ENGINE_GFX_SLOT_BYTES (64u * 1024u)
#define PAL_ENGINE_GFX_SLOTS 2u
#define PAL_ENGINE_COLD_RECORDS 64u

#if defined(__GNUC__) || defined(__clang__)
#define PAL_ENGINE_GFX_RESOURCE \
    __attribute__((section(".cy_gpu_buf.sdlpal_resource"), aligned(64)))
#else
#error "SDLPal resource pool requires a compiler-specific GFX SRAM section"
#endif

typedef struct pal_engine_cold_record
{
    void *pointer;
    size_t size;
} pal_engine_cold_record_t;

static PAL_ENGINE_GFX_RESOURCE uint8_t
    resource_pool[PAL_ENGINE_GFX_SLOTS][PAL_ENGINE_GFX_SLOT_BYTES];
static size_t resource_sizes[PAL_ENGINE_GFX_SLOTS];
static pal_engine_cold_record_t cold_records[PAL_ENGINE_COLD_RECORDS];

static const pal_engine_heap_primary_ops_t default_primary_operations = {
    malloc,
    calloc,
    realloc,
    free,
};
static pal_engine_heap_primary_ops_t primary_operations = {
    malloc,
    calloc,
    realloc,
    free,
};

static int gfx_slot_index(const void *pointer)
{
    uintptr_t address;
    uintptr_t start;
    uintptr_t end;
    uintptr_t offset;

    if (pointer == NULL)
    {
        return -1;
    }

    address = (uintptr_t)pointer;
    start = (uintptr_t)&resource_pool[0][0];
    end = start + sizeof(resource_pool);
    if (address < start || address >= end)
    {
        return -1;
    }

    offset = address - start;
    if ((offset % PAL_ENGINE_GFX_SLOT_BYTES) != 0u)
    {
        return -1;
    }

    return (int)(offset / PAL_ENGINE_GFX_SLOT_BYTES);
}

static int cold_record_index(const void *pointer)
{
    unsigned int index;

    for (index = 0u; index < PAL_ENGINE_COLD_RECORDS; ++index)
    {
        if (cold_records[index].pointer == pointer)
        {
            return (int)index;
        }
    }
    return -1;
}

static int free_cold_record_index(void)
{
    unsigned int index;

    for (index = 0u; index < PAL_ENGINE_COLD_RECORDS; ++index)
    {
        if (cold_records[index].pointer == NULL)
        {
            return (int)index;
        }
    }
    return -1;
}

static void *fallback_allocate(size_t size, int zero_initialize)
{
    unsigned int index;
    int record_index;
    void *pointer;

    if (size == 0u)
    {
        return NULL;
    }

    if (size <= PAL_ENGINE_GFX_SLOT_BYTES)
    {
        for (index = 0u; index < PAL_ENGINE_GFX_SLOTS; ++index)
        {
            if (resource_sizes[index] == 0u)
            {
                resource_sizes[index] = size;
                pointer = resource_pool[index];
                if (zero_initialize)
                {
                    memset(pointer, 0, size);
                }
                return pointer;
            }
        }
    }

    record_index = free_cold_record_index();
    if (record_index < 0)
    {
        return NULL;
    }

    pointer = pal_cold_alloc(size, PAL_MEMORY_TAG_RESOURCE);
    if (pointer == NULL)
    {
        return NULL;
    }
    cold_records[record_index].pointer = pointer;
    cold_records[record_index].size = size;
    if (zero_initialize)
    {
        memset(pointer, 0, size);
    }
    return pointer;
}

void pal_engine_heap_set_primary_ops(
    const pal_engine_heap_primary_ops_t *operations)
{
    if (operations == NULL)
    {
        primary_operations = default_primary_operations;
        return;
    }
    if (operations->malloc_fn == NULL || operations->calloc_fn == NULL ||
        operations->realloc_fn == NULL || operations->free_fn == NULL)
    {
        return;
    }
    primary_operations = *operations;
}

void *pal_engine_heap_malloc(size_t size)
{
    void *pointer;

    if (size == 0u)
    {
        return NULL;
    }

    pointer = primary_operations.malloc_fn(size);
    return pointer != NULL ? pointer : fallback_allocate(size, 0);
}

void *pal_engine_heap_fallback_malloc(size_t size)
{
    return fallback_allocate(size, 0);
}

void *pal_engine_heap_calloc(size_t count, size_t size)
{
    void *pointer;
    size_t total_size;

    if (count == 0u || size == 0u || count > SIZE_MAX / size)
    {
        return NULL;
    }

    pointer = primary_operations.calloc_fn(count, size);
    if (pointer != NULL)
    {
        return pointer;
    }

    total_size = count * size;
    return fallback_allocate(total_size, 1);
}

void *pal_engine_heap_realloc(void *pointer, size_t size)
{
    int index;
    size_t old_size;
    void *replacement;

    if (pointer == NULL)
    {
        return pal_engine_heap_malloc(size);
    }
    if (size == 0u)
    {
        pal_engine_heap_free(pointer);
        return NULL;
    }

    index = gfx_slot_index(pointer);
    if (index >= 0)
    {
        old_size = resource_sizes[index];
    }
    else
    {
        index = cold_record_index(pointer);
        if (index < 0)
        {
            return primary_operations.realloc_fn(pointer, size);
        }
        old_size = cold_records[index].size;
    }

    replacement = pal_engine_heap_malloc(size);
    if (replacement == NULL)
    {
        return NULL;
    }
    memcpy(replacement, pointer, old_size < size ? old_size : size);
    pal_engine_heap_free(pointer);
    return replacement;
}

void pal_engine_heap_free(void *pointer)
{
    int index;

    if (pointer == NULL)
    {
        return;
    }

    index = gfx_slot_index(pointer);
    if (index >= 0)
    {
        resource_sizes[index] = 0u;
        return;
    }

    index = cold_record_index(pointer);
    if (index >= 0)
    {
        pal_cold_free(pointer, cold_records[index].size,
                      PAL_MEMORY_TAG_RESOURCE);
        cold_records[index].pointer = NULL;
        cold_records[index].size = 0u;
        return;
    }

    primary_operations.free_fn(pointer);
}
