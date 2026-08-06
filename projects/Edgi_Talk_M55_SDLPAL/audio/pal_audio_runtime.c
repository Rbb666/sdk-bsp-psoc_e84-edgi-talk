#include "pal_audio_runtime.h"

#include <string.h>

uint32_t pal_audio_generation_next(uint32_t generation)
{
    ++generation;
    return generation != 0u ? generation : 1u;
}

int pal_audio_generation_is_newer(uint32_t candidate, uint32_t current)
{
    if (candidate == 0u)
    {
        return 0;
    }
    if (current == 0u)
    {
        return 1;
    }
    return (int32_t)(candidate - current) > 0;
}

void pal_audio_runtime_init(pal_audio_runtime_t *runtime)
{
    if (runtime != NULL)
    {
        memset(runtime, 0, sizeof(*runtime));
    }
}

int pal_audio_runtime_apply_music(
    pal_audio_runtime_t *runtime,
    const pal_audio_music_command_t *command)
{
    if (runtime == NULL || command == NULL)
    {
        return 0;
    }
    if (!pal_audio_generation_is_newer(
            command->generation, runtime->applied_music_generation))
    {
        ++runtime->stale_music_commands;
        return 0;
    }
    runtime->applied_music = *command;
    runtime->applied_music_generation = command->generation;
    return 1;
}
