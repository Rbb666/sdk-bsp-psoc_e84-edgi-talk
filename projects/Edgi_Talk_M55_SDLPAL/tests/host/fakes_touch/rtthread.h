#ifndef TEST_FAKE_RTTHREAD_H
#define TEST_FAKE_RTTHREAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int rt_err_t;
typedef int16_t rt_int16_t;
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
typedef size_t rt_size_t;
typedef ptrdiff_t rt_ssize_t;
typedef intptr_t rt_base_t;
typedef uintptr_t rt_ubase_t;
typedef uint32_t rt_tick_t;
typedef int rt_bool_t;

#define RT_NULL NULL
#define RT_EOK 0
#define RT_ERROR 1

#define rt_memcpy memcpy
#define rt_memset memset
#define rt_malloc malloc
#define rt_free free

void rt_thread_mdelay(int milliseconds);

#define MSH_CMD_EXPORT(command, description) typedef int msh_export_##command

#endif
