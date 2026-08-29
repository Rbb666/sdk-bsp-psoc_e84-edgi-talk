#include "retro_go_display.h"

#include "retro_go_lcd_api.h"
#include "retro_go_perf.h"
#include "retro_go_platform.h"
#include "retro_go_time.h"

#include <board.h>
#include <rthw.h>
#include <rtdevice.h>
#include <rtthread.h>

#include <string.h>

#include "cy_graphics.h"
#include "vg_lite.h"

#if !defined(BSP_LCD_ROTATION_90) && !defined(BSP_LCD_ROTATION_270)
#error "Retro-Go requires an 800x480 landscape LCD rotation"
#endif

typedef struct retro_go_viewport
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} retro_go_viewport_t;

#define RETRO_GO_DISPLAY_MAX_WIDTH 800u
#define RETRO_GO_DISPLAY_MAX_HEIGHT 480u
#define RETRO_GO_DISPLAY_MAX_VIEWPORT_WIDTH 534u
#define RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS 720u
#define RETRO_GO_LAUNCHER_SCALE 2u
#define RETRO_GO_LAUNCHER_OUTPUT_WIDTH \
    (RETRO_GO_LAUNCHER_WIDTH * RETRO_GO_LAUNCHER_SCALE)
#define RETRO_GO_LAUNCHER_OUTPUT_HEIGHT \
    (RETRO_GO_LAUNCHER_HEIGHT * RETRO_GO_LAUNCHER_SCALE)
#define RETRO_GO_GBA_SCALE 3u
#define RETRO_GO_GBA_OUTPUT_WIDTH (RETRO_GO_GBA_WIDTH * RETRO_GO_GBA_SCALE)
#define RETRO_GO_GBA_OUTPUT_HEIGHT (RETRO_GO_GBA_HEIGHT * RETRO_GO_GBA_SCALE)
#define RETRO_GO_GFXRAM_START 0x26200000u
#define RETRO_GO_GFXRAM_END   0x26500000u
#define RETRO_GO_SCANOUT_WIDTH 480u
#define RETRO_GO_SCANOUT_HEIGHT 800u
#define RETRO_GO_SCANOUT_STRIDE_PIXELS 512u

#define RETRO_GO_DISPLAY_THREAD_STACK_BYTES 4096u
#define RETRO_GO_GBA_MAILBOX_BUFFERS 2u

typedef enum retro_go_display_buffer_state
{
    RETRO_GO_DISPLAY_BUFFER_FREE,
    RETRO_GO_DISPLAY_BUFFER_WRITING,
    RETRO_GO_DISPLAY_BUFFER_READY,
    RETRO_GO_DISPLAY_BUFFER_DISPLAYING,
} retro_go_display_buffer_state_t;

#ifndef BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY
#define BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY 26
#endif
#ifndef BSP_RETRO_GO_DISPLAY_THREAD_SLICE
#define BSP_RETRO_GO_DISPLAY_THREAD_SLICE 5
#endif
#ifndef BSP_RETRO_GO_GBA_FLIP_WAIT_MS
#define BSP_RETRO_GO_GBA_FLIP_WAIT_MS 3
#endif

#if defined(CONFIG_USBHOST_PSC_PRIO) && \
    (BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY <= CONFIG_USBHOST_PSC_PRIO)
#error "Retro-Go display worker must remain below USB host priority"
#endif
#if defined(BSP_RETRO_GO_INPUT_THREAD_PRIORITY) && \
    (BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY <= \
     BSP_RETRO_GO_INPUT_THREAD_PRIORITY)
#error "Retro-Go display worker must remain below HID input priority"
#endif
#if defined(RT_MAIN_THREAD_PRIORITY) && \
    (BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY < RT_MAIN_THREAD_PRIORITY)
#error "Retro-Go display worker must not preempt the emulator main thread"
#endif

static uint16_t source_framebuffer[RETRO_GO_GB_WIDTH * RETRO_GO_GB_HEIGHT]
    RETRO_GO_GFXMEM;
static uint16_t launcher_framebuffer[RETRO_GO_LAUNCHER_WIDTH *
                                     RETRO_GO_LAUNCHER_HEIGHT]
    RETRO_GO_GFXMEM;
static uint16_t gba_framebuffer[RETRO_GO_GBA_WIDTH *
                                (RETRO_GO_GBA_HEIGHT + 1u)]
    __attribute__((section(".cy_gba_framebuffer"), aligned(64)));
#ifdef BSP_RETRO_GO_ASYNC_GBA_DISPLAY
static uint16_t gba_present_framebuffer[RETRO_GO_GBA_MAILBOX_BUFFERS]
                                       [RETRO_GO_GBA_WIDTH *
                                        RETRO_GO_GBA_HEIGHT]
    RETRO_GO_GFXMEM;
static struct rt_thread display_thread RETRO_GO_SOCMEM;
static struct rt_semaphore display_sem RETRO_GO_SOCMEM;
static rt_uint8_t display_thread_stack[RETRO_GO_DISPLAY_THREAD_STACK_BYTES]
    RETRO_GO_SOCMEM;
static volatile bool display_async_ready;
static volatile bool display_async_busy;
static volatile bool display_worker_running;
static volatile uint8_t display_buffer_state[RETRO_GO_GBA_MAILBOX_BUFFERS];
#endif
static volatile uint32_t display_submitted_frames;
static volatile uint32_t display_completed_frames;
static volatile uint32_t display_dropped_frames;
static volatile uint32_t display_failed_frames;
static volatile uint64_t display_total_present_cycles;
static volatile uint32_t display_max_present_cycles;
static bool display_present_completed;
typedef union retro_go_display_work_buffer
{
    uint16_t scaled[RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS *
                    RETRO_GO_DISPLAY_MAX_HEIGHT];
    uint16_t scanout[RETRO_GO_SCANOUT_STRIDE_PIXELS *
                     RETRO_GO_SCANOUT_HEIGHT];
} retro_go_display_work_buffer_t;

static retro_go_display_work_buffer_t display_work_buffer RETRO_GO_GFXMEM
    __attribute__((aligned(128)));
#define scaled_framebuffer (display_work_buffer.scaled)
static uint16_t source_x_map[RETRO_GO_DISPLAY_MAX_VIEWPORT_WIDTH]
    RETRO_GO_DTCM;
static uint16_t source_y_map[RETRO_GO_DISPLAY_MAX_HEIGHT] RETRO_GO_DTCM;
static rt_device_t lcd_device;
static struct rt_device_graphic_info lcd_info;
static retro_go_viewport_t viewport;
static bool display_ready;
#ifdef BSP_RETRO_GO_USE_VGLITE
static bool vglite_failed;
#ifdef BSP_RETRO_GO_GBA_DIRECT_SCANOUT
static bool direct_scanout_failed;
static bool direct_scanout_logged;
static bool direct_scanout_initialized;
static bool direct_scanout_flip_pending;
static bool direct_scanout_buffer_cleared[2];
static uint8_t direct_scanout_front;
static uint8_t direct_scanout_back;
static uint8_t direct_scanout_pending_front;
static uint16_t *direct_scanout_buffers[2];
static cy_stc_gfx_context_t direct_scanout_context;
#ifdef BSP_RETRO_GO_PERF_OVERLAY
static uint32_t direct_panel_revision;
static uint32_t direct_panel_applied_revision[2];
#endif
#endif
#ifdef BSP_RETRO_GO_GBA_DIRECT_RENDER
static bool direct_render_failed;
static bool direct_render_logged;
#endif
#endif

static bool present_gba_sync(const uint16_t *framebuffer);

static RETRO_GO_ITCM bool scale_gba_cpu_direct(const uint16_t *framebuffer)
{
#ifdef BSP_RETRO_GO_GBA_DIRECT_CPU_SCALE
    uint16_t *destination = (uint16_t *)lcd_info.framebuffer;
    uint32_t destination_stride = lcd_info.pitch / sizeof(uint16_t);
    uint16_t y;

    if (framebuffer == NULL || destination == NULL ||
        lcd_info.width < 800u || lcd_info.height < RETRO_GO_GBA_OUTPUT_HEIGHT ||
        lcd_info.pitch < lcd_info.width * sizeof(uint16_t))
    {
        return false;
    }
    for (y = 0u; y < RETRO_GO_GBA_HEIGHT; ++y)
    {
        const uint16_t *source_row =
            framebuffer + (size_t)y * RETRO_GO_GBA_WIDTH;
        uint16_t *first_row =
            destination + (size_t)(y * RETRO_GO_GBA_SCALE) *
                              destination_stride;
        uint16_t x;

        for (x = 0u; x < RETRO_GO_GBA_WIDTH; ++x)
        {
            uint16_t pixel = source_row[x];
            size_t output = (size_t)x * RETRO_GO_GBA_SCALE;

            first_row[output] = pixel;
            first_row[output + 1u] = pixel;
            first_row[output + 2u] = pixel;
        }
        memcpy(first_row + destination_stride, first_row,
               RETRO_GO_GBA_OUTPUT_WIDTH * sizeof(first_row[0]));
        memcpy(first_row + destination_stride * 2u, first_row,
               RETRO_GO_GBA_OUTPUT_WIDTH * sizeof(first_row[0]));
    }
    return true;
#else
    (void)framebuffer;
    return false;
#endif
}

static void stage_performance_panel(void)
{
#ifdef BSP_RETRO_GO_PERF_OVERLAY
    if (lcd_info.width >= RETRO_GO_PERF_PANEL_WIDTH &&
        lcd_info.height >= RETRO_GO_PERF_PANEL_HEIGHT &&
        retro_go_perf_render_due(launcher_framebuffer,
                                 RETRO_GO_PERF_PANEL_WIDTH,
                                 RETRO_GO_PERF_PANEL_HEIGHT,
                                 RETRO_GO_PERF_PANEL_WIDTH))
    {
        lcd_flush_rgb565_area(
            launcher_framebuffer,
            lcd_info.width - RETRO_GO_PERF_PANEL_WIDTH, 0u,
            RETRO_GO_PERF_PANEL_WIDTH, RETRO_GO_PERF_PANEL_HEIGHT,
            RETRO_GO_PERF_PANEL_WIDTH, RT_FALSE);
    }
#endif
}

#ifdef BSP_RETRO_GO_USE_VGLITE
static bool gba_framebuffer_cacheable_range(uintptr_t start, uintptr_t end)
{
#ifdef BSP_RETRO_GO_GBA_FRAMEBUFFER_CACHEABLE
    return start >= (uintptr_t)gba_framebuffer &&
           end <= (uintptr_t)gba_framebuffer + sizeof(gba_framebuffer);
#else
    (void)start;
    (void)end;
    return false;
#endif
}

static void cache_clean(const void *address, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    uintptr_t raw_start = (uintptr_t)address;
    uintptr_t raw_end = raw_start + size;
    uintptr_t start = (uintptr_t)address &
                      ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1u);
    uintptr_t end = ((uintptr_t)address + size + __SCB_DCACHE_LINE_SIZE - 1u) &
                    ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1u);

    /* Generated MPU region 3 maps all GFXRAM as Normal Non-cacheable.
     * Avoid thousands of redundant 32-byte SCB maintenance operations. */
    if (raw_start >= RETRO_GO_GFXRAM_START &&
        raw_end <= RETRO_GO_GFXRAM_END &&
        !gba_framebuffer_cacheable_range(raw_start, raw_end))
    {
        __DMB();
        return;
    }
    if (rt_hw_cpu_dcache_status() && end > start)
    {
        SCB_CleanDCache_by_Addr((void *)start, (int32_t)(end - start));
    }
#else
    (void)address;
    (void)size;
#endif
}

static void cache_invalidate(void *address, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    uintptr_t raw_start = (uintptr_t)address;
    uintptr_t raw_end = raw_start + size;
    uintptr_t start = (uintptr_t)address &
                      ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1u);
    uintptr_t end = ((uintptr_t)address + size + __SCB_DCACHE_LINE_SIZE - 1u) &
                    ~((uintptr_t)__SCB_DCACHE_LINE_SIZE - 1u);

    if (raw_start >= RETRO_GO_GFXRAM_START &&
        raw_end <= RETRO_GO_GFXRAM_END &&
        !gba_framebuffer_cacheable_range(raw_start, raw_end))
    {
        __DSB();
        return;
    }
    if (rt_hw_cpu_dcache_status() && end > start)
    {
        SCB_InvalidateDCache_by_Addr((void *)start, (int32_t)(end - start));
    }
#else
    (void)address;
    (void)size;
#endif
}
#endif

static retro_go_viewport_t calculate_viewport(uint16_t screen_width,
                                               uint16_t screen_height)
{
    retro_go_viewport_t result;

#ifdef BSP_RETRO_GO_SCALE_INTEGER
    uint16_t scale_x = screen_width / RETRO_GO_GB_WIDTH;
    uint16_t scale_y = screen_height / RETRO_GO_GB_HEIGHT;
    uint16_t scale = scale_x < scale_y ? scale_x : scale_y;

    if (scale == 0u)
    {
        scale = 1u;
    }
    result.width = (uint16_t)(RETRO_GO_GB_WIDTH * scale);
    result.height = (uint16_t)(RETRO_GO_GB_HEIGHT * scale);
#else
    uint32_t width_from_height =
        ((uint32_t)screen_height * RETRO_GO_GB_WIDTH) / RETRO_GO_GB_HEIGHT;

    if (width_from_height <= screen_width)
    {
        result.width = (uint16_t)width_from_height;
        result.height = screen_height;
    }
    else
    {
        result.width = screen_width;
        result.height = (uint16_t)(((uint32_t)screen_width *
                                    RETRO_GO_GB_HEIGHT) /
                                   RETRO_GO_GB_WIDTH);
    }
#endif
    result.x = (uint16_t)((screen_width - result.width) / 2u);
    result.y = (uint16_t)((screen_height - result.height) / 2u);
    return result;
}

static void build_scale_maps(void)
{
    uint16_t coordinate;

    for (coordinate = 0u; coordinate < viewport.width; ++coordinate)
    {
        source_x_map[coordinate] =
            (uint16_t)(((uint32_t)coordinate * RETRO_GO_GB_WIDTH) /
                       viewport.width);
    }
    for (coordinate = 0u; coordinate < viewport.height; ++coordinate)
    {
        source_y_map[coordinate] =
            (uint16_t)(((uint32_t)coordinate * RETRO_GO_GB_HEIGHT) /
                       viewport.height);
    }
}

static bool scale_vglite(const uint16_t *pixels,
                         uint16_t source_width, uint16_t source_height,
                         uint16_t target_width, uint16_t target_height)
{
#ifdef BSP_RETRO_GO_USE_VGLITE
    vg_lite_buffer_t source;
    vg_lite_buffer_t target;
    vg_lite_matrix_t matrix;
    vg_lite_error_t status;

    if (vglite_failed || pixels == NULL || source_width == 0u ||
        source_height == 0u || target_width == 0u || target_height == 0u ||
        target_width > RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS ||
        target_height > RETRO_GO_DISPLAY_MAX_HEIGHT)
    {
        return false;
    }

    memset(&source, 0, sizeof(source));
    source.width = source_width;
    source.height = source_height;
    source.stride = source_width * sizeof(uint16_t);
    source.format = VG_LITE_RGB565;
    source.tiled = VG_LITE_LINEAR;
    source.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    source.transparency_mode = VG_LITE_IMAGE_OPAQUE;
    source.memory = (void *)pixels;
    source.address = (uint32_t)(uintptr_t)pixels;

    memset(&target, 0, sizeof(target));
    target.width = target_width;
    target.height = target_height;
    target.stride = RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS * sizeof(uint16_t);
    target.format = VG_LITE_RGB565;
    target.tiled = VG_LITE_LINEAR;
    target.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    target.transparency_mode = VG_LITE_IMAGE_OPAQUE;
    target.memory = scaled_framebuffer;
    target.address = (uint32_t)(uintptr_t)scaled_framebuffer;

    cache_clean(pixels, (size_t)source_width * source_height *
                         sizeof(uint16_t));
    cache_clean(scaled_framebuffer,
                RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS * target_height *
                    sizeof(uint16_t));
    status = vg_lite_identity(&matrix);
    if (status == VG_LITE_SUCCESS)
    {
        status = vg_lite_scale((vg_lite_float_t)target_width /
                                   (vg_lite_float_t)source_width,
                               (vg_lite_float_t)target_height /
                                   (vg_lite_float_t)source_height,
                               &matrix);
    }
    if (status == VG_LITE_SUCCESS)
    {
        status = vg_lite_blit(&target, &source, &matrix,
                              VG_LITE_BLEND_NONE, 0u,
                              VG_LITE_FILTER_POINT);
    }
    if (status == VG_LITE_SUCCESS)
    {
        status = vg_lite_finish();
    }
    if (status != VG_LITE_SUCCESS)
    {
        if (!vglite_failed)
        {
            rt_kprintf("[retro-go] VG-Lite scale failed: %d; CPU fallback\n",
                       status);
            vglite_failed = true;
        }
        return false;
    }

    cache_invalidate(scaled_framebuffer,
                     RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS * target_height *
                         sizeof(uint16_t));
    return true;
#else
    (void)pixels;
    (void)source_width;
    (void)source_height;
    (void)target_width;
    (void)target_height;
    return false;
#endif
}

static bool scale_gba_vglite_direct(const uint16_t *pixels)
{
#if defined(BSP_RETRO_GO_USE_VGLITE) && \
    defined(BSP_RETRO_GO_GBA_DIRECT_RENDER)
    vg_lite_buffer_t source;
    vg_lite_buffer_t target;
    vg_lite_matrix_t matrix;
    vg_lite_error_t status;

    if (direct_render_failed || vglite_failed || pixels == NULL ||
        lcd_info.framebuffer == RT_NULL || lcd_info.width < 800u ||
        lcd_info.height < RETRO_GO_GBA_OUTPUT_HEIGHT ||
        lcd_info.pitch < lcd_info.width * sizeof(uint16_t))
    {
        return false;
    }
    memset(&source, 0, sizeof(source));
    source.width = RETRO_GO_GBA_WIDTH;
    source.height = RETRO_GO_GBA_HEIGHT;
    source.stride = RETRO_GO_GBA_WIDTH * sizeof(uint16_t);
    source.format = VG_LITE_RGB565;
    source.tiled = VG_LITE_LINEAR;
    source.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    source.transparency_mode = VG_LITE_IMAGE_OPAQUE;
    source.memory = (void *)pixels;
    source.address = (uint32_t)(uintptr_t)pixels;

    memset(&target, 0, sizeof(target));
    target.width = lcd_info.width;
    target.height = lcd_info.height;
    target.stride = lcd_info.pitch;
    target.format = VG_LITE_RGB565;
    target.tiled = VG_LITE_LINEAR;
    target.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    target.transparency_mode = VG_LITE_IMAGE_OPAQUE;
    target.memory = lcd_info.framebuffer;
    target.address = (uint32_t)(uintptr_t)lcd_info.framebuffer;

    cache_clean(pixels, RETRO_GO_GBA_WIDTH * RETRO_GO_GBA_HEIGHT *
                         sizeof(uint16_t));
    cache_clean(lcd_info.framebuffer,
                (size_t)lcd_info.pitch * lcd_info.height);
    status = vg_lite_identity(&matrix);
    if (status == VG_LITE_SUCCESS)
    {
        status = vg_lite_scale((vg_lite_float_t)RETRO_GO_GBA_SCALE,
                               (vg_lite_float_t)RETRO_GO_GBA_SCALE,
                               &matrix);
    }
    if (status == VG_LITE_SUCCESS)
    {
        status = vg_lite_blit(&target, &source, &matrix,
                              VG_LITE_BLEND_NONE, 0u,
                              VG_LITE_FILTER_POINT);
    }
    if (status == VG_LITE_SUCCESS)
    {
        status = vg_lite_finish();
    }
    if (status != VG_LITE_SUCCESS)
    {
        direct_render_failed = true;
        rt_kprintf("[retro-go] direct GBA render failed: %d; staging fallback\n",
                   status);
        return false;
    }
    cache_invalidate(lcd_info.framebuffer,
                     (size_t)lcd_info.pitch * lcd_info.height);
    if (!direct_render_logged)
    {
        direct_render_logged = true;
        rt_kprintf("[retro-go] GBA direct VG-Lite render active: target=%p "
                   "stride=%u\n", lcd_info.framebuffer, lcd_info.pitch);
    }
    return true;
#else
    (void)pixels;
    return false;
#endif
}

static vg_lite_error_t build_direct_scanout_matrix(
    vg_lite_matrix_t *matrix,
    uint32_t source_width, uint32_t source_height,
    uint32_t logical_x, uint32_t logical_y,
    uint32_t logical_width, uint32_t logical_height)
{
    vg_lite_float_point4_t source;
    vg_lite_float_point4_t destination;

    source[0].x = 0.0f;
    source[0].y = 0.0f;
    source[1].x = (vg_lite_float_t)source_width;
    source[1].y = 0.0f;
    source[2].x = (vg_lite_float_t)source_width;
    source[2].y = (vg_lite_float_t)source_height;
    source[3].x = 0.0f;
    source[3].y = (vg_lite_float_t)source_height;

#if defined(BSP_LCD_ROTATION_90)
    destination[0].x = (vg_lite_float_t)(RETRO_GO_SCANOUT_WIDTH - logical_y);
    destination[0].y = (vg_lite_float_t)logical_x;
    destination[1].x = destination[0].x;
    destination[1].y = (vg_lite_float_t)(logical_x + logical_width);
    destination[2].x = (vg_lite_float_t)(RETRO_GO_SCANOUT_WIDTH -
                                          logical_y - logical_height);
    destination[2].y = destination[1].y;
    destination[3].x = destination[2].x;
    destination[3].y = destination[0].y;
#else
    destination[0].x = (vg_lite_float_t)logical_y;
    destination[0].y = (vg_lite_float_t)(RETRO_GO_SCANOUT_HEIGHT - logical_x);
    destination[1].x = destination[0].x;
    destination[1].y = (vg_lite_float_t)(RETRO_GO_SCANOUT_HEIGHT -
                                          logical_x - logical_width);
    destination[2].x = (vg_lite_float_t)(logical_y + logical_height);
    destination[2].y = destination[1].y;
    destination[3].x = destination[2].x;
    destination[3].y = destination[0].y;
#endif
    vg_lite_identity(matrix);
    return vg_lite_get_transform_matrix(source, destination, matrix);
}

#ifdef BSP_RETRO_GO_GBA_DIRECT_SCANOUT
static void direct_scanout_reset(void)
{
    direct_scanout_initialized = false;
    direct_scanout_flip_pending = false;
    direct_scanout_front = 0u;
    direct_scanout_back = 1u;
    direct_scanout_pending_front = 0u;
    direct_scanout_buffers[0] = NULL;
    direct_scanout_buffers[1] = NULL;
    direct_scanout_buffer_cleared[0] = false;
    direct_scanout_buffer_cleared[1] = false;
    memset(&direct_scanout_context, 0, sizeof(direct_scanout_context));
#ifdef BSP_RETRO_GO_PERF_OVERLAY
    direct_panel_revision = 1u;
    direct_panel_applied_revision[0] = 0u;
    direct_panel_applied_revision[1] = 0u;
#endif
}

static bool direct_scanout_poll_flip(uint32_t timeout_ms)
{
    if (!direct_scanout_flip_pending)
    {
        return true;
    }
    if (lcd_wait_frame_done(timeout_ms) != RT_EOK)
    {
        return false;
    }
    direct_scanout_front = direct_scanout_pending_front;
    direct_scanout_back = (uint8_t)(1u - direct_scanout_front);
    direct_scanout_flip_pending = false;
    return true;
}

static bool direct_scanout_restore_vendor_buffer(void)
{
    rt_base_t level;
    cy_en_gfx_status_t status;

    if (!direct_scanout_initialized || direct_scanout_buffers[0] == NULL)
    {
        return true;
    }
    level = rt_hw_interrupt_disable();
    while (lcd_wait_frame_done(0u) == RT_EOK)
    {
    }
    status = Cy_GFXSS_Set_FrameBuffer(
        (GFXSS_Type *)GFXSS,
        (uint32_t *)direct_scanout_buffers[0],
        &direct_scanout_context);
    Cy_GFXSS_Clear_DC_Interrupt(
        (GFXSS_Type *)GFXSS, &direct_scanout_context);
    NVIC_ClearPendingIRQ(GFXSS_DC_IRQ);
    rt_hw_interrupt_enable(level);
    if (status != CY_GFX_SUCCESS || lcd_wait_frame_done(50u) != RT_EOK)
    {
        rt_kprintf("[retro-go] vendor scanout restore failed: status=%d\n",
                   status);
        return false;
    }
    direct_scanout_front = 0u;
    direct_scanout_back = 1u;
    direct_scanout_flip_pending = false;
    return true;
}

static bool direct_scanout_enter_vendor_path(void)
{
    if (direct_scanout_initialized &&
        !direct_scanout_restore_vendor_buffer())
    {
        return false;
    }
    direct_scanout_reset();
    return true;
}
#endif

static bool scale_gba_vglite_scanout(const uint16_t *pixels)
{
#if defined(BSP_RETRO_GO_USE_VGLITE) && \
    defined(BSP_RETRO_GO_GBA_DIRECT_SCANOUT)
    vg_lite_buffer_t source;
    vg_lite_buffer_t target;
    vg_lite_matrix_t matrix;
    vg_lite_error_t status;
    uint32_t scanout_stride;
    uint16_t *scanout;
    uintptr_t scanout_start;
    uintptr_t scanout_end;
    uint32_t game_logical_x;
#ifdef BSP_RETRO_GO_PERF_OVERLAY
    bool panel_blitted = false;
#endif

    if (direct_scanout_failed || vglite_failed || pixels == NULL ||
        lcd_info.width != RETRO_GO_SCANOUT_HEIGHT ||
        lcd_info.height != RETRO_GO_SCANOUT_WIDTH)
    {
        return false;
    }
    if (!direct_scanout_initialized)
    {
        scanout = (uint16_t *)Cy_GFXSS_Get_FrameBufferAddress(
            (GFXSS_Type *)GFXSS);
        scanout_stride =
            GFXSS->GFXSS_DC.DCNANO.GCREGFRAMEBUFFERSTRIDE;
        scanout_start = (uintptr_t)scanout;
        scanout_end = scanout_start +
            (uintptr_t)scanout_stride * RETRO_GO_SCANOUT_HEIGHT;
        if (scanout == NULL ||
            scanout_stride !=
                RETRO_GO_SCANOUT_STRIDE_PIXELS * sizeof(uint16_t) ||
            scanout_start < RETRO_GO_GFXRAM_START ||
            scanout_end > RETRO_GO_GFXRAM_END ||
            scanout_end <= scanout_start)
        {
            direct_scanout_failed = true;
            rt_kprintf("[retro-go] direct scanout contract mismatch: fb=%p "
                       "stride=%u\n", scanout, (unsigned)scanout_stride);
            return false;
        }
        direct_scanout_buffers[0] = scanout;
        direct_scanout_buffers[1] = display_work_buffer.scanout;
        direct_scanout_front = 0u;
        direct_scanout_back = 1u;
        direct_scanout_flip_pending = false;
        direct_scanout_initialized = true;
        direct_scanout_buffer_cleared[0] = true;
        direct_scanout_buffer_cleared[1] = false;
        memset(&direct_scanout_context, 0,
               sizeof(direct_scanout_context));
    }
    if (!direct_scanout_poll_flip(BSP_RETRO_GO_GBA_FLIP_WAIT_MS))
    {
        ++display_dropped_frames;
        display_present_completed = false;
        return true;
    }
    scanout = direct_scanout_buffers[direct_scanout_back];
    scanout_stride = RETRO_GO_SCANOUT_STRIDE_PIXELS * sizeof(uint16_t);
    scanout_start = (uintptr_t)scanout;

    memset(&source, 0, sizeof(source));
    source.width = RETRO_GO_GBA_WIDTH;
    source.height = RETRO_GO_GBA_HEIGHT;
    source.stride = RETRO_GO_GBA_WIDTH * sizeof(uint16_t);
    source.format = VG_LITE_RGB565;
    source.tiled = VG_LITE_LINEAR;
    source.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    source.transparency_mode = VG_LITE_IMAGE_OPAQUE;
    source.memory = (void *)pixels;
    source.address = (uint32_t)(uintptr_t)pixels;

    memset(&target, 0, sizeof(target));
    target.width = RETRO_GO_SCANOUT_WIDTH;
    target.height = RETRO_GO_SCANOUT_HEIGHT;
    target.stride = scanout_stride;
    target.format = VG_LITE_RGB565;
    target.tiled = VG_LITE_LINEAR;
    target.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    target.transparency_mode = VG_LITE_IMAGE_OPAQUE;
    target.memory = scanout;
    target.address = (uint32_t)scanout_start;

#ifdef BSP_RETRO_GO_PERF_OVERLAY
    game_logical_x = 0u;
#else
    game_logical_x =
        (lcd_info.width - RETRO_GO_GBA_OUTPUT_WIDTH) / 2u;
#endif
    cache_clean(pixels, RETRO_GO_GBA_WIDTH * RETRO_GO_GBA_HEIGHT *
                         sizeof(uint16_t));
    status = direct_scanout_buffer_cleared[direct_scanout_back] ?
        VG_LITE_SUCCESS : vg_lite_clear(&target, NULL, 0u);
    if (status == VG_LITE_SUCCESS)
    {
        status = build_direct_scanout_matrix(
            &matrix, RETRO_GO_GBA_WIDTH, RETRO_GO_GBA_HEIGHT,
            game_logical_x, 0u, RETRO_GO_GBA_OUTPUT_WIDTH,
            RETRO_GO_GBA_OUTPUT_HEIGHT);
    }
    if (status == VG_LITE_SUCCESS)
    {
        status = vg_lite_blit(&target, &source, &matrix,
                              VG_LITE_BLEND_NONE, 0u,
                              VG_LITE_FILTER_POINT);
    }
#ifdef BSP_RETRO_GO_PERF_OVERLAY
    if (retro_go_perf_render_due(
            launcher_framebuffer, RETRO_GO_PERF_PANEL_WIDTH,
            RETRO_GO_PERF_PANEL_HEIGHT, RETRO_GO_PERF_PANEL_WIDTH))
    {
        ++direct_panel_revision;
        if (direct_panel_revision == 0u)
        {
            direct_panel_revision = 1u;
            direct_panel_applied_revision[0] = 0u;
            direct_panel_applied_revision[1] = 0u;
        }
    }
    if (status == VG_LITE_SUCCESS &&
        direct_panel_applied_revision[direct_scanout_back] !=
            direct_panel_revision)
    {
        source.width = RETRO_GO_PERF_PANEL_WIDTH;
        source.height = RETRO_GO_PERF_PANEL_HEIGHT;
        source.stride = RETRO_GO_PERF_PANEL_WIDTH * sizeof(uint16_t);
        source.memory = launcher_framebuffer;
        source.address = (uint32_t)(uintptr_t)launcher_framebuffer;
        cache_clean(launcher_framebuffer,
                    RETRO_GO_PERF_PANEL_WIDTH *
                    RETRO_GO_PERF_PANEL_HEIGHT * sizeof(uint16_t));
        status = build_direct_scanout_matrix(
            &matrix, RETRO_GO_PERF_PANEL_WIDTH,
            RETRO_GO_PERF_PANEL_HEIGHT,
            lcd_info.width - RETRO_GO_PERF_PANEL_WIDTH, 0u,
            RETRO_GO_PERF_PANEL_WIDTH, RETRO_GO_PERF_PANEL_HEIGHT);
        if (status == VG_LITE_SUCCESS)
        {
            status = vg_lite_blit(&target, &source, &matrix,
                                  VG_LITE_BLEND_NONE, 0u,
                                  VG_LITE_FILTER_POINT);
            panel_blitted = status == VG_LITE_SUCCESS;
        }
    }
#endif
    if (status == VG_LITE_SUCCESS)
    {
        status = vg_lite_finish();
    }
    if (status != VG_LITE_SUCCESS)
    {
        direct_scanout_failed = true;
        (void)direct_scanout_restore_vendor_buffer();
        rt_kprintf("[retro-go] direct GBA scanout failed: %d; "
                   "staging fallback\n", status);
        return false;
    }
    cache_invalidate(scanout, scanout_stride * RETRO_GO_SCANOUT_HEIGHT);
    {
        rt_base_t level = rt_hw_interrupt_disable();
        cy_en_gfx_status_t flip_status;

        while (lcd_wait_frame_done(0u) == RT_EOK)
        {
        }
        flip_status = Cy_GFXSS_Set_FrameBuffer(
            (GFXSS_Type *)GFXSS, (uint32_t *)scanout,
            &direct_scanout_context);
        /* Discard an IRQ which raced with the commit. The next DC interrupt
         * is therefore guaranteed to occur after this buffer was submitted. */
        Cy_GFXSS_Clear_DC_Interrupt(
            (GFXSS_Type *)GFXSS, &direct_scanout_context);
        NVIC_ClearPendingIRQ(GFXSS_DC_IRQ);
        if (flip_status == CY_GFX_SUCCESS)
        {
            direct_scanout_buffer_cleared[direct_scanout_back] = true;
#ifdef BSP_RETRO_GO_PERF_OVERLAY
            if (panel_blitted)
            {
                direct_panel_applied_revision[direct_scanout_back] =
                    direct_panel_revision;
            }
#endif
            direct_scanout_pending_front = direct_scanout_back;
            direct_scanout_flip_pending = true;
        }
        rt_hw_interrupt_enable(level);
        if (flip_status != CY_GFX_SUCCESS)
        {
            direct_scanout_failed = true;
            (void)direct_scanout_restore_vendor_buffer();
            rt_kprintf("[retro-go] direct GBA scanout flip failed; "
                       "staging fallback\n");
            return false;
        }
    }
    if (!direct_scanout_logged)
    {
        direct_scanout_logged = true;
        rt_kprintf("[retro-go] direct GBA VG-Lite scanout active: "
                   "fb=%p stride=%u, no display worker\n",
                   scanout, (unsigned)scanout_stride);
    }
    return true;
#else
    (void)pixels;
    return false;
#endif
}

static RETRO_GO_ITCM bool present_cpu(const uint16_t *pixels)
{
    uint16_t previous_source_y = UINT16_MAX;
    uint16_t y;

    if (pixels == NULL || viewport.width == 0u || viewport.height == 0u ||
        viewport.width > RETRO_GO_DISPLAY_MAX_VIEWPORT_WIDTH ||
        viewport.height > RETRO_GO_DISPLAY_MAX_HEIGHT)
    {
        return false;
    }

    for (y = 0u; y < viewport.height; ++y)
    {
        const uint16_t *source_row =
            pixels + (size_t)source_y_map[y] * RETRO_GO_GB_WIDTH;
        uint16_t *destination_row =
            scaled_framebuffer +
            (size_t)y * RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS;

        if (y != 0u && source_y_map[y] == previous_source_y)
        {
            memcpy(destination_row,
                   destination_row - RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS,
                   viewport.width * sizeof(destination_row[0]));
        }
        else
        {
            uint16_t x;

            for (x = 0u; x < viewport.width; ++x)
            {
                destination_row[x] = source_row[source_x_map[x]];
            }
        }
        previous_source_y = source_y_map[y];
    }

    lcd_flush_rgb565_area(scaled_framebuffer, viewport.x, viewport.y,
                          viewport.width, viewport.height,
                          RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS,
                          RT_TRUE);
    return true;
}

#ifdef BSP_RETRO_GO_ASYNC_GBA_DISPLAY
static void display_thread_entry(void *parameter)
{
    (void)parameter;
    for (;;)
    {
        if (rt_sem_take(&display_sem, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }
        for (;;)
        {
            uint32_t started_cycles;
            uint32_t elapsed_cycles;
            rt_base_t level;
            int buffer = -1;
            bool success;
            unsigned index;

            level = rt_hw_interrupt_disable();
            for (index = 0u; index < RETRO_GO_GBA_MAILBOX_BUFFERS; ++index)
            {
                if (display_buffer_state[index] ==
                    RETRO_GO_DISPLAY_BUFFER_READY)
                {
                    display_buffer_state[index] =
                        RETRO_GO_DISPLAY_BUFFER_DISPLAYING;
                    buffer = (int)index;
                    break;
                }
            }
            if (buffer < 0)
            {
                display_worker_running = false;
                display_async_busy = false;
                for (index = 0u;
                     index < RETRO_GO_GBA_MAILBOX_BUFFERS; ++index)
                {
                    if (display_buffer_state[index] !=
                        RETRO_GO_DISPLAY_BUFFER_FREE)
                    {
                        display_async_busy = true;
                        break;
                    }
                }
                rt_hw_interrupt_enable(level);
                break;
            }
            rt_hw_interrupt_enable(level);

            started_cycles = retro_go_time_now_cycles();
            success = present_gba_sync(gba_present_framebuffer[buffer]);
            elapsed_cycles = retro_go_time_elapsed_cycles(started_cycles);

            level = rt_hw_interrupt_disable();
            display_buffer_state[buffer] = RETRO_GO_DISPLAY_BUFFER_FREE;
            if (success)
            {
                ++display_completed_frames;
                display_total_present_cycles += elapsed_cycles;
                if (elapsed_cycles > display_max_present_cycles)
                {
                    display_max_present_cycles = elapsed_cycles;
                }
            }
            else
            {
                ++display_failed_frames;
            }
            rt_hw_interrupt_enable(level);
        }
    }
}

static bool start_async_display(void)
{
    rt_err_t result;

    display_async_ready = false;
    display_async_busy = false;
    display_worker_running = false;
    memset((void *)display_buffer_state, RETRO_GO_DISPLAY_BUFFER_FREE,
           sizeof(display_buffer_state));
    display_submitted_frames = 0u;
    display_completed_frames = 0u;
    display_dropped_frames = 0u;
    display_failed_frames = 0u;
    display_total_present_cycles = 0u;
    display_max_present_cycles = 0u;
    result = rt_sem_init(&display_sem, "rg_disp", 0u, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        return false;
    }
    result = rt_thread_init(
        &display_thread, "rg_disp", display_thread_entry, RT_NULL,
        display_thread_stack, sizeof(display_thread_stack),
        BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY,
        BSP_RETRO_GO_DISPLAY_THREAD_SLICE);
    if (result != RT_EOK)
    {
        (void)rt_sem_detach(&display_sem);
        return false;
    }
    result = rt_thread_startup(&display_thread);
    if (result != RT_EOK)
    {
        (void)rt_thread_detach(&display_thread);
        (void)rt_sem_detach(&display_sem);
        return false;
    }
    display_async_ready = true;
    rt_kprintf("[retro-go] async GBA display ready: priority=%u slice=%u, "
               "latest-mailbox=%ux%u bytes\n",
               BSP_RETRO_GO_DISPLAY_THREAD_PRIORITY,
               BSP_RETRO_GO_DISPLAY_THREAD_SLICE,
               RETRO_GO_GBA_MAILBOX_BUFFERS,
               (unsigned)sizeof(gba_present_framebuffer[0]));
    return true;
}
#endif

bool retro_go_display_init(void)
{
    unsigned vglite_enabled = 0u;
    unsigned perf_panel_width = 0u;

    lcd_device = rt_device_find("lcd");
    if (lcd_device == RT_NULL)
    {
        rt_kprintf("[retro-go] lcd device not found\n");
        return false;
    }
    memset(&lcd_info, 0, sizeof(lcd_info));
    if (rt_device_control(lcd_device, RTGRAPHIC_CTRL_GET_INFO,
                          &lcd_info) != RT_EOK ||
        lcd_info.framebuffer == RT_NULL || lcd_info.bits_per_pixel != 16u ||
        lcd_info.width > RETRO_GO_DISPLAY_MAX_WIDTH ||
        lcd_info.height > RETRO_GO_DISPLAY_MAX_HEIGHT)
    {
        rt_kprintf("[retro-go] unsupported lcd framebuffer\n");
        return false;
    }

    viewport = calculate_viewport(lcd_info.width, lcd_info.height);
    build_scale_maps();
    memset(source_framebuffer, 0, sizeof(source_framebuffer));
    memset(launcher_framebuffer, 0, sizeof(launcher_framebuffer));
    memset(gba_framebuffer, 0, sizeof(gba_framebuffer));
    display_submitted_frames = 0u;
    display_completed_frames = 0u;
    display_dropped_frames = 0u;
    display_failed_frames = 0u;
    display_total_present_cycles = 0u;
    display_max_present_cycles = 0u;
    display_ready = true;
#ifdef BSP_RETRO_GO_USE_VGLITE
    vglite_enabled = 1u;
#endif
#ifdef BSP_RETRO_GO_PERF_OVERLAY
    perf_panel_width = RETRO_GO_PERF_PANEL_WIDTH;
#endif
    rt_kprintf("[retro-go] display %ux%u pitch=%u fb=%p, viewport %u,%u "
               "%ux%u, vglite-scaler=%d, perf-panel=%u\n",
               lcd_info.width, lcd_info.height, lcd_info.pitch,
               lcd_info.framebuffer, viewport.x, viewport.y,
               viewport.width, viewport.height, vglite_enabled,
               perf_panel_width);
    if (!present_cpu(source_framebuffer))
    {
        rt_kprintf("[retro-go] display clear failed\n");
        display_ready = false;
        return false;
    }
#ifdef BSP_RETRO_GO_ASYNC_GBA_DISPLAY
    if (!start_async_display())
    {
        rt_kprintf("[retro-go] async display init failed; synchronous fallback\n");
    }
#endif
    return true;
}

uint16_t *retro_go_display_framebuffer(void)
{
    return source_framebuffer;
}

uint16_t *retro_go_display_launcher_framebuffer(void)
{
    return launcher_framebuffer;
}

uint16_t *retro_go_display_gba_framebuffer(void)
{
    return gba_framebuffer;
}

static bool present_gba_sync(const uint16_t *framebuffer)
{
    uint32_t destination_x;
    uint16_t y;

    if (!display_ready || framebuffer == NULL ||
        lcd_info.width < RETRO_GO_GBA_OUTPUT_WIDTH ||
        lcd_info.height < RETRO_GO_GBA_OUTPUT_HEIGHT)
    {
        return false;
    }
    display_present_completed = true;
#ifdef BSP_RETRO_GO_PERF_OVERLAY
    destination_x = 0u;
#else
    destination_x = (lcd_info.width - RETRO_GO_GBA_OUTPUT_WIDTH) / 2u;
#endif
    if (scale_gba_vglite_scanout(framebuffer))
    {
        retro_go_perf_set_vglite(true);
        return true;
    }
    if (scale_gba_cpu_direct(framebuffer))
    {
        static bool cpu_direct_logged;

        stage_performance_panel();
        if (!cpu_direct_logged)
        {
            cpu_direct_logged = true;
            rt_kprintf("[retro-go] GBA direct CPU 3x scaler active\n");
        }
        return rt_device_control(lcd_device, RTGRAPHIC_CTRL_RECT_UPDATE,
                                 RT_NULL) == RT_EOK;
    }
    if (scale_gba_vglite_direct(framebuffer))
    {
        retro_go_perf_set_vglite(true);
        stage_performance_panel();
        return rt_device_control(lcd_device, RTGRAPHIC_CTRL_RECT_UPDATE,
                                 RT_NULL) == RT_EOK;
    }
    if (scale_vglite(framebuffer, RETRO_GO_GBA_WIDTH,
                     RETRO_GO_GBA_HEIGHT, RETRO_GO_GBA_OUTPUT_WIDTH,
                     RETRO_GO_GBA_OUTPUT_HEIGHT))
    {
        retro_go_perf_set_vglite(true);
        stage_performance_panel();
        lcd_flush_rgb565_area(scaled_framebuffer, destination_x, 0u,
                              RETRO_GO_GBA_OUTPUT_WIDTH,
                              RETRO_GO_GBA_OUTPUT_HEIGHT,
                              RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS, RT_TRUE);
        return true;
    }
    retro_go_perf_set_vglite(false);
    for (y = 0u; y < RETRO_GO_GBA_HEIGHT; ++y)
    {
        const uint16_t *source_row =
            framebuffer + (size_t)y * RETRO_GO_GBA_WIDTH;
        uint16_t *first_row =
            scaled_framebuffer +
            (size_t)(y * RETRO_GO_GBA_SCALE) *
                RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS;
        uint16_t x;

        for (x = 0u; x < RETRO_GO_GBA_WIDTH; ++x)
        {
            uint16_t pixel = source_row[x];
            size_t output = (size_t)x * RETRO_GO_GBA_SCALE;

            first_row[output] = pixel;
            first_row[output + 1u] = pixel;
            first_row[output + 2u] = pixel;
        }
        memcpy(first_row + RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS,
               first_row,
               RETRO_GO_GBA_OUTPUT_WIDTH * sizeof(first_row[0]));
        memcpy(first_row + RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS * 2u,
               first_row,
               RETRO_GO_GBA_OUTPUT_WIDTH * sizeof(first_row[0]));
    }
    stage_performance_panel();
    lcd_flush_rgb565_area(scaled_framebuffer, destination_x, 0u,
                          RETRO_GO_GBA_OUTPUT_WIDTH,
                          RETRO_GO_GBA_OUTPUT_HEIGHT,
                          RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS, RT_TRUE);
    return true;
}

bool retro_go_display_present_gba(const uint16_t *framebuffer)
{
#ifdef BSP_RETRO_GO_ASYNC_GBA_DISPLAY
    rt_base_t level;

    if (display_async_ready)
    {
        if (!display_ready || framebuffer == NULL)
        {
            return false;
        }
        level = rt_hw_interrupt_disable();
        {
            int target = -1;
            bool replacing = false;
            bool wake_worker = false;
            unsigned index;

            /* Prefer replacing an unclaimed READY frame. This keeps the
             * mailbox latency-bounded even if the producer preempts the
             * lower-priority worker between two presents. */
            for (index = 0u;
                 index < RETRO_GO_GBA_MAILBOX_BUFFERS; ++index)
            {
                if (display_buffer_state[index] ==
                    RETRO_GO_DISPLAY_BUFFER_READY)
                {
                    target = (int)index;
                    replacing = true;
                    break;
                }
            }
            if (target < 0)
            {
                for (index = 0u;
                     index < RETRO_GO_GBA_MAILBOX_BUFFERS; ++index)
                {
                    if (display_buffer_state[index] ==
                        RETRO_GO_DISPLAY_BUFFER_FREE)
                    {
                        target = (int)index;
                        break;
                    }
                }
            }
            if (target < 0)
            {
                ++display_dropped_frames;
                rt_hw_interrupt_enable(level);
                return false;
            }
            display_buffer_state[target] = RETRO_GO_DISPLAY_BUFFER_WRITING;
            display_async_busy = true;
            if (replacing)
            {
                ++display_dropped_frames;
            }
            rt_hw_interrupt_enable(level);

            memcpy(gba_present_framebuffer[target], framebuffer,
                   sizeof(gba_present_framebuffer[target]));

            level = rt_hw_interrupt_disable();
            display_buffer_state[target] = RETRO_GO_DISPLAY_BUFFER_READY;
            ++display_submitted_frames;
            if (!display_worker_running)
            {
                display_worker_running = true;
                wake_worker = true;
            }
            rt_hw_interrupt_enable(level);
            if (wake_worker)
            {
                (void)rt_sem_release(&display_sem);
            }
            return true;
        }
    }
#endif
    {
        uint32_t started_cycles = retro_go_time_now_cycles();
        bool success;
        uint32_t elapsed_cycles;
        rt_base_t level;

        ++display_submitted_frames;
        success = present_gba_sync(framebuffer);
        elapsed_cycles = retro_go_time_elapsed_cycles(started_cycles);
        level = rt_hw_interrupt_disable();
        if (success && display_present_completed)
        {
            ++display_completed_frames;
            display_total_present_cycles += elapsed_cycles;
            if (elapsed_cycles > display_max_present_cycles)
            {
                display_max_present_cycles = elapsed_cycles;
            }
        }
        else if (!success)
        {
            ++display_failed_frames;
        }
        rt_hw_interrupt_enable(level);
        return success;
    }
}

bool retro_go_display_can_accept_gba(void)
{
#ifdef BSP_RETRO_GO_GBA_DIRECT_SCANOUT
    if (direct_scanout_initialized && !direct_scanout_failed &&
        !direct_scanout_poll_flip(0u))
    {
        return false;
    }
#endif
#ifdef BSP_RETRO_GO_ASYNC_GBA_DISPLAY
    if (display_async_ready)
    {
        /* A READY snapshot can be atomically replaced with the newest frame
         * while the worker owns the DISPLAYING slot. Rendering is therefore
         * no longer suppressed merely because present is slower than 60 Hz. */
        return true;
    }
#endif
    return true;
}

bool retro_go_display_gba_busy(void)
{
#ifdef BSP_RETRO_GO_GBA_DIRECT_SCANOUT
    if (direct_scanout_initialized && !direct_scanout_failed &&
        !direct_scanout_poll_flip(0u))
    {
        return true;
    }
#endif
#ifdef BSP_RETRO_GO_ASYNC_GBA_DISPLAY
    return display_async_ready && display_async_busy;
#else
    return false;
#endif
}

bool retro_go_display_wait_gba_idle(uint32_t timeout_ms)
{
    uint32_t started_ms = rt_tick_get_millisecond();

    while (retro_go_display_gba_busy())
    {
        if ((uint32_t)(rt_tick_get_millisecond() - started_ms) >= timeout_ms)
        {
            return false;
        }
        rt_thread_mdelay(1u);
    }
    return true;
}

void retro_go_display_get_stats(retro_go_display_stats_t *stats)
{
    rt_base_t level;

    if (stats == NULL)
    {
        return;
    }
    memset(stats, 0, sizeof(*stats));
    level = rt_hw_interrupt_disable();
    stats->submitted_frames = display_submitted_frames;
    stats->completed_frames = display_completed_frames;
    stats->dropped_frames = display_dropped_frames;
    stats->failed_frames = display_failed_frames;
    stats->total_present_cycles = display_total_present_cycles;
    stats->max_present_cycles = display_max_present_cycles;
    stats->busy = false;
#ifdef BSP_RETRO_GO_ASYNC_GBA_DISPLAY
    stats->busy = display_async_ready && display_async_busy;
#endif
    rt_hw_interrupt_enable(level);
}

bool retro_go_display_present_launcher(const uint16_t *framebuffer)
{
    uint32_t destination_x;
    uint32_t destination_y;
    uint16_t y;

    if (!display_ready || framebuffer == NULL ||
        lcd_info.width < RETRO_GO_LAUNCHER_OUTPUT_WIDTH ||
        lcd_info.height < RETRO_GO_LAUNCHER_OUTPUT_HEIGHT)
    {
        return false;
    }
#ifdef BSP_RETRO_GO_GBA_DIRECT_SCANOUT
    /* The work-buffer union may currently be the physical front buffer.
     * Restore the vendor-owned scanout before writing launcher pixels into
     * that memory, then forget the game-only double-buffer state. */
    if (!direct_scanout_enter_vendor_path())
    {
        return false;
    }
#endif
    for (y = 0u; y < RETRO_GO_LAUNCHER_HEIGHT; ++y)
    {
        const uint16_t *source_row =
            framebuffer + (size_t)y * RETRO_GO_LAUNCHER_WIDTH;
        uint16_t *first_row =
            scaled_framebuffer +
            (size_t)(y * RETRO_GO_LAUNCHER_SCALE) *
                RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS;
        uint16_t x;

        for (x = 0u; x < RETRO_GO_LAUNCHER_WIDTH; ++x)
        {
            uint16_t pixel = source_row[x];
            first_row[x * RETRO_GO_LAUNCHER_SCALE] = pixel;
            first_row[x * RETRO_GO_LAUNCHER_SCALE + 1u] = pixel;
        }
        memcpy(first_row + RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS,
               first_row,
               RETRO_GO_LAUNCHER_OUTPUT_WIDTH * sizeof(first_row[0]));
    }
    destination_x = (lcd_info.width - RETRO_GO_LAUNCHER_OUTPUT_WIDTH) / 2u;
    destination_y = (lcd_info.height - RETRO_GO_LAUNCHER_OUTPUT_HEIGHT) / 2u;
    lcd_flush_rgb565_area(scaled_framebuffer, destination_x, destination_y,
                          RETRO_GO_LAUNCHER_OUTPUT_WIDTH,
                          RETRO_GO_LAUNCHER_OUTPUT_HEIGHT,
                          RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS, RT_TRUE);
    return true;
}

void retro_go_display_prepare_game(void)
{
#ifndef BSP_RETRO_GO_PERF_OVERLAY
    uint32_t destination_x;
    uint32_t destination_y;
#endif

    if (!display_ready || lcd_info.width < RETRO_GO_LAUNCHER_OUTPUT_WIDTH ||
        lcd_info.height < RETRO_GO_LAUNCHER_OUTPUT_HEIGHT)
    {
        return;
    }
#ifdef BSP_RETRO_GO_GBA_DIRECT_SCANOUT
    if (!direct_scanout_enter_vendor_path())
    {
        return;
    }
#endif
    memset(scaled_framebuffer, 0, sizeof(scaled_framebuffer));
#ifdef BSP_RETRO_GO_PERF_OVERLAY
    memset(launcher_framebuffer, 0, sizeof(launcher_framebuffer));
    lcd_flush_rgb565_area(
        launcher_framebuffer,
        lcd_info.width - RETRO_GO_PERF_PANEL_WIDTH, 0u,
        RETRO_GO_PERF_PANEL_WIDTH, lcd_info.height,
        RETRO_GO_PERF_PANEL_WIDTH, RT_FALSE);
    lcd_flush_rgb565_area(scaled_framebuffer, 0u, 0u,
                          RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS,
                          RETRO_GO_DISPLAY_MAX_HEIGHT,
                          RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS, RT_TRUE);
#else
    destination_x = (lcd_info.width - RETRO_GO_LAUNCHER_OUTPUT_WIDTH) / 2u;
    destination_y = (lcd_info.height - RETRO_GO_LAUNCHER_OUTPUT_HEIGHT) / 2u;
    lcd_flush_rgb565_area(scaled_framebuffer, destination_x, destination_y,
                          RETRO_GO_LAUNCHER_OUTPUT_WIDTH,
                          RETRO_GO_LAUNCHER_OUTPUT_HEIGHT,
                          RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS, RT_TRUE);
#endif
}

bool retro_go_display_present(const uint16_t *framebuffer)
{
    if (!display_ready || framebuffer == NULL)
    {
        return false;
    }

    if (scale_vglite(framebuffer, RETRO_GO_GB_WIDTH, RETRO_GO_GB_HEIGHT,
                     viewport.width, viewport.height))
    {
        retro_go_perf_set_vglite(true);
        stage_performance_panel();
        lcd_flush_rgb565_area(scaled_framebuffer, viewport.x, viewport.y,
                              viewport.width, viewport.height,
                              RETRO_GO_DISPLAY_SCALE_STRIDE_PIXELS,
                              RT_TRUE);
        return true;
    }
    retro_go_perf_set_vglite(false);
    stage_performance_panel();
    return present_cpu(framebuffer);
}
