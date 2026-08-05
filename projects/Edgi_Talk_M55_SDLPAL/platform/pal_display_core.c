#include "pal_display_core.h"

#include <stdbool.h>

#define PAL_CONTROL_BACKGROUND 0x1082u

static const uint16_t control_colors[PAL_CONTROL_COUNT] = {
    0x2d69u, 0x2d69u, 0x2d69u, 0x2d69u,
    0xd945u, 0x355du, 0x9a66u, 0x04b4u,
};

static uint16_t brighten_rgb565(uint16_t color)
{
    uint16_t red = (uint16_t)((color >> 11) & 0x1fu);
    uint16_t green = (uint16_t)((color >> 5) & 0x3fu);
    uint16_t blue = (uint16_t)(color & 0x1fu);

    red = (uint16_t)(red + (31u - red) / 2u);
    green = (uint16_t)(green + (63u - green) / 2u);
    blue = (uint16_t)(blue + (31u - blue) / 2u);
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static bool is_control_background(uint16_t width, uint16_t height,
                                  uint16_t x, uint16_t y)
{
    if (width <= height)
    {
        uint16_t game_bottom = (uint16_t)(((uint32_t)PAL_GAME_VIEW_HEIGHT *
                                           height) /
                                          PAL_PORTRAIT_HEIGHT);
        return y >= game_bottom;
    }

    return x < (uint16_t)(((uint32_t)160u * width) / 800u) ||
           x >= (uint16_t)(((uint32_t)640u * width) / 800u) ||
           y >= (uint16_t)(((uint32_t)300u * height) / 480u);
}

void pal_display_palette_set(pal_display_palette_t *palette,
                             const pal_rgb_t colors[256])
{
    size_t i;

    if (palette == NULL || colors == NULL)
    {
        return;
    }

    for (i = 0u; i < 256u; ++i)
    {
        palette->rgb565[i] = (uint16_t)(((uint16_t)(colors[i].r >> 3) << 11) |
                                        ((uint16_t)(colors[i].g >> 2) << 5) |
                                        (uint16_t)(colors[i].b >> 3));
        palette->argb8888[i] = 0xff000000u |
                               ((uint32_t)colors[i].r << 16) |
                               ((uint32_t)colors[i].g << 8) |
                               (uint32_t)colors[i].b;
    }
}

size_t pal_display_convert_rows(const uint8_t *indexed, size_t src_pitch,
                                uint16_t first_dst_y, uint16_t row_count,
                                const pal_display_palette_t *palette,
                                uint16_t *dst, size_t dst_pitch_pixels)
{
    uint16_t available;
    uint16_t row;

    if (indexed == NULL || palette == NULL || dst == NULL ||
        src_pitch < PAL_GAME_WIDTH ||
        dst_pitch_pixels < PAL_PORTRAIT_WIDTH ||
        first_dst_y >= PAL_GAME_VIEW_HEIGHT)
    {
        return 0u;
    }

    available = (uint16_t)(PAL_GAME_VIEW_HEIGHT - first_dst_y);
    if (row_count > available)
    {
        row_count = available;
    }

    for (row = 0u; row < row_count; ++row)
    {
        uint32_t dst_y = (uint32_t)first_dst_y + row;
        uint32_t src_y = (dst_y * 2u) / 3u;
        uint16_t dst_x;

        for (dst_x = 0u; dst_x < PAL_PORTRAIT_WIDTH; ++dst_x)
        {
            uint32_t src_x = ((uint32_t)dst_x * 2u) / 3u;
            uint8_t index = indexed[src_y * src_pitch + src_x];
            dst[(size_t)row * dst_pitch_pixels + dst_x] =
                palette->rgb565[index];
        }
    }

    return row_count;
}

void pal_display_draw_controls(uint16_t *dst, size_t pitch,
                               uint16_t width, uint16_t height,
                               uint32_t pressed_mask)
{
    if (dst == NULL || width == 0u || height == 0u || pitch < width)
    {
        return;
    }

    pal_display_render_controls_area(dst, pitch, width, height,
                                     0u, 0u, width, height,
                                     pressed_mask);
}

void pal_display_render_controls_area(uint16_t *dst, size_t pitch,
                                      uint16_t logical_width,
                                      uint16_t logical_height,
                                      uint16_t first_x, uint16_t first_y,
                                      uint16_t area_width,
                                      uint16_t area_height,
                                      uint32_t pressed_mask)
{
    pal_control_rect_t rects[PAL_CONTROL_COUNT];
    size_t count;
    uint16_t local_y;

    if (dst == NULL || logical_width == 0u || logical_height == 0u ||
        area_width == 0u || area_height == 0u || pitch < area_width ||
        (uint32_t)first_x + area_width > logical_width ||
        (uint32_t)first_y + area_height > logical_height)
    {
        return;
    }

    count = pal_controls_layout(logical_width, logical_height, rects);
    for (local_y = 0u; local_y < area_height; ++local_y)
    {
        uint16_t local_x;
        uint16_t y = (uint16_t)(first_y + local_y);

        for (local_x = 0u; local_x < area_width; ++local_x)
        {
            uint16_t x = (uint16_t)(first_x + local_x);
            uint16_t color;
            size_t i;

            if (!is_control_background(logical_width, logical_height, x, y))
            {
                continue;
            }

            color = PAL_CONTROL_BACKGROUND;
            for (i = 0u; i < count; ++i)
            {
                if (pal_control_rect_contains(&rects[i], x, y))
                {
                    color = control_colors[i];
                    if ((pressed_mask & rects[i].mask) != 0u)
                    {
                        color = brighten_rgb565(color);
                    }
                    break;
                }
            }
            dst[(size_t)local_y * pitch + local_x] = color;
        }
    }
}
