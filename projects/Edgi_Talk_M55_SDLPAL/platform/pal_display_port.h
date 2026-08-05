#ifndef PAL_DISPLAY_PORT_H
#define PAL_DISPLAY_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pal_display_core.h"

#define PAL_DISPLAY_STRIP_WIDTH 480u
#define PAL_DISPLAY_STRIP_ROWS 16u

typedef struct pal_display_metrics
{
    uint32_t frame_count;
    uint32_t vglite_frame_count;
    uint32_t fallback_frame_count;
    uint32_t last_microseconds;
    uint32_t max_microseconds;
    uint32_t control_update_count;
    uint32_t control_last_microseconds;
    uint32_t control_max_microseconds;
} pal_display_metrics_t;

bool pal_display_present_indexed(const uint8_t *pixels, size_t pitch,
                                 const pal_rgb_t palette[256]);
void pal_display_controls_set(uint32_t pressed_mask);
void pal_display_metrics_get(pal_display_metrics_t *metrics);
void pal_display_port_get_dimensions(uint16_t *width, uint16_t *height);
uint16_t *pal_display_port_work_buffer(size_t *capacity_pixels);
bool pal_display_port_flush_work_area(uint16_t x, uint16_t y,
                                      uint16_t width, uint16_t height,
                                      size_t stride_pixels, bool present);

#endif
