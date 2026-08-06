#include "pal_surface_storage.h"

#include <stdint.h>

#define PAL_SURFACE_STORAGE_SLOT_BYTES (64u * 1024u)
#define PAL_SURFACE_STORAGE_SLOTS 2u

#if defined(__GNUC__) || defined(__clang__)
#define PAL_SURFACE_GFX __attribute__((section(".cy_gpu_buf.sdlpal_surface"), aligned(64)))
#else
#define PAL_SURFACE_GFX
#endif

static PAL_SURFACE_GFX uint8_t
    pal_surface_gfx_pool[PAL_SURFACE_STORAGE_SLOTS]
                         [PAL_SURFACE_STORAGE_SLOT_BYTES];
static uint8_t pal_surface_gfx_used[PAL_SURFACE_STORAGE_SLOTS];

static void *gfx_alloc(size_t size)
{
    unsigned int index;

    if (size == 0u || size > PAL_SURFACE_STORAGE_SLOT_BYTES)
    {
        return NULL;
    }

    for (index = 0u; index < PAL_SURFACE_STORAGE_SLOTS; ++index)
    {
        if (!pal_surface_gfx_used[index])
        {
            pal_surface_gfx_used[index] = 1u;
            return pal_surface_gfx_pool[index];
        }
    }

    return NULL;
}

static int gfx_index(const void *pointer)
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
    start = (uintptr_t)&pal_surface_gfx_pool[0][0];
    end = (uintptr_t)&pal_surface_gfx_pool[PAL_SURFACE_STORAGE_SLOTS][0];
    if (address < start || address >= end)
    {
        return -1;
    }

    offset = address - start;
    if ((offset % PAL_SURFACE_STORAGE_SLOT_BYTES) != 0u)
    {
        return -1;
    }

    return (int)(offset / PAL_SURFACE_STORAGE_SLOT_BYTES);
}

void *pal_surface_alloc(size_t size, pal_memory_tag_t tag,
                        pal_surface_storage_kind_t *kind)
{
    void *pointer;

    if (kind == NULL)
    {
        return NULL;
    }

    pointer = pal_hot_alloc(size, tag);
    if (pointer != NULL)
    {
        *kind = PAL_SURFACE_STORAGE_HOT;
        return pointer;
    }

    pointer = gfx_alloc(size);
    if (pointer != NULL)
    {
        *kind = PAL_SURFACE_STORAGE_GFX;
        return pointer;
    }

    pointer = pal_cold_alloc(size, tag);
    if (pointer != NULL)
    {
        *kind = PAL_SURFACE_STORAGE_COLD;
        return pointer;
    }

    return NULL;
}

void pal_surface_free(void *pointer, size_t size, pal_memory_tag_t tag,
                      pal_surface_storage_kind_t kind)
{
    int index;

    if (pointer == NULL)
    {
        return;
    }

    if (kind == PAL_SURFACE_STORAGE_HOT)
    {
        pal_hot_free(pointer, size, tag);
        return;
    }
    if (kind == PAL_SURFACE_STORAGE_COLD)
    {
        pal_cold_free(pointer, size, tag);
        return;
    }

    index = gfx_index(pointer);
    if (index >= 0 && index < (int)PAL_SURFACE_STORAGE_SLOTS)
    {
        pal_surface_gfx_used[index] = 0u;
    }
}
