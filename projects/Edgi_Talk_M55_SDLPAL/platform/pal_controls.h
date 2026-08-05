#ifndef PAL_CONTROLS_H
#define PAL_CONTROLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum pal_control
{
    PAL_CONTROL_UP = 1u << 0,
    PAL_CONTROL_DOWN = 1u << 1,
    PAL_CONTROL_LEFT = 1u << 2,
    PAL_CONTROL_RIGHT = 1u << 3,
    PAL_CONTROL_A = 1u << 4,
    PAL_CONTROL_B = 1u << 5,
    PAL_CONTROL_PGUP = 1u << 6,
    PAL_CONTROL_PGDN = 1u << 7
} pal_control_t;

typedef struct pal_control_rect
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t mask;
} pal_control_rect_t;

#define PAL_CONTROL_COUNT 8u

size_t pal_controls_layout(uint16_t width, uint16_t height,
                           pal_control_rect_t rects[PAL_CONTROL_COUNT]);
bool pal_control_rect_contains(const pal_control_rect_t *rect,
                               uint16_t x, uint16_t y);

#endif
