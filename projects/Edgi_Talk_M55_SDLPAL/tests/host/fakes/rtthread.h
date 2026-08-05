#ifndef PAL_TEST_RTTHREAD_H
#define PAL_TEST_RTTHREAD_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t rt_int32_t;

#define RT_NULL NULL

uint32_t rt_tick_get_millisecond(void);
void rt_thread_mdelay(rt_int32_t milliseconds);
int rt_kprintf(const char *format, ...);

#endif
