#include "pal_controls.h"

typedef struct pal_control_base_rect
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t mask;
} pal_control_base_rect_t;

static const pal_control_base_rect_t portrait_layout[PAL_CONTROL_COUNT] = {
    {100u, 430u, 80u, 80u, PAL_CONTROL_UP},
    {100u, 590u, 80u, 80u, PAL_CONTROL_DOWN},
    {20u, 510u, 80u, 80u, PAL_CONTROL_LEFT},
    {180u, 510u, 80u, 80u, PAL_CONTROL_RIGHT},
    {384u, 460u, 72u, 72u, PAL_CONTROL_A},
    {300u, 540u, 72u, 72u, PAL_CONTROL_B},
    {20u, 330u, 132u, 56u, PAL_CONTROL_PGUP},
    {328u, 330u, 132u, 56u, PAL_CONTROL_PGDN},
};

static const pal_control_base_rect_t landscape_layout[PAL_CONTROL_COUNT] = {
    {50u, 150u, 60u, 60u, PAL_CONTROL_UP},
    {50u, 290u, 60u, 60u, PAL_CONTROL_DOWN},
    {10u, 220u, 60u, 60u, PAL_CONTROL_LEFT},
    {90u, 220u, 60u, 60u, PAL_CONTROL_RIGHT},
    {720u, 160u, 64u, 64u, PAL_CONTROL_A},
    {650u, 250u, 64u, 64u, PAL_CONTROL_B},
    {180u, 360u, 180u, 60u, PAL_CONTROL_PGUP},
    {440u, 360u, 180u, 60u, PAL_CONTROL_PGDN},
};

static uint16_t scale_value(uint16_t value, uint16_t actual,
                            uint16_t reference)
{
    return (uint16_t)(((uint32_t)value * actual) / reference);
}

size_t pal_controls_layout(uint16_t width, uint16_t height,
                           pal_control_rect_t rects[PAL_CONTROL_COUNT])
{
    const pal_control_base_rect_t *base;
    uint16_t reference_width;
    uint16_t reference_height;
    size_t i;

    if (rects == NULL || width == 0u || height == 0u)
    {
        return 0u;
    }

    if (width > height)
    {
        base = landscape_layout;
        reference_width = 800u;
        reference_height = 480u;
    }
    else
    {
        base = portrait_layout;
        reference_width = 480u;
        reference_height = 800u;
    }

    for (i = 0u; i < PAL_CONTROL_COUNT; ++i)
    {
        rects[i].x = scale_value(base[i].x, width, reference_width);
        rects[i].y = scale_value(base[i].y, height, reference_height);
        rects[i].width = scale_value(base[i].width, width, reference_width);
        rects[i].height = scale_value(base[i].height, height, reference_height);
        rects[i].mask = base[i].mask;
        if (rects[i].width == 0u)
        {
            rects[i].width = 1u;
        }
        if (rects[i].height == 0u)
        {
            rects[i].height = 1u;
        }
    }

    return PAL_CONTROL_COUNT;
}

bool pal_control_rect_contains(const pal_control_rect_t *rect,
                               uint16_t x, uint16_t y)
{
    uint32_t right;
    uint32_t bottom;

    if (rect == NULL || rect->width == 0u || rect->height == 0u)
    {
        return false;
    }

    right = (uint32_t)rect->x + rect->width;
    bottom = (uint32_t)rect->y + rect->height;
    return x >= rect->x && y >= rect->y &&
           (uint32_t)x < right && (uint32_t)y < bottom;
}
