#ifndef RETRO_GO_PERF_H
#define RETRO_GO_PERF_H

#include <stdbool.h>
#include <stdint.h>

#define RETRO_GO_PERF_PANEL_WIDTH 80u
#define RETRO_GO_PERF_PANEL_HEIGHT 320u

void retro_go_perf_begin(const char *system_name, uint32_t target_frame_us);
void retro_go_perf_frame(bool drawn, uint32_t work_cycles);
void retro_go_perf_end(void);
void retro_go_perf_set_vglite(bool active);
bool retro_go_perf_render_due(uint16_t *framebuffer, uint16_t width,
                              uint16_t height, uint16_t stride);

#endif
