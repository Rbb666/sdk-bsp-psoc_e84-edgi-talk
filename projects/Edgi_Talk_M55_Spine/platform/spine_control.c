/* SPDX-License-Identifier: Apache-2.0 */
#include "spine_control.h"

#include "spine_display.h"
#include "spine_runtime.h"

#include <math.h>
#include <rtthread.h>

#define DASH_SECONDS 0.72f
#define DASH_PIXELS 96.0f
#define HOP_SECONDS 0.60f
#define HOP_PIXELS 108.0f
/* Match the push-off and braking keys in the original C012 dash clip. */
#define AUTHORED_DASH_START 0.14f
#define AUTHORED_DASH_STOP  0.68f
#define AUTHORED_DASH_PIXELS 84.0f

/* The CM55 project is single-core. Hold the interrupt lock only while
 * reading or writing the request, never when touching the Spine runtime. */
static struct
{
    struct spine_position base;
    enum spine_preset preset;
    bool preset_pending;
    bool ready;
} request;

static struct
{
    enum spine_preset preset;
    float elapsed;
    float dash_direction;
    bool authored_dash;
} motion;

static float clamp_pixel(float value, float last_pixel)
{
    if (value < 0.0f)
        return 0.0f;
    return value > last_pixel ? last_pixel : value;
}

static int request_position(float x, float y, bool relative)
{
    if (!isfinite(x) || !isfinite(y))
        return -RT_EINVAL;

    const rt_base_t level = rt_hw_interrupt_disable();
    if (!request.ready)
    {
        rt_hw_interrupt_enable(level);
        return -RT_EBUSY;
    }
    if (relative)
    {
        x += request.base.x;
        y += request.base.y;
    }
    request.base.x = clamp_pixel(x, SPINE_DISPLAY_WIDTH - 1);
    request.base.y = clamp_pixel(y, SPINE_DISPLAY_HEIGHT - 1);
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

int spine_control_set_position(float x, float y)
{
    return request_position(x, y, false);
}

int spine_control_move_by(float dx, float dy)
{
    return request_position(dx, dy, true);
}

int spine_control_center(void)
{
    return spine_control_set_position(SPINE_DISPLAY_WIDTH * 0.5f, SPINE_DISPLAY_HEIGHT * 0.5f);
}

int spine_control_get_position(struct spine_position *position)
{
    if (position == RT_NULL)
        return -RT_EINVAL;

    const rt_base_t level = rt_hw_interrupt_disable();
    const bool ready = request.ready;
    if (ready)
        *position = request.base;
    rt_hw_interrupt_enable(level);
    return ready ? RT_EOK : -RT_EBUSY;
}

int spine_control_play(enum spine_preset preset)
{
    if (preset < SPINE_PRESET_IDLE || preset > SPINE_PRESET_HOP)
        return -RT_EINVAL;

    const rt_base_t level = rt_hw_interrupt_disable();
    const bool ready = request.ready;
    if (ready)
    {
        request.preset = preset;
        request.preset_pending = true;
    }
    rt_hw_interrupt_enable(level);
    return ready ? RT_EOK : -RT_EBUSY;
}

void spine_control_start(void)
{
    motion.preset = SPINE_PRESET_IDLE;
    const rt_base_t level = rt_hw_interrupt_disable();
    request.base = (struct spine_position){SPINE_DISPLAY_WIDTH * 0.5f, SPINE_DISPLAY_HEIGHT * 0.5f};
    request.preset_pending = false;
    request.ready = true;
    rt_hw_interrupt_enable(level);
}

void spine_control_stop(void)
{
    const rt_base_t level = rt_hw_interrupt_disable();
    request.ready = false;
    rt_hw_interrupt_enable(level);
}

static int start_preset(enum spine_preset preset, float x)
{
    motion.preset = preset;
    motion.elapsed = 0.0f;
    motion.authored_dash = false;
    switch (preset)
    {
    case SPINE_PRESET_ACTION:
        return spine_runtime_action();
    case SPINE_PRESET_DASH:
    {
        const int result = spine_runtime_dash();
        motion.dash_direction = x <= SPINE_DISPLAY_WIDTH * 0.5f ? 1.0f : -1.0f;
        if (result == -RT_ENOENT)
            return spine_runtime_run();
        if (result == RT_EOK)
        {
            motion.authored_dash = true;
            spine_runtime_face_left(motion.dash_direction < 0.0f);
        }
        return result;
    }
    case SPINE_PRESET_HOP:
    {
        const int result = spine_runtime_hop();
        if (result == -RT_ENOENT)
            return spine_runtime_idle();
        /* The clip's root timeline supplies the complete vertical motion. */
        motion.preset = SPINE_PRESET_IDLE;
        return result;
    }
    default:
        return spine_runtime_idle();
    }
}

static float authored_dash_distance(float elapsed)
{
    const float progress = clamp_pixel((elapsed - AUTHORED_DASH_START) /
                                      (AUTHORED_DASH_STOP - AUTHORED_DASH_START), 1.0f);
    return AUTHORED_DASH_PIXELS * progress * progress * (3.0f - 2.0f * progress);
}

static int apply_motion(struct spine_position *position, float seconds)
{
    const bool dash = motion.preset == SPINE_PRESET_DASH;
    if (!dash && motion.preset != SPINE_PRESET_HOP)
        return RT_EOK;

    const float previous_elapsed = motion.elapsed;
    motion.elapsed += seconds;
    if (motion.authored_dash)
    {
        const float step = authored_dash_distance(motion.elapsed) - authored_dash_distance(previous_elapsed);
        if (step != 0.0f)
        {
            spine_control_move_by(motion.dash_direction * step, 0.0f);
            spine_control_get_position(position);
        }
        if (motion.elapsed >= AUTHORED_DASH_STOP)
            motion.preset = SPINE_PRESET_IDLE;
        return RT_EOK;
    }

    const float duration = dash ? DASH_SECONDS : HOP_SECONDS;
    if (motion.elapsed >= duration)
        return start_preset(SPINE_PRESET_IDLE, position->x);

    const float progress = motion.elapsed / duration;
    const float arc = 4.0f * progress * (1.0f - progress);
    if (dash)
    {
        const float room = motion.dash_direction > 0.0f ? SPINE_DISPLAY_WIDTH - 1 - position->x : position->x;
        const float distance = room < DASH_PIXELS ? room : DASH_PIXELS;
        position->x += motion.dash_direction * distance * arc;
    }
    else
    {
        const float height = position->y < HOP_PIXELS ? position->y : HOP_PIXELS;
        position->y -= height * arc;
    }
    return RT_EOK;
}

int spine_control_update(float seconds, bool *action_started)
{
    const rt_base_t level = rt_hw_interrupt_disable();
    struct spine_position position = request.base;
    const enum spine_preset preset = request.preset;
    const bool pending = request.preset_pending;
    request.preset_pending = false;
    rt_hw_interrupt_enable(level);

    *action_started = false;
    if (pending)
    {
        const int result = start_preset(preset, position.x);
        if (result != RT_EOK)
            return result;
        *action_started = preset == SPINE_PRESET_ACTION;
    }
    const int result = apply_motion(&position, (!pending || motion.authored_dash) ? seconds : 0.0f);
    if (result != RT_EOK)
        return result;

    spine_runtime_move(position.x, position.y);
    spine_runtime_update(seconds);
    return RT_EOK;
}
