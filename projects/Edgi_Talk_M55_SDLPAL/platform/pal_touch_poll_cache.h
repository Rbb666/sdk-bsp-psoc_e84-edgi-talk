#ifndef PAL_TOUCH_POLL_CACHE_H
#define PAL_TOUCH_POLL_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pal_touch_core.h"

#define PAL_TOUCH_POLL_INTERVAL_MS 10u

typedef struct pal_touch_poll_cache
{
    pal_touch_point_t points[PAL_TOUCH_MAX_POINTS];
    size_t count;
    uint32_t last_sample_ms;
    bool initialized;
} pal_touch_poll_cache_t;

void pal_touch_poll_cache_init(pal_touch_poll_cache_t *cache);
bool pal_touch_poll_cache_sample_due(const pal_touch_poll_cache_t *cache,
                                     uint32_t now_ms);
void pal_touch_poll_cache_store(pal_touch_poll_cache_t *cache,
                                uint32_t now_ms,
                                const pal_touch_point_t *points,
                                size_t count);
size_t pal_touch_poll_cache_copy(const pal_touch_poll_cache_t *cache,
                                 pal_touch_point_t *points,
                                 size_t capacity);

#endif
