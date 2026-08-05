#ifndef PAL_TEST_DISPLAY_RTTHREAD_H
#define PAL_TEST_DISPLAY_RTTHREAD_H

#include <stddef.h>
#include <stdint.h>

typedef int rt_bool_t;
typedef int rt_err_t;

#define RT_NULL NULL
#define RT_FALSE 0
#define RT_TRUE 1

uint32_t rt_tick_get_millisecond(void);
int rt_kprintf(const char *format, ...);

#endif
