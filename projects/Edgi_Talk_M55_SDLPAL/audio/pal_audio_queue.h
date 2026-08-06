#ifndef PAL_AUDIO_QUEUE_H
#define PAL_AUDIO_QUEUE_H

#include <stdint.h>

#include "pal_audio_cache.h"

#define PAL_AUDIO_SOUND_QUEUE_CAPACITY 8u
#define PAL_AUDIO_COMMAND_GAIN_ONE 32768u

typedef uintptr_t (*pal_audio_queue_enter_fn)(void *context);
typedef void (*pal_audio_queue_exit_fn)(void *context, uintptr_t level);

typedef struct pal_audio_sound_command
{
    pal_audio_cache_handle_t resource;
    uint16_t chunk;
    uint16_t gain_q15;
} pal_audio_sound_command_t;

typedef struct pal_audio_music_command
{
    pal_audio_cache_handle_t resource;
    uint32_t half_fade_samples;
    uint32_t generation;
    int16_t track;
    uint16_t music_gain_q15;
    uint16_t sound_gain_q15;
    uint8_t loop;
    uint8_t enabled;
} pal_audio_music_command_t;

typedef enum pal_audio_music_publish_result
{
    PAL_AUDIO_MUSIC_PUBLISHED = 1,
    PAL_AUDIO_MUSIC_COALESCED = 2
} pal_audio_music_publish_result_t;

typedef struct pal_audio_queue_metrics
{
    uint32_t sound_drops;
    uint32_t music_coalesces;
} pal_audio_queue_metrics_t;

typedef struct pal_audio_queue
{
    pal_audio_sound_command_t sound[PAL_AUDIO_SOUND_QUEUE_CAPACITY];
    pal_audio_music_command_t music;
    pal_audio_queue_enter_fn enter;
    pal_audio_queue_exit_fn exit;
    void *critical_context;
    uint8_t sound_head;
    uint8_t sound_tail;
    uint8_t sound_count;
    uint8_t music_pending;
    pal_audio_queue_metrics_t metrics;
} pal_audio_queue_t;

void pal_audio_queue_init(pal_audio_queue_t *queue,
                          pal_audio_queue_enter_fn enter,
                          pal_audio_queue_exit_fn exit,
                          void *critical_context);
int pal_audio_queue_push_sound(pal_audio_queue_t *queue,
                               const pal_audio_sound_command_t *command);
int pal_audio_queue_pop_sound(pal_audio_queue_t *queue,
                              pal_audio_sound_command_t *command);
pal_audio_music_publish_result_t pal_audio_queue_publish_music(
    pal_audio_queue_t *queue, const pal_audio_music_command_t *command,
    pal_audio_music_command_t *replaced);
int pal_audio_queue_take_music(pal_audio_queue_t *queue,
                               pal_audio_music_command_t *command);
void pal_audio_queue_metrics_get(pal_audio_queue_t *queue,
                                 pal_audio_queue_metrics_t *metrics);

#endif
