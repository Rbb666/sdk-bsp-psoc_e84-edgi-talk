#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pal_touch_core.h"

static pal_touch_point_t point_in(const pal_control_rect_t *rect, uint8_t id)
{
    pal_touch_point_t point;
    point.x = (uint16_t)(rect->x + rect->width / 2u);
    point.y = (uint16_t)(rect->y + rect->height / 2u);
    point.id = id;
    point.active = 1u;
    return point;
}

static pal_touch_point_t physical_point_for_logical(uint16_t logical_x,
                                                     uint16_t logical_y,
                                                     uint16_t rotation,
                                                     uint8_t id)
{
    pal_touch_point_t point;

    if (rotation == 90u)
    {
        point.x = (uint16_t)((PAL_TOUCH_PHYSICAL_WIDTH - 1u) - logical_y);
        point.y = logical_x;
    }
    else
    {
        assert(rotation == 270u);
        point.x = logical_y;
        point.y = (uint16_t)((PAL_TOUCH_PHYSICAL_HEIGHT - 1u) - logical_x);
    }
    point.id = id;
    point.active = 1u;
    return point;
}

static const pal_control_rect_t *find_rect(
    const pal_control_rect_t rects[PAL_CONTROL_COUNT], uint32_t mask)
{
    size_t i;
    for (i = 0u; i < PAL_CONTROL_COUNT; ++i)
    {
        if (rects[i].mask == mask)
        {
            return &rects[i];
        }
    }
    return NULL;
}

static void test_all_rotations(void)
{
    uint16_t x;
    uint16_t y;

    assert(pal_touch_transform(10u, 20u, 0u, NULL, &x, &y));
    assert(x == 10u && y == 20u);
    assert(pal_touch_transform(10u, 20u, 90u, NULL, &x, &y));
    assert(x == 20u && y == 469u);
    assert(pal_touch_transform(10u, 20u, 180u, NULL, &x, &y));
    assert(x == 469u && y == 779u);
    assert(pal_touch_transform(10u, 20u, 270u, NULL, &x, &y));
    assert(x == 779u && y == 10u);
    assert(!pal_touch_transform(480u, 0u, 0u, NULL, &x, &y));
    assert(!pal_touch_transform(0u, 800u, 0u, NULL, &x, &y));
    assert(!pal_touch_transform(0u, 0u, 45u, NULL, &x, &y));
}

static void test_edges_inactive_and_slide(void)
{
    pal_control_rect_t rects[PAL_CONTROL_COUNT];
    const pal_control_rect_t *left;
    const pal_control_rect_t *up;
    pal_touch_point_t point;

    assert(pal_controls_layout(480u, 800u, rects) == PAL_CONTROL_COUNT);
    left = find_rect(rects, PAL_CONTROL_LEFT);
    up = find_rect(rects, PAL_CONTROL_UP);
    assert(left != NULL && up != NULL);

    point.x = left->x;
    point.y = left->y;
    point.id = 0u;
    point.active = 1u;
    assert(pal_touch_controls(&point, 1u, 0u, NULL) == PAL_CONTROL_LEFT);

    point.x = (uint16_t)(left->x + left->width - 1u);
    point.y = (uint16_t)(left->y + left->height - 1u);
    assert(pal_touch_controls(&point, 1u, 0u, NULL) == PAL_CONTROL_LEFT);

    point.x = (uint16_t)(left->x + left->width);
    assert(pal_touch_controls(&point, 1u, 0u, NULL) == 0u);

    point = point_in(left, 0u);
    point.active = 0u;
    assert(pal_touch_controls(&point, 1u, 0u, NULL) == 0u);

    point = point_in(left, 0u);
    assert(pal_touch_controls(&point, 1u, 0u, NULL) == PAL_CONTROL_LEFT);
    point = point_in(up, 0u);
    assert(pal_touch_controls(&point, 1u, 0u, NULL) == PAL_CONTROL_UP);
}

static void test_two_contacts(void)
{
    pal_control_rect_t rects[PAL_CONTROL_COUNT];
    pal_touch_point_t points[2];

    assert(pal_controls_layout(480u, 800u, rects) == PAL_CONTROL_COUNT);
    points[0] = point_in(find_rect(rects, PAL_CONTROL_RIGHT), 1u);
    points[1] = point_in(find_rect(rects, PAL_CONTROL_A), 2u);
    assert(pal_touch_controls(points, 2u, 0u, NULL) ==
           (PAL_CONTROL_RIGHT | PAL_CONTROL_A));
}

static void test_landscape_control_centers(void)
{
    static const uint16_t rotations[] = {90u, 270u};
    pal_control_rect_t rects[PAL_CONTROL_COUNT];
    size_t rotation_index;

    assert(pal_controls_layout(800u, 480u, rects) == PAL_CONTROL_COUNT);
    for (rotation_index = 0u;
         rotation_index < sizeof(rotations) / sizeof(rotations[0]);
         ++rotation_index)
    {
        size_t control_index;

        for (control_index = 0u; control_index < PAL_CONTROL_COUNT;
             ++control_index)
        {
            uint16_t logical_x = (uint16_t)(rects[control_index].x +
                                             rects[control_index].width / 2u);
            uint16_t logical_y = (uint16_t)(rects[control_index].y +
                                             rects[control_index].height / 2u);
            pal_touch_point_t point = physical_point_for_logical(
                logical_x, logical_y, rotations[rotation_index],
                (uint8_t)control_index);

            assert(pal_touch_controls(&point, 1u,
                                      rotations[rotation_index], NULL) ==
                   rects[control_index].mask);
        }
    }
}

int main(void)
{
    test_all_rotations();
    test_edges_inactive_and_slide();
    test_two_contacts();
    test_landscape_control_centers();
    puts("touch_core: PASS");
    return 0;
}
