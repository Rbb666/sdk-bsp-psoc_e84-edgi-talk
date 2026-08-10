#ifndef PAL_TEST_RTTHREAD_H
#define PAL_TEST_RTTHREAD_H

#include <stdint.h>

typedef int32_t rt_int32_t;
typedef uint32_t rt_tick_t;
typedef int rt_err_t;

#define RT_TICK_PER_SECOND 1000u
#define RT_TICK_MAX UINT32_MAX

rt_tick_t rt_tick_get(void);
rt_tick_t rt_tick_get_millisecond(void);
rt_err_t rt_thread_mdelay(rt_int32_t milliseconds);

#endif
