#ifndef RETRO_GO_DISPLAY_H
#define RETRO_GO_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#define RETRO_GO_LAUNCHER_WIDTH 320u
#define RETRO_GO_LAUNCHER_HEIGHT 240u
#define RETRO_GO_GBA_WIDTH 240u
#define RETRO_GO_GBA_HEIGHT 160u

typedef struct retro_go_display_stats
{
    uint32_t submitted_frames;
    uint32_t completed_frames;
    uint32_t dropped_frames;
    uint32_t failed_frames;
    uint64_t total_present_cycles;
    uint32_t max_present_cycles;
    bool busy;
} retro_go_display_stats_t;

bool retro_go_display_init(void);
uint16_t *retro_go_display_framebuffer(void);
bool retro_go_display_present(const uint16_t *framebuffer);
uint16_t *retro_go_display_launcher_framebuffer(void);
bool retro_go_display_present_launcher(const uint16_t *framebuffer);
void retro_go_display_prepare_game(void);
uint16_t *retro_go_display_gba_framebuffer(void);
bool retro_go_display_present_gba(const uint16_t *framebuffer);
bool retro_go_display_can_accept_gba(void);
bool retro_go_display_gba_busy(void);
bool retro_go_display_wait_gba_idle(uint32_t timeout_ms);
void retro_go_display_get_stats(retro_go_display_stats_t *stats);

#endif
