/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SPINE_RUNTIME_H
#define SPINE_RUNTIME_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Use from the thread that owns rendering. Positions use LCD pixel coordinates. */
int spine_runtime_init(unsigned width, unsigned height);
void spine_runtime_deinit(void);
/* Set the absolute reference-pose anchor. The control API bounds user input. */
void spine_runtime_move(float x, float y);
void spine_runtime_face_left(bool left);
int spine_runtime_play(const char *name, bool loop);
int spine_runtime_idle(void);
int spine_runtime_action(void);
int spine_runtime_run(void);
int spine_runtime_dash(void);
int spine_runtime_hop(void);
const char *spine_runtime_model_name(void);
void spine_runtime_update(float seconds);
int spine_runtime_draw(void);
unsigned spine_runtime_heap_peak(void);

#ifdef __cplusplus
}
#endif

#endif
