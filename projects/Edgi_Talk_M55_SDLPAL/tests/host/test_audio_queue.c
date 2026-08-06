#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pal_audio_queue.h"
#include "pal_audio_runtime.h"

static pal_audio_sound_command_t sound_command(uint16_t chunk)
{
    pal_audio_sound_command_t command;

    memset(&command, 0, sizeof(command));
    command.chunk = chunk;
    command.resource.slot = chunk;
    command.resource.generation = (uint32_t)chunk + 1u;
    return command;
}

static pal_audio_music_command_t music_command(int16_t track,
                                               uint32_t generation)
{
    pal_audio_music_command_t command;

    memset(&command, 0, sizeof(command));
    command.track = track;
    command.loop = 1u;
    command.enabled = 1u;
    command.sound_enabled = 1u;
    command.music_gain_q15 = PAL_AUDIO_COMMAND_GAIN_ONE;
    command.sound_gain_q15 = PAL_AUDIO_COMMAND_GAIN_ONE / 2u;
    command.half_fade_samples = 8000u;
    command.generation = generation;
    command.resource.slot = (uint16_t)track;
    command.resource.generation = generation;
    return command;
}

static void test_sound_fifo_and_drop(void)
{
    pal_audio_queue_t queue;
    pal_audio_queue_metrics_t metrics;
    pal_audio_sound_command_t command;
    unsigned i;

    pal_audio_queue_init(&queue, NULL, NULL, NULL);
    for (i = 0u; i < PAL_AUDIO_SOUND_QUEUE_CAPACITY; ++i)
    {
        command = sound_command((uint16_t)i);
        assert(pal_audio_queue_push_sound(&queue, &command));
    }
    command = sound_command(99u);
    assert(!pal_audio_queue_push_sound(&queue, &command));

    for (i = 0u; i < PAL_AUDIO_SOUND_QUEUE_CAPACITY; ++i)
    {
        memset(&command, 0, sizeof(command));
        assert(pal_audio_queue_pop_sound(&queue, &command));
        assert(command.chunk == i);
    }
    assert(!pal_audio_queue_pop_sound(&queue, &command));
    pal_audio_queue_metrics_get(&queue, &metrics);
    assert(metrics.sound_drops == 1u);
}

static void test_music_snapshot_coalescing(void)
{
    pal_audio_queue_t queue;
    pal_audio_music_command_t first = music_command(1, 1u);
    pal_audio_music_command_t second = music_command(2, 2u);
    pal_audio_music_command_t replaced;
    pal_audio_music_command_t received;
    pal_audio_queue_metrics_t metrics;

    pal_audio_queue_init(&queue, NULL, NULL, NULL);
    memset(&replaced, 0, sizeof(replaced));
    assert(pal_audio_queue_publish_music(&queue, &first, &replaced) ==
           PAL_AUDIO_MUSIC_PUBLISHED);
    assert(replaced.generation == 0u);
    assert(pal_audio_queue_publish_music(&queue, &second, &replaced) ==
           PAL_AUDIO_MUSIC_COALESCED);
    assert(replaced.track == 1);
    assert(replaced.resource.slot == 1u);

    memset(&received, 0, sizeof(received));
    assert(pal_audio_queue_take_music(&queue, &received));
    assert(received.track == 2);
    assert(received.loop == 1u);
    assert(received.enabled == 1u);
    assert(received.sound_enabled == 1u);
    assert(received.music_gain_q15 == PAL_AUDIO_COMMAND_GAIN_ONE);
    assert(received.sound_gain_q15 == PAL_AUDIO_COMMAND_GAIN_ONE / 2u);
    assert(received.half_fade_samples == 8000u);
    assert(received.generation == 2u);
    assert(!pal_audio_queue_take_music(&queue, &received));

    pal_audio_queue_metrics_get(&queue, &metrics);
    assert(metrics.music_coalesces == 1u);
}

static void test_generation_wrap_and_stale_rejection(void)
{
    pal_audio_runtime_t runtime;
    pal_audio_music_command_t command;

    assert(pal_audio_generation_next(0u) == 1u);
    assert(pal_audio_generation_next(UINT32_MAX) == 1u);
    assert(pal_audio_generation_is_newer(1u, UINT32_MAX));
    assert(!pal_audio_generation_is_newer(UINT32_MAX, 1u));

    pal_audio_runtime_init(&runtime);
    command = music_command(7, 10u);
    assert(pal_audio_runtime_apply_music(&runtime, &command));
    assert(runtime.applied_music_generation == 10u);
    assert(!pal_audio_runtime_apply_music(&runtime, &command));
    command.generation = 9u;
    assert(!pal_audio_runtime_apply_music(&runtime, &command));
    assert(runtime.stale_music_commands == 2u);

    runtime.applied_music_generation = UINT32_MAX;
    command.generation = 1u;
    assert(pal_audio_runtime_apply_music(&runtime, &command));
}

int main(void)
{
    test_sound_fifo_and_drop();
    test_music_snapshot_coalescing();
    test_generation_wrap_and_stale_rejection();
    puts("audio_queue: PASS");
    return 0;
}
