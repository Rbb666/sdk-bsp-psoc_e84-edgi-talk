#ifndef RTTHREAD_H
#define RTTHREAD_H

#include <stddef.h>
#include <stdint.h>

typedef int rt_err_t;
typedef long rt_ssize_t;
typedef unsigned long rt_base_t;
typedef unsigned long rt_size_t;
typedef unsigned char rt_uint8_t;
typedef unsigned int rt_uint32_t;

struct rt_thread
{
    uintptr_t opaque[32];
};

struct rt_messagequeue
{
    uintptr_t opaque[32];
};

struct rt_mq_message
{
    struct rt_mq_message *next;
    rt_ssize_t length;
};

typedef struct rt_thread *rt_thread_t;
typedef struct rt_messagequeue *rt_mq_t;

#define RT_EOK 0
#define RT_ERROR 1
#define RT_IPC_FLAG_FIFO 0u
#define RT_IPC_CMD_RESET 1
#define RT_WAITING_FOREVER (-1)
#define RT_ALIGN_SIZE sizeof(void *)
#define RT_ALIGN(value, alignment) \
    (((value) + (alignment) - 1u) & ~((alignment) - 1u))
#define RT_MQ_BUF_SIZE(msg_size, max_msgs) \
    ((RT_ALIGN((msg_size), RT_ALIGN_SIZE) + sizeof(struct rt_mq_message)) * \
     (max_msgs))

rt_err_t rt_thread_init(struct rt_thread *thread, const char *name,
                        void (*entry)(void *), void *parameter,
                        void *stack_start, rt_uint32_t stack_size,
                        rt_uint8_t priority, rt_uint32_t tick);
rt_err_t rt_thread_detach(rt_thread_t thread);
rt_err_t rt_thread_startup(rt_thread_t thread);
rt_err_t rt_mq_init(rt_mq_t mq, const char *name, void *msgpool,
                    rt_size_t msg_size, rt_size_t pool_size,
                    rt_uint8_t flag);
rt_err_t rt_mq_detach(rt_mq_t mq);
rt_err_t rt_mq_send(rt_mq_t mq, const void *buffer, rt_size_t size);
rt_ssize_t rt_mq_recv(rt_mq_t mq, void *buffer, rt_size_t size,
                      int timeout);
rt_err_t rt_mq_control(rt_mq_t mq, int command, void *argument);
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t level);
int rt_kprintf(const char *format, ...);

#endif
