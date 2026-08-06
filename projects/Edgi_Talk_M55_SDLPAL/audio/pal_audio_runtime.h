#ifndef PAL_AUDIO_RUNTIME_H
#define PAL_AUDIO_RUNTIME_H

#include <stdint.h>

#include "pal_audio_queue.h"

typedef struct pal_audio_runtime
{
    pal_audio_music_command_t applied_music;
    uint32_t applied_music_generation;
    uint32_t stale_music_commands;
} pal_audio_runtime_t;

uint32_t pal_audio_generation_next(uint32_t generation);
int pal_audio_generation_is_newer(uint32_t candidate, uint32_t current);
void pal_audio_runtime_init(pal_audio_runtime_t *runtime);
int pal_audio_runtime_apply_music(
    pal_audio_runtime_t *runtime,
    const pal_audio_music_command_t *command);

#endif
