#ifndef PAL_SURFACE_STORAGE_H
#define PAL_SURFACE_STORAGE_H

#include <stddef.h>

#include "pal_memory_policy.h"

typedef enum pal_surface_storage_kind
{
    PAL_SURFACE_STORAGE_HOT = 0,
    PAL_SURFACE_STORAGE_GFX,
    PAL_SURFACE_STORAGE_COLD
} pal_surface_storage_kind_t;

void *pal_surface_alloc(size_t size, pal_memory_tag_t tag,
                        pal_surface_storage_kind_t *kind);
void pal_surface_free(void *pointer, size_t size, pal_memory_tag_t tag,
                      pal_surface_storage_kind_t kind);

#endif
