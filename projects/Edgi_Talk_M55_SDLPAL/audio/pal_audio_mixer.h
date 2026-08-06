#ifndef PAL_AUDIO_MIXER_H
#define PAL_AUDIO_MIXER_H

#include <stddef.h>
#include <stdint.h>

#include "pal_voc.h"

#define PAL_AUDIO_MIXER_MAX_VOICES 4u
#define PAL_AUDIO_GAIN_ONE 32768u

typedef struct pal_audio_voice
{
    pal_voc_stream_t stream;
    uint32_t serial;
    uint8_t active;
} pal_audio_voice_t;

typedef struct pal_audio_mixer_metrics
{
    uint32_t peak_voices;
    uint32_t replaced_voices;
    uint32_t rejected_voices;
} pal_audio_mixer_metrics_t;

typedef struct pal_audio_mixer
{
    pal_audio_voice_t voices[PAL_AUDIO_MIXER_MAX_VOICES];
    uint32_t output_rate;
    uint32_t serial;
    uint16_t music_gain_q15;
    uint16_t sound_gain_q15;
    pal_audio_mixer_metrics_t metrics;
} pal_audio_mixer_t;

void pal_audio_mixer_init(pal_audio_mixer_t *mixer, uint32_t output_rate);
void pal_audio_mixer_set_volume(pal_audio_mixer_t *mixer,
                                uint16_t music_gain_q15,
                                uint16_t sound_gain_q15);
int pal_audio_mixer_start_voc(pal_audio_mixer_t *mixer,
                              const void *data, size_t size);
int pal_audio_mixer_start_voc_slot(pal_audio_mixer_t *mixer,
                                   const void *data, size_t size,
                                   size_t *voice_slot, int *replaced);
void pal_audio_mixer_render(pal_audio_mixer_t *mixer,
                            const int16_t *music, int16_t *output,
                            size_t sample_count);
size_t pal_audio_mixer_active_voices(const pal_audio_mixer_t *mixer);
void pal_audio_mixer_metrics_get(const pal_audio_mixer_t *mixer,
                                 pal_audio_mixer_metrics_t *metrics);

#endif
