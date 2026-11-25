/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SPINE_DISPLAY_H
#define SPINE_DISPLAY_H

#include <rtthread.h>
#include "vg_lite.h"

#define SPINE_DISPLAY_WIDTH  480
#define SPINE_DISPLAY_HEIGHT 800

rt_err_t spine_display_init(void);

/* The render thread owns this buffer until it queues a flip. */
vg_lite_buffer_t *spine_display_back_buffer(void);

/* Submit the queued VG-Lite commands while the render thread does CPU-only work. */
rt_err_t spine_display_submit(void);

/* Finish GPU rendering with or without a preceding asynchronous submission. */
rt_err_t spine_display_wait_gpu(void);

/* Queue the finished frame for LCD scanout and wait for the old buffer's release. */
rt_err_t spine_display_flip(void);

struct spine_touch
{
    rt_uint16_t x;
    rt_uint16_t y;
    rt_bool_t pressed;
};

rt_err_t spine_touch_init(void);
void spine_touch_read(struct spine_touch *touch);

#endif
