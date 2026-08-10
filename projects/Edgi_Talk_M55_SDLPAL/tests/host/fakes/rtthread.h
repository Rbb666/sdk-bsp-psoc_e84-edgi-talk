#ifndef PAL_TEST_RTTHREAD_H
#define PAL_TEST_RTTHREAD_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t rt_int32_t;
typedef uint32_t rt_tick_t;

#define RT_NULL NULL
#define RT_TICK_MAX UINT32_MAX
#define RT_TICK_PER_SECOND 1000u

uint32_t rt_tick_get_millisecond(void);
rt_tick_t rt_tick_get(void);
void rt_thread_mdelay(rt_int32_t milliseconds);
int rt_kprintf(const char *format, ...);

#endif
