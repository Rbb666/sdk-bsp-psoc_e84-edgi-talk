/* SPDX-License-Identifier: Apache-2.0 */
#include <finsh.h>
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include "spine_control.h"
#include "spine_display.h"
#include "spine_gpu.h"
#include "spine_runtime.h"

#define FRAME_PERIOD_MS 16U
#define BACKGROUND_ABGR 0xff2e2017u

static struct demo_status
{
    const char *stage;
    rt_uint32_t frames;
    rt_uint32_t fps;
    rt_uint32_t touches;
    rt_uint32_t actions;
    rt_uint32_t heap_peak;
    int touch_status;
    int error;
} status = {.stage = "starting"};

static struct rt_thread render_thread;

/* GPU command construction frequently touches stack-local matrices and paths. */
__attribute__((section(".spine_dtcm.render_stack"), aligned(RT_ALIGN_SIZE)))
static rt_uint8_t render_stack[RT_MAIN_THREAD_STACK_SIZE];

static void spine_status(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    struct demo_status snapshot = status;
    rt_hw_interrupt_enable(level);

    rt_kprintf("spine: %s, model=%s, frames=%u, fps=%u, touch=%d, taps=%u, actions=%u, dtcm_peak=%u, error=%d\n",
               snapshot.stage, spine_runtime_model_name(), snapshot.frames, snapshot.fps, snapshot.touch_status,
               snapshot.touches, snapshot.actions, snapshot.heap_peak, snapshot.error);
    struct spine_position position;
    if (spine_control_get_position(&position) == RT_EOK)
        rt_kprintf("spine: base anchor=(%d,%d) px\n", (int)position.x, (int)position.y);
}
MSH_CMD_EXPORT(spine_status, Show Spine rendering and touch status);

static int report_request(int result)
{
    if (result != RT_EOK)
        rt_kprintf("spine: request failed (%d)\n", result);
    return result;
}

static int spine_action(void)
{
    return report_request(spine_control_play(SPINE_PRESET_ACTION));
}
MSH_CMD_EXPORT(spine_action, Play the model action on the render thread);

static bool parse_pixels(const char *text, float *pixels)
{
    char *end;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0')
        return false;
    *pixels = (float)value;
    return true;
}

static int spine_move(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "center") == 0)
        return report_request(spine_control_center());

    float x, y;
    if (argc == 3 && parse_pixels(argv[1], &x) && parse_pixels(argv[2], &y))
        return report_request(spine_control_set_position(x, y));
    if (argc == 4 && strcmp(argv[1], "by") == 0 && parse_pixels(argv[2], &x) && parse_pixels(argv[3], &y))
        return report_request(spine_control_move_by(x, y));

    rt_kprintf("usage: spine_move <x> <y> | spine_move by <dx> <dy> | spine_move center\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(spine_move, Move the model anchor in LCD pixels);

static int spine_demo(int argc, char **argv)
{
    static const struct
    {
        const char *name;
        enum spine_preset preset;
    } presets[] = {
        {"idle", SPINE_PRESET_IDLE},
        {"action", SPINE_PRESET_ACTION},
        {"dash", SPINE_PRESET_DASH},
        {"hop", SPINE_PRESET_HOP},
    };

    if (argc == 2)
    {
        for (unsigned i = 0; i < sizeof(presets) / sizeof(presets[0]); ++i)
        {
            if (strcmp(argv[1], presets[i].name) == 0)
                return report_request(spine_control_play(presets[i].preset));
        }
    }

    rt_kprintf("usage: spine_demo <idle|action|dash|hop>\n");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(spine_demo, Play a Spine movement preset);

static int demo_fail(const char *stage, int error)
{
    spine_control_stop();
    status.error = error;
    status.stage = stage;
    spine_status();
    return error;
}

static int prepare_pose(rt_tick_t *previous, rt_bool_t *was_pressed)
{
    const rt_tick_t now = rt_tick_get();
    const rt_tick_t dt = now - *previous;
    *previous = now;

    if (status.touch_status == RT_EOK)
    {
        struct spine_touch touch = {0};
        spine_touch_read(&touch);
        if (touch.pressed)
        {
            spine_control_set_position(touch.x, touch.y);
            if (!*was_pressed)
            {
                spine_control_play(SPINE_PRESET_ACTION);
                ++status.touches;
            }
        }
        *was_pressed = touch.pressed;
    }

    bool action_started;
    const int result = spine_control_update(dt / (float)RT_TICK_PER_SECOND, &action_started);
    if (action_started)
        ++status.actions;
    return result;
}

static int render_loop(void)
{
    status.stage = "display init";
    int result = spine_display_init();
    if (result)
        return demo_fail("display init failed", result);

    status.stage = "runtime init";
    result = spine_runtime_init(SPINE_DISPLAY_WIDTH, SPINE_DISPLAY_HEIGHT);
    if (result)
        return demo_fail("runtime init failed", result);

    status.touch_status = spine_touch_init();
    spine_control_start();
    status.heap_peak = spine_runtime_heap_peak();
    status.stage = "running";
    spine_status();

    rt_tick_t previous = rt_tick_get();
    rt_tick_t sample_start = previous;
    rt_uint32_t sample_frames = 0;
    rt_bool_t was_pressed = RT_FALSE;
    const rt_tick_t frame_ticks = rt_tick_from_millisecond(FRAME_PERIOD_MS);

    result = prepare_pose(&previous, &was_pressed);
    if (result)
        return demo_fail("pose update failed", result);

    for (;;)
    {
        const rt_tick_t frame_start = rt_tick_get();
        result = spine_gpu_begin(spine_display_back_buffer(), BACKGROUND_ABGR);
        if (!result)
            result = spine_runtime_draw();
        if (result)
            return demo_fail("GPU draw failed", result);

        result = spine_display_submit();
        if (result)
            return demo_fail("GPU submit failed", result);

        /* The GPU reads copied commands and the atlas while the CPU
         * updates the next pose. Flip only after the GPU has finished. */
        result = prepare_pose(&previous, &was_pressed);
        if (result)
            return demo_fail("pose update failed", result);

        result = spine_display_wait_gpu();
        if (result)
            return demo_fail("GPU wait failed", result);
        result = spine_display_flip();
        if (result)
            return demo_fail("display present failed", result);

        ++status.frames;
        ++sample_frames;
        const rt_tick_t finished = rt_tick_get();
        const rt_tick_t elapsed = finished - sample_start;
        if (elapsed >= RT_TICK_PER_SECOND)
        {
            status.fps = (sample_frames * RT_TICK_PER_SECOND + elapsed / 2u) / elapsed;
            status.heap_peak = spine_runtime_heap_peak();
            sample_start = finished;
            sample_frames = 0;
        }

        if (finished - frame_start < frame_ticks)
            rt_thread_delay(frame_ticks - (finished - frame_start));
    }
}

static void render_entry(void *parameter)
{
    (void)parameter;
    (void)render_loop();
}

int main(void)
{
    rt_err_t result = rt_thread_init(&render_thread, "spiner", render_entry, RT_NULL,
                                    render_stack, sizeof(render_stack),
                                    RT_MAIN_THREAD_PRIORITY, 20);
    if (result == RT_EOK)
        result = rt_thread_startup(&render_thread);
    return result == RT_EOK ? RT_EOK : demo_fail("render thread init failed", result);
}
