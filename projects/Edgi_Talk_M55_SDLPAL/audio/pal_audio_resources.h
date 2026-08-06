#ifndef PAL_AUDIO_RESOURCES_H
#define PAL_AUDIO_RESOURCES_H

#include <stddef.h>
#include <stdint.h>

#include "pal_audio_cache.h"
#include "pal_memory_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    PAL_AUDIO_ARCHIVE_MUSIC = 1,
    PAL_AUDIO_ARCHIVE_SOUND = 2
};

typedef struct pal_audio_resources
{
    void *music_file;
    void *sound_file;
} pal_audio_resources_t;

void pal_audio_resources_init(pal_audio_resources_t *resources);
int pal_audio_resources_open(pal_audio_resources_t *resources,
                             const char *root);
void pal_audio_resources_close(pal_audio_resources_t *resources);
int pal_audio_resources_music_available(
    const pal_audio_resources_t *resources);
int pal_audio_resources_sound_available(
    const pal_audio_resources_t *resources);
void pal_audio_resources_cache_init(pal_audio_resources_t *resources,
                                    pal_audio_cache_t *cache,
                                    size_t limit);

#ifdef __cplusplus
}
#endif

#endif
