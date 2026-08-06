#ifndef PAL_TOUCH_PORT_H
#define PAL_TOUCH_PORT_H

#include <stdbool.h>
#include <stddef.h>

#include "pal_touch_core.h"

bool pal_touch_port_init(void);
bool pal_touch_port_poll(pal_touch_point_t *points, size_t capacity,
                         size_t *count);

#endif
