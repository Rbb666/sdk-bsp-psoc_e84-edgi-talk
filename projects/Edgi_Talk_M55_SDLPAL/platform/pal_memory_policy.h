#ifndef PAL_MEMORY_POLICY_H
#define PAL_MEMORY_POLICY_H

#include <stddef.h>

typedef enum pal_memory_class
{
    PAL_MEMORY_CLASS_HOT = 0,
    PAL_MEMORY_CLASS_COLD,
    PAL_MEMORY_CLASS_COUNT
} pal_memory_class_t;

typedef enum pal_memory_tag
{
    PAL_MEMORY_TAG_GENERAL = 0,
    PAL_MEMORY_TAG_SURFACE,
    PAL_MEMORY_TAG_RESOURCE,
    PAL_MEMORY_TAG_FONT,
    PAL_MEMORY_TAG_COUNT
} pal_memory_tag_t;

typedef struct pal_memory_counter
{
    size_t current_bytes;
    size_t peak_bytes;
} pal_memory_counter_t;

typedef struct pal_memory_stats
{
    size_t current_bytes;
    size_t peak_bytes;
    size_t successful_allocations;
    size_t failed_allocations;
    pal_memory_counter_t tags[PAL_MEMORY_TAG_COUNT];
} pal_memory_stats_t;

typedef void *(*pal_memory_alloc_fn)(void *context, size_t size);
typedef void (*pal_memory_free_fn)(void *context, void *pointer);

void pal_memory_policy_configure(pal_memory_alloc_fn hot_alloc,
                                 pal_memory_free_fn hot_free,
                                 void *hot_context,
                                 pal_memory_alloc_fn cold_alloc,
                                 pal_memory_free_fn cold_free,
                                 void *cold_context);
void *pal_hot_alloc(size_t size, pal_memory_tag_t tag);
void pal_hot_free(void *pointer, size_t size, pal_memory_tag_t tag);
void *pal_cold_alloc(size_t size, pal_memory_tag_t tag);
void pal_cold_free(void *pointer, size_t size, pal_memory_tag_t tag);
void pal_memory_stats_get(pal_memory_class_t memory_class,
                          pal_memory_stats_t *stats);

#endif
