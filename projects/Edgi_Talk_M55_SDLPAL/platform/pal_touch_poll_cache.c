#include "pal_touch_poll_cache.h"

void pal_touch_poll_cache_init(pal_touch_poll_cache_t *cache)
{
    if (cache == NULL)
    {
        return;
    }

    cache->count = 0u;
    cache->last_sample_ms = 0u;
    cache->initialized = false;
}

bool pal_touch_poll_cache_sample_due(const pal_touch_poll_cache_t *cache,
                                     uint32_t now_ms)
{
    if (cache == NULL || !cache->initialized)
    {
        return true;
    }

    return (uint32_t)(now_ms - cache->last_sample_ms) >=
           PAL_TOUCH_POLL_INTERVAL_MS;
}

void pal_touch_poll_cache_store(pal_touch_poll_cache_t *cache,
                                uint32_t now_ms,
                                const pal_touch_point_t *points,
                                size_t count)
{
    size_t i;

    if (cache == NULL)
    {
        return;
    }

    if (points == NULL)
    {
        count = 0u;
    }
    else if (count > PAL_TOUCH_MAX_POINTS)
    {
        count = PAL_TOUCH_MAX_POINTS;
    }

    for (i = 0u; i < count; ++i)
    {
        cache->points[i] = points[i];
    }
    cache->count = count;
    cache->last_sample_ms = now_ms;
    cache->initialized = true;
}

size_t pal_touch_poll_cache_copy(const pal_touch_poll_cache_t *cache,
                                 pal_touch_point_t *points,
                                 size_t capacity)
{
    size_t count;
    size_t i;

    if (cache == NULL || points == NULL || capacity == 0u)
    {
        return 0u;
    }

    count = cache->count;
    if (count > capacity)
    {
        count = capacity;
    }

    for (i = 0u; i < count; ++i)
    {
        points[i] = cache->points[i];
    }

    return count;
}
