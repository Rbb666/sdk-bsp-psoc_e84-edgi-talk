#ifndef PAL_DISPLAY_CORE_H
#define PAL_DISPLAY_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pal_controls.h"

#define PAL_GAME_WIDTH 320u
#define PAL_GAME_HEIGHT 200u
#define PAL_PORTRAIT_WIDTH 480u
#define PAL_PORTRAIT_HEIGHT 800u
#define PAL_GAME_VIEW_HEIGHT 300u

typedef struct pal_rgb
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} pal_rgb_t;

typedef struct pal_display_palette
{
    uint16_t rgb565[256];
    uint32_t argb8888[256];
} pal_display_palette_t;

typedef struct pal_display_viewport
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} pal_display_viewport_t;

void pal_display_palette_set(pal_display_palette_t *palette,
                             const pal_rgb_t colors[256]);
bool pal_display_viewport_get(uint16_t width, uint16_t height,
                              bool touch_controls,
                              pal_display_viewport_t *viewport);
size_t pal_display_convert_scaled_rows(
    const uint8_t *indexed, size_t src_pitch,
    uint16_t dst_width, uint16_t dst_height,
    uint16_t first_dst_y, uint16_t row_count,
    const pal_display_palette_t *palette,
    uint16_t *dst, size_t dst_pitch_pixels);
size_t pal_display_convert_rows(const uint8_t *indexed, size_t src_pitch,
                                uint16_t first_dst_y, uint16_t row_count,
                                const pal_display_palette_t *palette,
                                uint16_t *dst, size_t dst_pitch_pixels);
void pal_display_draw_controls(uint16_t *dst, size_t pitch,
                               uint16_t width, uint16_t height,
                               uint32_t pressed_mask);
void pal_display_render_controls_area(uint16_t *dst, size_t pitch,
                                      uint16_t logical_width,
                                      uint16_t logical_height,
                                      uint16_t first_x, uint16_t first_y,
                                      uint16_t area_width,
                                      uint16_t area_height,
                                      uint32_t pressed_mask);

#endif
