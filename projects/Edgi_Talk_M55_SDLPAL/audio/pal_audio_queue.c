#include "pal_audio_queue.h"

#include <string.h>

static uintptr_t enter_critical(pal_audio_queue_t *queue)
{
    return queue->enter != NULL
               ? queue->enter(queue->critical_context)
               : (uintptr_t)0u;
}

static void exit_critical(pal_audio_queue_t *queue, uintptr_t level)
{
    if (queue->exit != NULL)
    {
        queue->exit(queue->critical_context, level);
    }
}

void pal_audio_queue_init(pal_audio_queue_t *queue,
                          pal_audio_queue_enter_fn enter,
                          pal_audio_queue_exit_fn exit,
                          void *critical_context)
{
    if (queue == NULL)
    {
        return;
    }
    memset(queue, 0, sizeof(*queue));
    queue->enter = enter;
    queue->exit = exit;
    queue->critical_context = critical_context;
}

int pal_audio_queue_push_sound(pal_audio_queue_t *queue,
                               const pal_audio_sound_command_t *command)
{
    uintptr_t level;

    if (queue == NULL || command == NULL)
    {
        return 0;
    }
    level = enter_critical(queue);
    if (queue->sound_count == PAL_AUDIO_SOUND_QUEUE_CAPACITY)
    {
        ++queue->metrics.sound_drops;
        exit_critical(queue, level);
        return 0;
    }
    queue->sound[queue->sound_tail] = *command;
    queue->sound_tail = (uint8_t)(
        (queue->sound_tail + 1u) % PAL_AUDIO_SOUND_QUEUE_CAPACITY);
    ++queue->sound_count;
    exit_critical(queue, level);
    return 1;
}

int pal_audio_queue_pop_sound(pal_audio_queue_t *queue,
                              pal_audio_sound_command_t *command)
{
    uintptr_t level;

    if (queue == NULL || command == NULL)
    {
        return 0;
    }
    level = enter_critical(queue);
    if (queue->sound_count == 0u)
    {
        exit_critical(queue, level);
        return 0;
    }
    *command = queue->sound[queue->sound_head];
    queue->sound_head = (uint8_t)(
        (queue->sound_head + 1u) % PAL_AUDIO_SOUND_QUEUE_CAPACITY);
    --queue->sound_count;
    exit_critical(queue, level);
    return 1;
}

pal_audio_music_publish_result_t pal_audio_queue_publish_music(
    pal_audio_queue_t *queue, const pal_audio_music_command_t *command,
    pal_audio_music_command_t *replaced)
{
    pal_audio_music_publish_result_t result = PAL_AUDIO_MUSIC_PUBLISHED;
    uintptr_t level;

    if (replaced != NULL)
    {
        memset(replaced, 0, sizeof(*replaced));
        replaced->resource.slot = PAL_AUDIO_CACHE_INVALID_SLOT;
    }
    if (queue == NULL || command == NULL)
    {
        return result;
    }

    level = enter_critical(queue);
    if (queue->music_pending)
    {
        if (replaced != NULL)
        {
            *replaced = queue->music;
        }
        ++queue->metrics.music_coalesces;
        result = PAL_AUDIO_MUSIC_COALESCED;
    }
    queue->music = *command;
    queue->music_pending = 1u;
    exit_critical(queue, level);
    return result;
}

int pal_audio_queue_take_music(pal_audio_queue_t *queue,
                               pal_audio_music_command_t *command)
{
    uintptr_t level;

    if (queue == NULL || command == NULL)
    {
        return 0;
    }
    level = enter_critical(queue);
    if (!queue->music_pending)
    {
        exit_critical(queue, level);
        return 0;
    }
    *command = queue->music;
    queue->music_pending = 0u;
    exit_critical(queue, level);
    return 1;
}

void pal_audio_queue_metrics_get(pal_audio_queue_t *queue,
                                 pal_audio_queue_metrics_t *metrics)
{
    uintptr_t level;

    if (metrics == NULL)
    {
        return;
    }
    if (queue == NULL)
    {
        memset(metrics, 0, sizeof(*metrics));
        return;
    }
    level = enter_critical(queue);
    *metrics = queue->metrics;
    exit_critical(queue, level);
}
