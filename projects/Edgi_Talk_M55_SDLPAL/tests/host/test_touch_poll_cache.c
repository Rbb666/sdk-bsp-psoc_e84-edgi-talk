#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "pal_touch_poll_cache.h"

static void test_first_sample_and_interval(void)
{
    pal_touch_poll_cache_t cache;

    pal_touch_poll_cache_init(&cache);

    assert(pal_touch_poll_cache_sample_due(&cache, 0u));
    assert(pal_touch_poll_cache_sample_due(&cache, 999u));

    pal_touch_poll_cache_store(&cache, 100u, NULL, 0u);
    assert(!pal_touch_poll_cache_sample_due(&cache, 100u));
    assert(!pal_touch_poll_cache_sample_due(&cache, 109u));
    assert(pal_touch_poll_cache_sample_due(&cache, 110u));
}

static void test_cached_press_and_release(void)
{
    pal_touch_poll_cache_t cache;
    pal_touch_point_t pressed[2] = {
        {11u, 22u, 3u, 1u},
        {33u, 44u, 4u, 1u},
    };
    pal_touch_point_t output[PAL_TOUCH_MAX_POINTS] = {0};
    size_t count;

    pal_touch_poll_cache_init(&cache);
    pal_touch_poll_cache_store(&cache, 0u, pressed, 2u);

    count = pal_touch_poll_cache_copy(&cache, output, 1u);
    assert(count == 1u);
    assert(output[0].x == 11u);
    assert(output[0].y == 22u);
    assert(output[0].id == 3u);
    assert(output[0].active == 1u);

    count = pal_touch_poll_cache_copy(&cache, output,
                                      PAL_TOUCH_MAX_POINTS);
    assert(count == 2u);
    assert(output[1].x == 33u);
    assert(output[1].y == 44u);

    pal_touch_poll_cache_store(&cache, 10u, NULL, 0u);
    assert(pal_touch_poll_cache_copy(&cache, output,
                                     PAL_TOUCH_MAX_POINTS) == 0u);
}

static void test_capacity_is_bounded(void)
{
    pal_touch_poll_cache_t cache;
    pal_touch_point_t input[PAL_TOUCH_MAX_POINTS + 2u];
    pal_touch_point_t output[PAL_TOUCH_MAX_POINTS];
    size_t i;

    for (i = 0u; i < PAL_TOUCH_MAX_POINTS + 2u; ++i)
    {
        input[i].x = (uint16_t)(100u + i);
        input[i].y = (uint16_t)(200u + i);
        input[i].id = (uint8_t)i;
        input[i].active = 1u;
    }

    pal_touch_poll_cache_init(&cache);
    pal_touch_poll_cache_store(&cache, 20u, input,
                               PAL_TOUCH_MAX_POINTS + 2u);

    assert(pal_touch_poll_cache_copy(&cache, output,
                                     PAL_TOUCH_MAX_POINTS) ==
           PAL_TOUCH_MAX_POINTS);
    assert(output[PAL_TOUCH_MAX_POINTS - 1u].id ==
           PAL_TOUCH_MAX_POINTS - 1u);
    assert(pal_touch_poll_cache_copy(&cache, NULL, 0u) == 0u);
}

static void test_tick_wraparound(void)
{
    pal_touch_poll_cache_t cache;

    pal_touch_poll_cache_init(&cache);
    pal_touch_poll_cache_store(&cache, UINT32_MAX - 4u, NULL, 0u);

    assert(!pal_touch_poll_cache_sample_due(&cache, 4u));
    assert(pal_touch_poll_cache_sample_due(&cache, 5u));
}

int main(void)
{
    test_first_sample_and_interval();
    test_cached_press_and_release();
    test_capacity_is_bounded();
    test_tick_wraparound();
    puts("touch poll cache tests passed");
    return 0;
}
