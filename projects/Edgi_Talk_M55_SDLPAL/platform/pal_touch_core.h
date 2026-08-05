#ifndef PAL_TOUCH_CORE_H
#define PAL_TOUCH_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pal_controls.h"

#define PAL_TOUCH_PHYSICAL_WIDTH 480u
#define PAL_TOUCH_PHYSICAL_HEIGHT 800u
#define PAL_TOUCH_MAX_POINTS 5u

typedef struct pal_touch_point
{
    uint16_t x;
    uint16_t y;
    uint8_t id;
    uint8_t active;
} pal_touch_point_t;

typedef struct pal_touch_calibration
{
    bool swap_xy;
    bool invert_x;
    bool invert_y;
} pal_touch_calibration_t;

bool pal_touch_transform(uint16_t physical_x, uint16_t physical_y,
                         uint16_t rotation,
                         const pal_touch_calibration_t *calibration,
                         uint16_t *logical_x, uint16_t *logical_y);
uint32_t pal_touch_controls(const pal_touch_point_t *points, size_t count,
                            uint16_t rotation,
                            const pal_touch_calibration_t *calibration);

#endif
