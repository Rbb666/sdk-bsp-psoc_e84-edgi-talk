#ifndef PAL_ENGINE_HEAP_H
#define PAL_ENGINE_HEAP_H

#include <stddef.h>

typedef void *(*pal_engine_malloc_fn)(size_t size);
typedef void *(*pal_engine_calloc_fn)(size_t count, size_t size);
typedef void *(*pal_engine_realloc_fn)(void *pointer, size_t size);
typedef void (*pal_engine_free_fn)(void *pointer);

typedef struct pal_engine_heap_primary_ops
{
    pal_engine_malloc_fn malloc_fn;
    pal_engine_calloc_fn calloc_fn;
    pal_engine_realloc_fn realloc_fn;
    pal_engine_free_fn free_fn;
} pal_engine_heap_primary_ops_t;

void pal_engine_heap_set_primary_ops(
    const pal_engine_heap_primary_ops_t *operations);
void *pal_engine_heap_malloc(size_t size);
void *pal_engine_heap_fallback_malloc(size_t size);
void *pal_engine_heap_calloc(size_t count, size_t size);
void *pal_engine_heap_realloc(void *pointer, size_t size);
void pal_engine_heap_free(void *pointer);

#endif
