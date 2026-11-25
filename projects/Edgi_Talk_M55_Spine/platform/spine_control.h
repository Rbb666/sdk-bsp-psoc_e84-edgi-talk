/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SPINE_CONTROL_H
#define SPINE_CONTROL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spine_position
{
    float x;
    float y;
};

enum spine_preset
{
    SPINE_PRESET_IDLE,
    SPINE_PRESET_ACTION,
    SPINE_PRESET_DASH,
    SPINE_PRESET_HOP,
};

/* Call from application threads. Position is the model's reference-pose anchor
 * in LCD pixels (origin at top left). The base anchor is clamped to the panel.
 * A relative move adds to the latest requested base, including unrendered requests.
 * Requests return -RT_EBUSY outside the render loop's lifetime. */
int spine_control_set_position(float x, float y);
int spine_control_move_by(float dx, float dy);
int spine_control_center(void);
int spine_control_get_position(struct spine_position *position);
/* Only the latest preset request since the last frame is played. */
int spine_control_play(enum spine_preset preset);

/* Render-thread only. Start after spine_runtime_init(). */
void spine_control_start(void);
void spine_control_stop(void);
int spine_control_update(float seconds, bool *action_started);

#ifdef __cplusplus
}
#endif

#endif
