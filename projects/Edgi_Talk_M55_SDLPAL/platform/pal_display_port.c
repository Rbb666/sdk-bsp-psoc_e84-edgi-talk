#include "pal_display_port.h"

#include <limits.h>
#include <string.h>

#include <rtthread.h>

#include "drv_lcd.h"

#ifndef BSP_LCD_ROTATION_DEGREES
#define BSP_LCD_ROTATION_DEGREES 0
#endif

#if BSP_LCD_ROTATION_DEGREES == 90 || BSP_LCD_ROTATION_DEGREES == 270
#define PAL_DISPLAY_LOGICAL_WIDTH 800u
#define PAL_DISPLAY_LOGICAL_HEIGHT 480u
#else
#define PAL_DISPLAY_LOGICAL_WIDTH 480u
#define PAL_DISPLAY_LOGICAL_HEIGHT 800u
#endif

#if defined(__GNUC__)
#define PAL_GFX_BUFFER __attribute__((section(".cy_gpu_buf"), aligned(64)))
#else
#define PAL_GFX_BUFFER
#endif

static PAL_GFX_BUFFER uint16_t
    display_strip[PAL_DISPLAY_STRIP_WIDTH * PAL_DISPLAY_STRIP_ROWS];
static uint32_t requested_control_mask;
static uint32_t rendered_control_mask;
static pal_display_metrics_t display_metrics;
static bool controls_initialized;
static bool indexed_success_logged;
static bool indexed_fallback_logged;

static uint32_t display_now_microseconds(void)
{
    return (uint32_t)(rt_tick_get_millisecond() * 1000u);
}

static bool flush_control_region(uint16_t x, uint16_t y,
                                 uint16_t width, uint16_t height,
                                 bool present_on_last)
{
    uint16_t row_offset;

    for (row_offset = 0u; row_offset < height;
         row_offset = (uint16_t)(row_offset + PAL_DISPLAY_STRIP_ROWS))
    {
        uint16_t rows = (uint16_t)(height - row_offset);
        uint16_t column_offset;

        if (rows > PAL_DISPLAY_STRIP_ROWS)
        {
            rows = PAL_DISPLAY_STRIP_ROWS;
        }

        for (column_offset = 0u; column_offset < width;
             column_offset = (uint16_t)(column_offset +
                                        PAL_DISPLAY_STRIP_WIDTH))
        {
            uint16_t columns = (uint16_t)(width - column_offset);
            bool last;

            if (columns > PAL_DISPLAY_STRIP_WIDTH)
            {
                columns = PAL_DISPLAY_STRIP_WIDTH;
            }
            memset(display_strip, 0,
                   (size_t)columns * rows * sizeof(display_strip[0]));
            pal_display_render_controls_area(
                display_strip, columns,
                PAL_DISPLAY_LOGICAL_WIDTH, PAL_DISPLAY_LOGICAL_HEIGHT,
                (uint16_t)(x + column_offset),
                (uint16_t)(y + row_offset), columns, rows,
                requested_control_mask);
            last = present_on_last &&
                   (uint16_t)(row_offset + rows) == height &&
                   (uint16_t)(column_offset + columns) == width;
            lcd_flush_rgb565_area(display_strip,
                                  (uint16_t)(x + column_offset),
                                  (uint16_t)(y + row_offset),
                                  columns, rows, columns,
                                  last ? RT_TRUE : RT_FALSE);
        }
    }

    return true;
}

static void flush_full_controls(void)
{
    if (PAL_DISPLAY_LOGICAL_WIDTH <= PAL_DISPLAY_LOGICAL_HEIGHT)
    {
        (void)flush_control_region(0u, PAL_GAME_VIEW_HEIGHT,
                                   PAL_DISPLAY_LOGICAL_WIDTH,
                                   (uint16_t)(PAL_DISPLAY_LOGICAL_HEIGHT -
                                              PAL_GAME_VIEW_HEIGHT),
                                   true);
    }
    else
    {
        (void)flush_control_region(0u, 0u, 160u,
                                   PAL_DISPLAY_LOGICAL_HEIGHT, false);
        (void)flush_control_region(640u, 0u, 160u,
                                   PAL_DISPLAY_LOGICAL_HEIGHT, false);
        (void)flush_control_region(160u, PAL_GAME_VIEW_HEIGHT, 480u,
                                   (uint16_t)(PAL_DISPLAY_LOGICAL_HEIGHT -
                                              PAL_GAME_VIEW_HEIGHT),
                                   true);
    }
}

static void flush_dirty_controls(uint32_t changed_mask)
{
    pal_control_rect_t controls[PAL_CONTROL_COUNT];
    size_t control_count;
    size_t last_changed = PAL_CONTROL_COUNT;
    size_t i;

    control_count = pal_controls_layout(PAL_DISPLAY_LOGICAL_WIDTH,
                                        PAL_DISPLAY_LOGICAL_HEIGHT,
                                        controls);
    for (i = 0u; i < control_count; ++i)
    {
        if ((controls[i].mask & changed_mask) != 0u)
        {
            last_changed = i;
        }
    }

    for (i = 0u; i < control_count; ++i)
    {
        if ((controls[i].mask & changed_mask) == 0u)
        {
            continue;
        }

        (void)flush_control_region(controls[i].x, controls[i].y,
                                   controls[i].width, controls[i].height,
                                   i == last_changed);
    }
}

static bool present_cpu_fallback(const uint8_t *pixels, size_t pitch,
                                 const pal_display_palette_t *palette,
                                 uint16_t game_x, bool present_on_last)
{
    uint16_t first_y;

    for (first_y = 0u; first_y < PAL_GAME_VIEW_HEIGHT;
         first_y = (uint16_t)(first_y + PAL_DISPLAY_STRIP_ROWS))
    {
        uint16_t rows = (uint16_t)(PAL_GAME_VIEW_HEIGHT - first_y);
        bool present;

        if (rows > PAL_DISPLAY_STRIP_ROWS)
        {
            rows = PAL_DISPLAY_STRIP_ROWS;
        }
        if (pal_display_convert_rows(pixels, pitch, first_y, rows,
                                     palette, display_strip,
                                     PAL_DISPLAY_STRIP_WIDTH) != rows)
        {
            return false;
        }
        present = present_on_last &&
                  (uint16_t)(first_y + rows) == PAL_GAME_VIEW_HEIGHT;
        lcd_flush_rgb565_area(display_strip, game_x, first_y,
                              PAL_PORTRAIT_WIDTH, rows,
                              PAL_DISPLAY_STRIP_WIDTH,
                              present ? RT_TRUE : RT_FALSE);
    }

    return true;
}

bool pal_display_present_indexed(const uint8_t *pixels, size_t pitch,
                                 const pal_rgb_t palette[256])
{
    pal_display_palette_t converted_palette;
    uint16_t game_x = (uint16_t)((PAL_DISPLAY_LOGICAL_WIDTH -
                                  PAL_PORTRAIT_WIDTH) /
                                 2u);
    bool controls_changed;
    bool indexed_ok = false;
    uint32_t started;

    if (pixels == NULL || palette == NULL || pitch < PAL_GAME_WIDTH ||
        pitch > UINT32_MAX)
    {
        return false;
    }

    started = display_now_microseconds();
    controls_changed = !controls_initialized ||
                       rendered_control_mask != requested_control_mask;
    pal_display_palette_set(&converted_palette, palette);

#ifdef BSP_LCD_VGLITE_INDEXED
    indexed_ok = lcd_blit_indexed8(pixels,
                                   PAL_GAME_WIDTH, PAL_GAME_HEIGHT,
                                   (uint32_t)pitch,
                                   converted_palette.argb8888,
                                   game_x, 0u,
                                   PAL_PORTRAIT_WIDTH,
                                   PAL_GAME_VIEW_HEIGHT,
                                   controls_changed ? RT_FALSE : RT_TRUE);
#endif
    if (indexed_ok)
    {
        ++display_metrics.vglite_frame_count;
        if (!indexed_success_logged)
        {
            rt_kprintf("[PAL DISPLAY] VG-Lite indexed path active\n");
            indexed_success_logged = true;
        }
    }
    else
    {
        ++display_metrics.fallback_frame_count;
        if (!indexed_fallback_logged)
        {
            rt_kprintf("[PAL DISPLAY] VG-Lite indexed path unavailable; using CPU fallback\n");
            indexed_fallback_logged = true;
        }
        if (!present_cpu_fallback(pixels, pitch, &converted_palette,
                                  game_x, !controls_changed))
        {
            return false;
        }
    }

    if (controls_changed)
    {
        if (controls_initialized)
        {
            flush_dirty_controls(rendered_control_mask ^
                                 requested_control_mask);
        }
        else
        {
            flush_full_controls();
            controls_initialized = true;
        }
        rendered_control_mask = requested_control_mask;
    }

    display_metrics.last_microseconds =
        display_now_microseconds() - started;
    if (display_metrics.last_microseconds > display_metrics.max_microseconds)
    {
        display_metrics.max_microseconds = display_metrics.last_microseconds;
    }
    ++display_metrics.frame_count;
    return true;
}

void pal_display_controls_set(uint32_t pressed_mask)
{
    uint32_t changed_mask;
    uint32_t started;

    if (pressed_mask == requested_control_mask)
    {
        return;
    }

    requested_control_mask = pressed_mask;
    if (!controls_initialized)
    {
        return;
    }

    changed_mask = rendered_control_mask ^ requested_control_mask;
    started = display_now_microseconds();
    flush_dirty_controls(changed_mask);
    rendered_control_mask = requested_control_mask;

    display_metrics.control_last_microseconds =
        display_now_microseconds() - started;
    if (display_metrics.control_last_microseconds >
        display_metrics.control_max_microseconds)
    {
        display_metrics.control_max_microseconds =
            display_metrics.control_last_microseconds;
    }
    ++display_metrics.control_update_count;
}

void pal_display_metrics_get(pal_display_metrics_t *metrics)
{
    if (metrics != NULL)
    {
        *metrics = display_metrics;
    }
}

void pal_display_port_get_dimensions(uint16_t *width, uint16_t *height)
{
    if (width != NULL)
    {
        *width = PAL_DISPLAY_LOGICAL_WIDTH;
    }
    if (height != NULL)
    {
        *height = PAL_DISPLAY_LOGICAL_HEIGHT;
    }
}

uint16_t *pal_display_port_work_buffer(size_t *capacity_pixels)
{
    if (capacity_pixels != NULL)
    {
        *capacity_pixels = PAL_DISPLAY_STRIP_WIDTH * PAL_DISPLAY_STRIP_ROWS;
    }
    return display_strip;
}

bool pal_display_port_flush_work_area(uint16_t x, uint16_t y,
                                      uint16_t width, uint16_t height,
                                      size_t stride_pixels, bool present)
{
    if (width == 0u || height == 0u || width > PAL_DISPLAY_STRIP_WIDTH ||
        height > PAL_DISPLAY_STRIP_ROWS || stride_pixels < width ||
        stride_pixels > PAL_DISPLAY_STRIP_WIDTH ||
        (uint32_t)x + width > PAL_DISPLAY_LOGICAL_WIDTH ||
        (uint32_t)y + height > PAL_DISPLAY_LOGICAL_HEIGHT)
    {
        return false;
    }

    lcd_flush_rgb565_area(display_strip, x, y, width, height,
                          (uint32_t)stride_pixels,
                          present ? RT_TRUE : RT_FALSE);
    return true;
}
