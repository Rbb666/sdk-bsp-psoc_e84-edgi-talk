#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pal_lcd_api.h"
#include "pal_display_port.h"

#if defined(BSP_SDLPAL_INPUT_TOUCH) == \
    defined(BSP_SDLPAL_INPUT_USB_KEYBOARD)
#error "Test requires exactly one SDLPal input mode"
#endif

#define TEST_SOURCE_PITCH (PAL_GAME_WIDTH + 8u)
#define MAX_AREA_CALLS 128u

typedef struct area_call
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    rt_bool_t present;
    bool all_zero;
} area_call_t;

static uint8_t frame[TEST_SOURCE_PITCH * PAL_GAME_HEIGHT];
static pal_rgb_t colors[256];
static rt_bool_t indexed_result = RT_TRUE;
static unsigned indexed_calls;
static unsigned area_calls;
static unsigned area_present_calls;
static unsigned area_game_rows;
static area_call_t area_call_log[MAX_AREA_CALLS];
static size_t area_call_count;
static const void *indexed_pixels;
static uint32_t indexed_width;
static uint32_t indexed_height;
static uint32_t indexed_pitch;
static uint32_t indexed_x;
static uint32_t indexed_y;
static uint32_t indexed_dst_width;
static uint32_t indexed_dst_height;
static uint32_t indexed_clut_sample;
static rt_bool_t indexed_present;
static uint32_t fake_tick;

static void reset_calls(void)
{
    indexed_calls = 0u;
    area_calls = 0u;
    area_present_calls = 0u;
    area_game_rows = 0u;
    area_call_count = 0u;
    indexed_pixels = NULL;
    indexed_width = 0u;
    indexed_height = 0u;
    indexed_pitch = 0u;
    indexed_x = 0u;
    indexed_y = 0u;
    indexed_dst_width = 0u;
    indexed_dst_height = 0u;
    indexed_clut_sample = 0u;
    indexed_present = RT_FALSE;
}

uint32_t rt_tick_get_millisecond(void)
{
    return fake_tick++;
}

int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}

void lcd_flush_rgb565_area(const void *pixels, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height,
                           uint32_t src_stride, rt_bool_t present)
{
    const uint16_t *source = (const uint16_t *)pixels;
    uint32_t row;
    bool all_zero = true;

    assert(src_stride == width);
    for (row = 0u; row < height; ++row)
    {
        uint32_t column;

        for (column = 0u; column < width; ++column)
        {
            if (source[row * src_stride + column] != 0u)
            {
                all_zero = false;
            }
        }
    }
    assert(area_call_count < MAX_AREA_CALLS);
    area_call_log[area_call_count].x = x;
    area_call_log[area_call_count].y = y;
    area_call_log[area_call_count].width = width;
    area_call_log[area_call_count].height = height;
    area_call_log[area_call_count].present = present;
    area_call_log[area_call_count].all_zero = all_zero;
    ++area_call_count;
    ++area_calls;
    if (y < PAL_GAME_VIEW_HEIGHT && width == PAL_PORTRAIT_WIDTH)
    {
        area_game_rows += height;
    }
    if (present)
    {
        ++area_present_calls;
    }
}

rt_bool_t lcd_blit_indexed8(const void *pixels,
                            uint32_t width, uint32_t height,
                            uint32_t src_stride,
                            const uint32_t *clut,
                            uint32_t x, uint32_t y,
                            uint32_t dst_width, uint32_t dst_height,
                            rt_bool_t present)
{
    ++indexed_calls;
    indexed_pixels = pixels;
    indexed_width = width;
    indexed_height = height;
    indexed_pitch = src_stride;
    indexed_x = x;
    indexed_y = y;
    indexed_dst_width = dst_width;
    indexed_dst_height = dst_height;
    indexed_clut_sample = clut[7];
    indexed_present = present;
    return indexed_result;
}

#if defined(BSP_SDLPAL_INPUT_TOUCH)

static void assert_only_control_rects_were_flushed(uint32_t changed_mask)
{
    pal_control_rect_t controls[PAL_CONTROL_COUNT];
    uint16_t display_width;
    uint16_t display_height;
    size_t control_count;
    size_t expected_call = 0u;
    size_t i;

    pal_display_port_get_dimensions(&display_width, &display_height);
    control_count = pal_controls_layout(display_width, display_height,
                                        controls);
    assert(control_count == PAL_CONTROL_COUNT);

    for (i = 0u; i < control_count; ++i)
    {
        uint16_t row_offset;

        if ((controls[i].mask & changed_mask) == 0u)
        {
            continue;
        }

        for (row_offset = 0u; row_offset < controls[i].height;
             row_offset = (uint16_t)(row_offset + PAL_DISPLAY_STRIP_ROWS))
        {
            uint16_t rows = (uint16_t)(controls[i].height - row_offset);

            if (rows > PAL_DISPLAY_STRIP_ROWS)
            {
                rows = PAL_DISPLAY_STRIP_ROWS;
            }

            assert(expected_call < area_call_count);
            assert(area_call_log[expected_call].x == controls[i].x);
            assert(area_call_log[expected_call].y ==
                   (uint32_t)controls[i].y + row_offset);
            assert(area_call_log[expected_call].width == controls[i].width);
            assert(area_call_log[expected_call].height == rows);
            ++expected_call;
        }
    }

    assert(expected_call == area_call_count);
    assert(area_present_calls == 1u);
    for (i = 0u; i < area_call_count; ++i)
    {
        assert(area_call_log[i].present ==
               (i + 1u == area_call_count ? RT_TRUE : RT_FALSE));
    }
}

static void test_touch_routing_and_fallback(void)
{
    pal_display_metrics_t metrics;
    uint16_t display_width;
    uint16_t display_height;
    uint16_t expected_game_x;

    memset(frame, 0, sizeof(frame));
    memset(colors, 0, sizeof(colors));
    colors[7].r = 0x12u;
    colors[7].g = 0x34u;
    colors[7].b = 0x56u;
    pal_display_port_get_dimensions(&display_width, &display_height);
    expected_game_x = (uint16_t)((display_width - PAL_PORTRAIT_WIDTH) / 2u);
    assert(display_height >= PAL_GAME_VIEW_HEIGHT);

    reset_calls();
    indexed_result = RT_TRUE;
    assert(pal_display_present_indexed(frame, TEST_SOURCE_PITCH, colors));
    assert(indexed_calls == 1u);
    assert(indexed_pixels == frame);
    assert(indexed_width == PAL_GAME_WIDTH);
    assert(indexed_height == PAL_GAME_HEIGHT);
    assert(indexed_pitch == TEST_SOURCE_PITCH);
    assert(indexed_x == expected_game_x);
    assert(indexed_y == 0u);
    assert(indexed_dst_width == PAL_PORTRAIT_WIDTH);
    assert(indexed_dst_height == PAL_GAME_VIEW_HEIGHT);
    assert(indexed_clut_sample == 0xff123456u);
    assert(indexed_present == RT_FALSE);
    assert(area_game_rows == 0u);
    assert(area_present_calls == 1u);

    reset_calls();
    assert(pal_display_present_indexed(frame, TEST_SOURCE_PITCH, colors));
    assert(indexed_calls == 1u);
    assert(indexed_present == RT_TRUE);
    assert(area_calls == 0u);

    reset_calls();
    indexed_result = RT_FALSE;
    assert(pal_display_present_indexed(frame, TEST_SOURCE_PITCH, colors));
    assert(indexed_calls == 1u);
    assert(area_game_rows == PAL_GAME_VIEW_HEIGHT);
    assert(area_present_calls == 1u);

    reset_calls();
    indexed_result = RT_TRUE;
    pal_display_controls_set(PAL_CONTROL_A);
    assert(indexed_calls == 0u);
    assert(area_game_rows == 0u);
    assert_only_control_rects_were_flushed(PAL_CONTROL_A);

    reset_calls();
    pal_display_controls_set(PAL_CONTROL_A);
    assert(area_calls == 0u);

    reset_calls();
    pal_display_controls_set(PAL_CONTROL_A | PAL_CONTROL_RIGHT);
    assert_only_control_rects_were_flushed(PAL_CONTROL_RIGHT);

    reset_calls();
    pal_display_controls_set(0u);
    assert_only_control_rects_were_flushed(PAL_CONTROL_A |
                                           PAL_CONTROL_RIGHT);

    reset_calls();
    assert(pal_display_present_indexed(frame, TEST_SOURCE_PITCH, colors));
    assert(indexed_calls == 1u);
    assert(indexed_present == RT_TRUE);
    assert(area_calls == 0u);

    pal_display_metrics_get(&metrics);
    assert(metrics.frame_count == 4u);
    assert(metrics.vglite_frame_count == 3u);
    assert(metrics.fallback_frame_count == 1u);
    assert(metrics.control_update_count == 3u);
    assert(metrics.control_last_microseconds == 1000u);
    assert(metrics.control_max_microseconds == 1000u);
}

#else

static void assert_black_bars_were_flushed(
    uint16_t display_width, uint16_t display_height,
    const pal_display_viewport_t *viewport)
{
    uint32_t viewport_right = (uint32_t)viewport->x + viewport->width;
    uint32_t viewport_bottom = (uint32_t)viewport->y + viewport->height;
    uint32_t flushed_pixels = 0u;
    size_t i;

    assert(area_call_count > 0u);
    for (i = 0u; i < area_call_count; ++i)
    {
        const area_call_t *call = &area_call_log[i];
        uint32_t call_right = call->x + call->width;
        uint32_t call_bottom = call->y + call->height;

        assert(call->all_zero);
        assert(call_right <= viewport->x || call->x >= viewport_right ||
               call_bottom <= viewport->y || call->y >= viewport_bottom);
        flushed_pixels += call->width * call->height;
        assert(call->present ==
               (i + 1u == area_call_count ? RT_TRUE : RT_FALSE));
    }
    assert(flushed_pixels ==
           (uint32_t)display_width * display_height -
               (uint32_t)viewport->width * viewport->height);
    assert(area_present_calls == 1u);
}

static void assert_cpu_viewport_was_flushed(
    const pal_display_viewport_t *viewport)
{
    uint16_t rows = 0u;
    size_t i;

    assert(area_call_count > 0u);
    for (i = 0u; i < area_call_count; ++i)
    {
        const area_call_t *call = &area_call_log[i];

        assert(call->x == viewport->x);
        assert(call->y == (uint32_t)viewport->y + rows);
        assert(call->width == viewport->width);
        assert(call->height <= PAL_DISPLAY_STRIP_ROWS);
        rows = (uint16_t)(rows + call->height);
        assert(call->present ==
               (i + 1u == area_call_count ? RT_TRUE : RT_FALSE));
    }
    assert(rows == viewport->height);
    assert(area_present_calls == 1u);
}

static void test_keyboard_routing_and_fallback(void)
{
    pal_display_metrics_t metrics;
    pal_display_viewport_t viewport;
    uint16_t display_width;
    uint16_t display_height;

    memset(frame, 0, sizeof(frame));
    memset(colors, 0, sizeof(colors));
    pal_display_port_get_dimensions(&display_width, &display_height);
    assert(pal_display_viewport_get(display_width, display_height, false,
                                    &viewport));

    reset_calls();
    indexed_result = RT_TRUE;
    assert(pal_display_present_indexed(frame, TEST_SOURCE_PITCH, colors));
    assert(indexed_calls == 1u);
    assert(indexed_pixels == frame);
    assert(indexed_width == PAL_GAME_WIDTH);
    assert(indexed_height == PAL_GAME_HEIGHT);
    assert(indexed_pitch == TEST_SOURCE_PITCH);
    assert(indexed_x == viewport.x);
    assert(indexed_y == viewport.y);
    assert(indexed_dst_width == viewport.width);
    assert(indexed_dst_height == viewport.height);
    assert(indexed_present == RT_FALSE);
    assert_black_bars_were_flushed(display_width, display_height,
                                   &viewport);

    reset_calls();
    assert(pal_display_present_indexed(frame, TEST_SOURCE_PITCH, colors));
    assert(indexed_calls == 1u);
    assert(indexed_present == RT_TRUE);
    assert(area_calls == 0u);

    reset_calls();
    pal_display_controls_set(PAL_CONTROL_A | PAL_CONTROL_RIGHT);
    assert(area_calls == 0u);

    reset_calls();
    indexed_result = RT_FALSE;
    assert(pal_display_present_indexed(frame, TEST_SOURCE_PITCH, colors));
    assert(indexed_calls == 1u);
    assert_cpu_viewport_was_flushed(&viewport);

    pal_display_metrics_get(&metrics);
    assert(metrics.frame_count == 3u);
    assert(metrics.vglite_frame_count == 2u);
    assert(metrics.fallback_frame_count == 1u);
    assert(metrics.control_update_count == 0u);
}

#endif

int main(void)
{
#if defined(BSP_SDLPAL_INPUT_TOUCH)
    test_touch_routing_and_fallback();
    puts("display_port_touch: PASS");
#else
    test_keyboard_routing_and_fallback();
    puts("display_port_keyboard: PASS");
#endif
    return 0;
}
