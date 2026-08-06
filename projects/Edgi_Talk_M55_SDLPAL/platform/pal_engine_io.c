#include "pal_engine_io.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rtthread.h>

#include "pal_engine_heap.h"
#include "pal_memory.h"
#include "pal_save_io_core.h"

#define PAL_SAVE_RESERVE_BYTES (192u * 1024u)
#define PAL_SAVE_ALLOC_MIN_BYTES (180u * 1024u)
#define PAL_SAVE_PROGRESS_BYTES (32u * 1024u)

#if defined(__GNUC__) || defined(__clang__)
#define PAL_SAVE_GFX __attribute__((section(".cy_gpu_buf.sdlpal_save"), aligned(64)))
#else
#error "SDLPal save reserve requires a compiler-specific GFX SRAM section"
#endif

PAL_SAVE_GFX static uint8_t save_reserve[PAL_SAVE_RESERVE_BYTES];
static rt_bool_t save_reserve_in_use;
static FILE *save_stream;

typedef struct save_sink_context
{
    FILE *stream;
    size_t completed_bytes;
} save_sink_context_t;

static rt_bool_t save_sized_allocation(size_t size)
{
    return size >= PAL_SAVE_ALLOC_MIN_BYTES && size <= PAL_SAVE_RESERVE_BYTES;
}

static rt_bool_t save_write_path(const char *path, const char *mode)
{
    static const char suffix[] = ".rpg";
    size_t path_length;

    if (path == RT_NULL || mode == RT_NULL || mode[0] != 'w')
    {
        return RT_FALSE;
    }
    path_length = strlen(path);
    return path_length >= sizeof(suffix) - 1u &&
           strcmp(path + path_length - (sizeof(suffix) - 1u), suffix) == 0;
}

static void *save_reserve_acquire(void)
{
    void *result = RT_NULL;

    rt_enter_critical();
    if (!save_reserve_in_use)
    {
        save_reserve_in_use = RT_TRUE;
        result = save_reserve;
    }
    rt_exit_critical();
    return result;
}

static void save_reserve_release(void)
{
    rt_enter_critical();
    save_reserve_in_use = RT_FALSE;
    rt_exit_critical();
}

void *pal_engine_malloc(size_t size)
{
    void *pointer;

    if (!save_sized_allocation(size))
    {
        return pal_engine_heap_malloc(size);
    }

    pointer = malloc(size);
    if (pointer != RT_NULL)
    {
        rt_kprintf("[PAL SAVE] alloc bytes=%lu source=SRAM ptr=%p\n",
                   (unsigned long)size, pointer);
        return pointer;
    }

    pointer = save_reserve_acquire();
    if (pointer != RT_NULL)
    {
        rt_kprintf("[PAL SAVE] alloc bytes=%lu source=GFX-reserve ptr=%p\n",
                   (unsigned long)size, pointer);
        return pointer;
    }

    pointer = pal_engine_heap_fallback_malloc(size);
    if (pointer != RT_NULL)
    {
        rt_kprintf("[PAL SAVE] alloc bytes=%lu source=HyperRAM ptr=%p\n",
                   (unsigned long)size, pointer);
        return pointer;
    }

    rt_kprintf("[PAL SAVE] alloc failed bytes=%lu\n", (unsigned long)size);
    pal_memory_report("save-alloc-failed");
    return RT_NULL;
}

void *pal_engine_calloc(size_t count, size_t size)
{
    return pal_engine_heap_calloc(count, size);
}

void *pal_engine_realloc(void *pointer, size_t size)
{
    void *replacement;

    if (pointer != save_reserve)
    {
        return pal_engine_heap_realloc(pointer, size);
    }
    if (size == 0u)
    {
        save_reserve_release();
        return RT_NULL;
    }
    if (size <= PAL_SAVE_RESERVE_BYTES)
    {
        return pointer;
    }

    replacement = pal_engine_heap_malloc(size);
    if (replacement == RT_NULL)
    {
        return RT_NULL;
    }
    memcpy(replacement, save_reserve, PAL_SAVE_RESERVE_BYTES);
    save_reserve_release();
    return replacement;
}

void pal_engine_free(void *pointer)
{
    if (pointer == save_reserve)
    {
        save_reserve_release();
        rt_kprintf("[PAL SAVE] released GFX reserve\n");
        return;
    }
    pal_engine_heap_free(pointer);
}

FILE *pal_engine_fopen(const char *path, const char *mode)
{
    rt_bool_t is_save = save_write_path(path, mode);
    FILE *stream;

    if (is_save)
    {
        rt_kprintf("[PAL SAVE] open path=%s mode=%s\n", path, mode);
    }

    stream = fopen(path, mode);
    if (!is_save)
    {
        return stream;
    }
    if (stream == RT_NULL)
    {
        rt_kprintf("[PAL SAVE] open failed errno=%d\n", errno);
        return RT_NULL;
    }
    if (setvbuf(stream, RT_NULL, _IONBF, 0u) != 0)
    {
        rt_kprintf("[PAL SAVE] unbuffered mode failed errno=%d\n", errno);
        (void)fclose(stream);
        return RT_NULL;
    }

    save_stream = stream;
    rt_kprintf("[PAL SAVE] open ok stream=%p chunk=%u\n",
               stream, (unsigned int)PAL_SAVE_IO_CHUNK_BYTES);
    return stream;
}

static size_t save_sink_write(const void *data, size_t bytes, void *opaque)
{
    save_sink_context_t *context = (save_sink_context_t *)opaque;
    size_t accepted;

    if ((context->completed_bytes % PAL_SAVE_PROGRESS_BYTES) == 0u)
    {
        rt_kprintf("[PAL SAVE] write offset=%lu\n",
                   (unsigned long)context->completed_bytes);
    }
    accepted = fwrite(data, 1u, bytes, context->stream);
    context->completed_bytes += accepted;
    if (accepted != bytes)
    {
        rt_kprintf("[PAL SAVE] short write request=%lu accepted=%lu errno=%d ferror=%d\n",
                   (unsigned long)bytes, (unsigned long)accepted,
                   errno, ferror(context->stream));
    }
    return accepted;
}

static void save_writer_yield(void *context)
{
    (void)context;
    rt_thread_yield();
}

size_t pal_engine_fwrite(const void *data, size_t element_size,
                         size_t element_count, FILE *stream)
{
    save_sink_context_t context;
    size_t result;
    size_t total_bytes;

    if (stream != save_stream)
    {
        return fwrite(data, element_size, element_count, stream);
    }
    if (element_size == 0u || element_count == 0u ||
        element_count > SIZE_MAX / element_size)
    {
        return 0u;
    }

    total_bytes = element_size * element_count;
    context.stream = stream;
    context.completed_bytes = 0u;
    rt_kprintf("[PAL SAVE] write begin bytes=%lu\n", (unsigned long)total_bytes);
    result = pal_save_write_chunked(data, element_size, element_count,
                                    save_sink_write, &context,
                                    save_writer_yield, RT_NULL);
    rt_kprintf("[PAL SAVE] write end bytes=%lu/%lu elements=%lu/%lu\n",
               (unsigned long)context.completed_bytes,
               (unsigned long)total_bytes,
               (unsigned long)result,
               (unsigned long)element_count);
    return result;
}

int pal_engine_fclose(FILE *stream)
{
    rt_bool_t is_save = stream == save_stream;
    int result;
    int close_errno;

    if (is_save)
    {
        rt_kprintf("[PAL SAVE] close begin stream=%p\n", stream);
    }
    errno = 0;
    result = fclose(stream);
    close_errno = errno;
    if (is_save)
    {
        save_stream = RT_NULL;
        rt_kprintf("[PAL SAVE] close end result=%d errno=%d\n",
                   result, close_errno);
    }
    return result;
}
