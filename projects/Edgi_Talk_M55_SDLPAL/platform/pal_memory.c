#include "pal_memory.h"

#include <string.h>

#include <finsh.h>
#include <rtthread.h>

#include "pal_display_port.h"

#if defined(__GNUC__)
#define PAL_FRAMEBUFFER __attribute__((section(".pal_framebuffer"), aligned(64)))
#else
#define PAL_FRAMEBUFFER
#endif

PAL_FRAMEBUFFER uint8_t pal_framebuffer_primary[PAL_INDEXED_SLOT_BYTES];
PAL_FRAMEBUFFER uint8_t pal_framebuffer_backup[PAL_INDEXED_SLOT_BYTES];

extern uint8_t __pal_framebuffer_start__[];
extern uint8_t __pal_framebuffer_end__[];
extern uint8_t __bss_end__[];
extern uint8_t __StackLimit[];
extern uint8_t __cy_gpu_buf_start__[];
extern uint8_t __cy_gpu_buf_end__[];

extern struct rt_memheap *drv_hyperam_get_memheap(void);

static void *hot_alloc(void *context, size_t size)
{
    (void)context;
    return rt_malloc(size);
}

static void hot_free(void *context, void *pointer)
{
    (void)context;
    rt_free(pointer);
}

static void *cold_alloc(void *context, size_t size)
{
    struct rt_memheap *heap = (struct rt_memheap *)context;

    return heap != RT_NULL ? rt_memheap_alloc(heap, size) : RT_NULL;
}

static void cold_free(void *context, void *pointer)
{
    (void)context;
    rt_memheap_free(pointer);
}

static rt_size_t hyperram_largest_free(struct rt_memheap *heap)
{
    const rt_size_t header_size =
        RT_ALIGN(sizeof(struct rt_memheap_item), RT_ALIGN_SIZE);
    struct rt_memheap_item *item;
    rt_size_t largest = 0u;

    if (heap == RT_NULL || heap->pool_size == 0u ||
        rt_sem_take(&heap->lock, RT_WAITING_FOREVER) != RT_EOK)
    {
        return 0u;
    }

    for (item = heap->free_list->next_free;
         item != heap->free_list;
         item = item->next_free)
    {
        rt_size_t block_size =
            (rt_size_t)((uint8_t *)item->next - (uint8_t *)item);

        if (block_size > header_size)
        {
            block_size -= header_size;
            if (block_size > largest)
            {
                largest = block_size;
            }
        }
    }
    (void)rt_sem_release(&heap->lock);
    return largest;
}

static rt_size_t sdlpal_stack_high_water(rt_size_t *total)
{
    rt_thread_t thread = rt_thread_find((char *)"sdlpal");
    rt_uint8_t *stack;
    rt_size_t untouched = 0u;

    if (thread == RT_NULL || thread->stack_addr == RT_NULL)
    {
        if (total != RT_NULL)
        {
            *total = 0u;
        }
        return 0u;
    }

    stack = (rt_uint8_t *)thread->stack_addr;
    while (untouched < thread->stack_size && stack[untouched] == '#')
    {
        ++untouched;
    }
    if (total != RT_NULL)
    {
        *total = thread->stack_size;
    }
    return thread->stack_size - untouched;
}

void pal_memory_clear_framebuffers(void)
{
    memset(pal_framebuffer_primary, 0, sizeof(pal_framebuffer_primary));
    memset(pal_framebuffer_backup, 0, sizeof(pal_framebuffer_backup));
}

void pal_memory_init_allocators(void)
{
    struct rt_memheap *hyperram = drv_hyperam_get_memheap();

    pal_memory_policy_configure(hot_alloc, hot_free, RT_NULL,
                                cold_alloc, cold_free, hyperram);
}

void pal_memory_report(const char *stage)
{
    rt_size_t primary_total = 0u;
    rt_size_t primary_used = 0u;
    rt_size_t primary_peak = 0u;
    rt_size_t stack_total = 0u;
    rt_size_t stack_peak;
    struct rt_memheap *hyperram = drv_hyperam_get_memheap();
    pal_memory_stats_t hot_stats;
    pal_memory_stats_t cold_stats;
    pal_display_metrics_t display;

    rt_memory_info(&primary_total, &primary_used, &primary_peak);
    stack_peak = sdlpal_stack_high_water(&stack_total);
    pal_memory_stats_get(PAL_MEMORY_CLASS_HOT, &hot_stats);
    pal_memory_stats_get(PAL_MEMORY_CLASS_COLD, &cold_stats);
    pal_display_metrics_get(&display);

    rt_kprintf("\n[PAL MEM] %s\n", stage != RT_NULL ? stage : "manual");
    rt_kprintf("  DTCM fb=%p..%p bytes=%lu bss_end=%p stack_limit=%p headroom=%lu\n",
               __pal_framebuffer_start__, __pal_framebuffer_end__,
               (unsigned long)(__pal_framebuffer_end__ -
                               __pal_framebuffer_start__),
               __bss_end__, __StackLimit,
               (unsigned long)(__StackLimit - __bss_end__));
    rt_kprintf("  SRAM heap total=%lu used=%lu peak=%lu free=%lu\n",
               (unsigned long)primary_total, (unsigned long)primary_used,
               (unsigned long)primary_peak,
               (unsigned long)(primary_total - primary_used));
    if (hyperram != RT_NULL && hyperram->pool_size != 0u)
    {
        rt_kprintf("  HyperRAM total=%lu used=%lu peak=%lu free=%lu largest=%lu\n",
                   (unsigned long)hyperram->pool_size,
                   (unsigned long)(hyperram->pool_size -
                                   hyperram->available_size),
                   (unsigned long)hyperram->max_used_size,
                   (unsigned long)hyperram->available_size,
                   (unsigned long)hyperram_largest_free(hyperram));
    }
    else
    {
        rt_kprintf("  HyperRAM unavailable\n");
    }
    rt_kprintf("  GFX fixed=%p..%p bytes=%lu\n",
               __cy_gpu_buf_start__, __cy_gpu_buf_end__,
               (unsigned long)(__cy_gpu_buf_end__ - __cy_gpu_buf_start__));
    rt_kprintf("  policy hot current=%lu peak=%lu fail=%lu; cold current=%lu peak=%lu fail=%lu\n",
               (unsigned long)hot_stats.current_bytes,
               (unsigned long)hot_stats.peak_bytes,
               (unsigned long)hot_stats.failed_allocations,
               (unsigned long)cold_stats.current_bytes,
               (unsigned long)cold_stats.peak_bytes,
               (unsigned long)cold_stats.failed_allocations);
    rt_kprintf("  display frames=%lu vg=%lu fallback=%lu last_us=%lu max_us=%lu\n",
               (unsigned long)display.frame_count,
               (unsigned long)display.vglite_frame_count,
               (unsigned long)display.fallback_frame_count,
               (unsigned long)display.last_microseconds,
               (unsigned long)display.max_microseconds);
    rt_kprintf("  control updates=%lu last_us=%lu max_us=%lu\n",
               (unsigned long)display.control_update_count,
               (unsigned long)display.control_last_microseconds,
               (unsigned long)display.control_max_microseconds);
    rt_kprintf("  sdlpal stack used_peak=%lu total=%lu\n",
               (unsigned long)stack_peak, (unsigned long)stack_total);
}

static int pal_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    pal_memory_report("shell");
    return 0;
}
MSH_CMD_EXPORT(pal_mem, Show SDLPal SRAM HyperRAM GFX and stack usage);
