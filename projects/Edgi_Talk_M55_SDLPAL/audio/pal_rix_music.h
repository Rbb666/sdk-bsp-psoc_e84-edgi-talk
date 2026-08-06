#ifndef PAL_RIX_MUSIC_H
#define PAL_RIX_MUSIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_RIX_MUSIC_GAIN_ONE 32768u

typedef struct pal_rix_music_metrics
{
    uint32_t rendered_ticks;
    uint32_t completed_loops;
    uint32_t failed_tracks;
    uint16_t fade_gain_q15;
    uint8_t playing;
} pal_rix_music_metrics_t;

uint16_t pal_rix_music_next_tick_frames(uint32_t *phase,
                                        uint32_t output_rate);
void pal_rix_music_init(uint32_t output_rate);
int pal_rix_music_play(const void *data, size_t size, int loop,
                       uint32_t half_fade_samples);
void pal_rix_music_stop(uint32_t half_fade_samples);
void pal_rix_music_enable(int enabled);
void pal_rix_music_set_volume(uint16_t gain_q15);
void pal_rix_music_render(int16_t *output, size_t sample_count);
const void *pal_rix_music_current_resource(void);
void pal_rix_music_metrics_get(pal_rix_music_metrics_t *metrics);

#ifdef __cplusplus
}
#endif

#endif
