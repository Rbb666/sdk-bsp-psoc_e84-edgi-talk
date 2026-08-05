#include "pal_touch_core.h"

static void apply_calibration(uint16_t physical_x, uint16_t physical_y,
                              const pal_touch_calibration_t *calibration,
                              uint16_t *calibrated_x,
                              uint16_t *calibrated_y)
{
    uint16_t x = physical_x;
    uint16_t y = physical_y;

    if (calibration != NULL && calibration->swap_xy)
    {
        x = (uint16_t)(((uint32_t)physical_y *
                       (PAL_TOUCH_PHYSICAL_WIDTH - 1u)) /
                      (PAL_TOUCH_PHYSICAL_HEIGHT - 1u));
        y = (uint16_t)(((uint32_t)physical_x *
                       (PAL_TOUCH_PHYSICAL_HEIGHT - 1u)) /
                      (PAL_TOUCH_PHYSICAL_WIDTH - 1u));
    }

    if (calibration != NULL && calibration->invert_x)
    {
        x = (uint16_t)((PAL_TOUCH_PHYSICAL_WIDTH - 1u) - x);
    }
    if (calibration != NULL && calibration->invert_y)
    {
        y = (uint16_t)((PAL_TOUCH_PHYSICAL_HEIGHT - 1u) - y);
    }

    *calibrated_x = x;
    *calibrated_y = y;
}

bool pal_touch_transform(uint16_t physical_x, uint16_t physical_y,
                         uint16_t rotation,
                         const pal_touch_calibration_t *calibration,
                         uint16_t *logical_x, uint16_t *logical_y)
{
    uint16_t x;
    uint16_t y;

    if (logical_x == NULL || logical_y == NULL ||
        physical_x >= PAL_TOUCH_PHYSICAL_WIDTH ||
        physical_y >= PAL_TOUCH_PHYSICAL_HEIGHT)
    {
        return false;
    }

    apply_calibration(physical_x, physical_y, calibration, &x, &y);

    switch (rotation)
    {
    case 0u:
        *logical_x = x;
        *logical_y = y;
        break;
    case 90u:
        *logical_x = y;
        *logical_y = (uint16_t)((PAL_TOUCH_PHYSICAL_WIDTH - 1u) - x);
        break;
    case 180u:
        *logical_x = (uint16_t)((PAL_TOUCH_PHYSICAL_WIDTH - 1u) - x);
        *logical_y = (uint16_t)((PAL_TOUCH_PHYSICAL_HEIGHT - 1u) - y);
        break;
    case 270u:
        *logical_x = (uint16_t)((PAL_TOUCH_PHYSICAL_HEIGHT - 1u) - y);
        *logical_y = x;
        break;
    default:
        return false;
    }

    return true;
}

uint32_t pal_touch_controls(const pal_touch_point_t *points, size_t count,
                            uint16_t rotation,
                            const pal_touch_calibration_t *calibration)
{
    pal_control_rect_t rects[PAL_CONTROL_COUNT];
    uint16_t logical_width;
    uint16_t logical_height;
    uint32_t mask = 0u;
    size_t point_index;

    if (points == NULL || count == 0u)
    {
        return 0u;
    }
    if (count > PAL_TOUCH_MAX_POINTS)
    {
        count = PAL_TOUCH_MAX_POINTS;
    }

    if (rotation == 0u || rotation == 180u)
    {
        logical_width = PAL_TOUCH_PHYSICAL_WIDTH;
        logical_height = PAL_TOUCH_PHYSICAL_HEIGHT;
    }
    else if (rotation == 90u || rotation == 270u)
    {
        logical_width = PAL_TOUCH_PHYSICAL_HEIGHT;
        logical_height = PAL_TOUCH_PHYSICAL_WIDTH;
    }
    else
    {
        return 0u;
    }

    if (pal_controls_layout(logical_width, logical_height, rects) !=
        PAL_CONTROL_COUNT)
    {
        return 0u;
    }

    for (point_index = 0u; point_index < count; ++point_index)
    {
        uint16_t x;
        uint16_t y;
        size_t rect_index;

        if (points[point_index].active == 0u ||
            !pal_touch_transform(points[point_index].x,
                                 points[point_index].y,
                                 rotation, calibration, &x, &y))
        {
            continue;
        }

        for (rect_index = 0u; rect_index < PAL_CONTROL_COUNT; ++rect_index)
        {
            if (pal_control_rect_contains(&rects[rect_index], x, y))
            {
                mask |= rects[rect_index].mask;
            }
        }
    }

    return mask;
}
