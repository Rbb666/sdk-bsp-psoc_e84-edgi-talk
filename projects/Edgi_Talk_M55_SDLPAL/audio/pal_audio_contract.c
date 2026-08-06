#include "audio.h"
#include "palcfg.h"

#include "pal_audio_cache.h"
#include "pal_audio_mixer.h"
#include "pal_audio_port.h"
#include "pal_audio_queue.h"
#include "pal_audio_resources.h"
#include "pal_audio_runtime.h"
#include "pal_rix_music.h"

#include <rtthread.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PAL_AUDIO_OUTPUT_RATE 16000u
#define PAL_AUDIO_OUTPUT_CHANNELS 1u
#define PAL_AUDIO_OUTPUT_BITS 16u
#define PAL_AUDIO_OUTPUT_SAMPLES 256u
#define PAL_AUDIO_RELEASE_CAPACITY 32u
#define PAL_AUDIO_STOP_TIMEOUT_MS 250u

#if defined(__GNUC__)
#define PAL_AUDIO_SRAM __attribute__((section(".sdlpal_audio"), aligned(8)))
#else
#define PAL_AUDIO_SRAM
#endif

typedef struct pal_audio_contract_state
{
    pal_audio_resources_t resources;
    pal_audio_cache_t cache;
    pal_audio_queue_t queue;
    pal_audio_runtime_t runtime;
    pal_audio_mixer_t mixer;
    pal_audio_cache_handle_t music_current;
    pal_audio_cache_handle_t music_pending;
    pal_audio_cache_handle_t voice_handles[PAL_AUDIO_MIXER_MAX_VOICES];
    pal_audio_cache_handle_t releases[PAL_AUDIO_RELEASE_CAPACITY];
    int16_t music_block[PAL_AUDIO_OUTPUT_SAMPLES];
    uint32_t desired_generation;
    uint32_t release_overflows;
    int16_t desired_track;
    uint8_t desired_loop;
    uint8_t sound_enabled;
    uint8_t release_head;
    uint8_t release_tail;
    uint8_t release_count;
} pal_audio_contract_state_t;

AUDIODEVICE gAudioDevice PAL_AUDIO_SRAM;
static pal_audio_contract_state_t audio_state PAL_AUDIO_SRAM;

static void handle_invalidate(pal_audio_cache_handle_t *handle)
{
    if (handle != NULL)
    {
        memset(handle, 0, sizeof(*handle));
        handle->slot = PAL_AUDIO_CACHE_INVALID_SLOT;
    }
}

static int handle_valid(const pal_audio_cache_handle_t *handle)
{
    return handle != NULL && handle->data != NULL && handle->size != 0u &&
           handle->slot != PAL_AUDIO_CACHE_INVALID_SLOT;
}

static uintptr_t audio_enter_critical(void *context)
{
    (void)context;
    return (uintptr_t)rt_hw_interrupt_disable();
}

static void audio_exit_critical(void *context, uintptr_t level)
{
    (void)context;
    rt_hw_interrupt_enable((rt_base_t)level);
}

static void defer_release(pal_audio_cache_handle_t *handle)
{
    rt_base_t level;

    if (!handle_valid(handle))
    {
        return;
    }
    level = rt_hw_interrupt_disable();
    if (audio_state.release_count == PAL_AUDIO_RELEASE_CAPACITY)
    {
        ++audio_state.release_overflows;
        rt_hw_interrupt_enable(level);
        return;
    }
    audio_state.releases[audio_state.release_tail] = *handle;
    audio_state.release_tail = (uint8_t)(
        (audio_state.release_tail + 1u) % PAL_AUDIO_RELEASE_CAPACITY);
    ++audio_state.release_count;
    rt_hw_interrupt_enable(level);
    handle_invalidate(handle);
}

static int take_deferred_release(pal_audio_cache_handle_t *handle)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (audio_state.release_count == 0u)
    {
        rt_hw_interrupt_enable(level);
        return 0;
    }
    *handle = audio_state.releases[audio_state.release_head];
    audio_state.release_head = (uint8_t)(
        (audio_state.release_head + 1u) % PAL_AUDIO_RELEASE_CAPACITY);
    --audio_state.release_count;
    rt_hw_interrupt_enable(level);
    return 1;
}

static void drain_deferred_releases(void)
{
    pal_audio_cache_handle_t handle;

    while (take_deferred_release(&handle))
    {
        pal_audio_cache_release(&audio_state.cache, &handle);
    }
}

static uint16_t volume_to_gain(int volume)
{
    if (volume <= 0)
    {
        return 0u;
    }
    if (volume >= SDL_MIX_MAXVOLUME)
    {
        return PAL_AUDIO_COMMAND_GAIN_ONE;
    }
    return (uint16_t)(((uint32_t)volume * PAL_AUDIO_COMMAND_GAIN_ONE) /
                      SDL_MIX_MAXVOLUME);
}

static void update_device_volumes(void)
{
    int music = gConfig.iMusicVolume;
    int sound = gConfig.iSoundVolume;

    if (music < 0)
    {
        music = 0;
    }
    else if (music > PAL_MAX_VOLUME)
    {
        music = PAL_MAX_VOLUME;
    }
    if (sound < 0)
    {
        sound = 0;
    }
    else if (sound > PAL_MAX_VOLUME)
    {
        sound = PAL_MAX_VOLUME;
    }
    gConfig.iMusicVolume = music;
    gConfig.iSoundVolume = sound;
    gAudioDevice.iMusicVolume =
        music * SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
    gAudioDevice.iSoundVolume =
        sound * SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
}

static uint32_t fade_to_half_samples(FLOAT fade_seconds)
{
    double samples;

    if (!(fade_seconds > 0.0f))
    {
        return 0u;
    }
    samples = (double)fade_seconds * PAL_AUDIO_OUTPUT_RATE / 2.0;
    if (samples >= (double)UINT32_MAX)
    {
        return UINT32_MAX;
    }
    return (uint32_t)(samples + 0.5);
}

static void synchronize_music_handles(void)
{
    const void *current = pal_rix_music_current_resource();

    if (handle_valid(&audio_state.music_pending) &&
        current == audio_state.music_pending.data)
    {
        defer_release(&audio_state.music_current);
        audio_state.music_current = audio_state.music_pending;
        handle_invalidate(&audio_state.music_pending);
    }
    if (handle_valid(&audio_state.music_current) &&
        current != audio_state.music_current.data &&
        !handle_valid(&audio_state.music_pending))
    {
        defer_release(&audio_state.music_current);
    }
    if (current == NULL && handle_valid(&audio_state.music_pending))
    {
        defer_release(&audio_state.music_pending);
    }
}

static void apply_music_command(pal_audio_music_command_t *command)
{
    int started;

    if (!pal_audio_runtime_apply_music(&audio_state.runtime, command))
    {
        defer_release(&command->resource);
        return;
    }
    handle_invalidate(&audio_state.runtime.applied_music.resource);
    pal_audio_mixer_set_volume(
        &audio_state.mixer,
        command->enabled ? command->music_gain_q15 : 0u,
        command->sound_enabled ? command->sound_gain_q15 : 0u);
    audio_state.sound_enabled = command->sound_enabled;
    pal_rix_music_enable(command->enabled);

    if (command->track <= 0 || !handle_valid(&command->resource))
    {
        defer_release(&audio_state.music_pending);
        pal_rix_music_stop(command->half_fade_samples);
        if (command->half_fade_samples == 0u)
        {
            defer_release(&audio_state.music_current);
        }
        defer_release(&command->resource);
        return;
    }

    if (handle_valid(&audio_state.music_current) &&
        !handle_valid(&audio_state.music_pending) &&
        audio_state.music_current.data == command->resource.data)
    {
        (void)pal_rix_music_play(
            command->resource.data, command->resource.size,
            command->loop, command->half_fade_samples);
        defer_release(&command->resource);
        return;
    }

    defer_release(&audio_state.music_pending);
    started = pal_rix_music_play(
        command->resource.data, command->resource.size,
        command->loop, command->half_fade_samples);
    if (!started)
    {
        defer_release(&command->resource);
        return;
    }
    if (handle_valid(&audio_state.music_current) &&
        command->half_fade_samples != 0u)
    {
        audio_state.music_pending = command->resource;
    }
    else
    {
        defer_release(&audio_state.music_current);
        audio_state.music_current = command->resource;
    }
    handle_invalidate(&command->resource);
}

static void process_audio_commands(void)
{
    pal_audio_music_command_t music;
    pal_audio_sound_command_t sound;

    if (pal_audio_queue_take_music(&audio_state.queue, &music))
    {
        apply_music_command(&music);
    }
    while (pal_audio_queue_pop_sound(&audio_state.queue, &sound))
    {
        size_t slot;
        int replaced;

        if (!audio_state.sound_enabled ||
            !pal_audio_mixer_start_voc_slot(
                &audio_state.mixer, sound.resource.data,
                sound.resource.size, &slot, &replaced))
        {
            defer_release(&sound.resource);
            continue;
        }
        if (replaced)
        {
            defer_release(&audio_state.voice_handles[slot]);
        }
        audio_state.voice_handles[slot] = sound.resource;
    }
}

static void audio_render(void *context, int16_t *samples,
                         size_t sample_count)
{
    size_t i;

    (void)context;
    process_audio_commands();
    while (sample_count != 0u)
    {
        size_t block = sample_count > PAL_AUDIO_OUTPUT_SAMPLES
                           ? PAL_AUDIO_OUTPUT_SAMPLES
                           : sample_count;
        pal_rix_music_render(audio_state.music_block, block);
        pal_audio_mixer_render(&audio_state.mixer, audio_state.music_block,
                               samples, block);
        samples += block;
        sample_count -= block;
    }
    synchronize_music_handles();
    for (i = 0u; i < PAL_AUDIO_MIXER_MAX_VOICES; ++i)
    {
        if (!audio_state.mixer.voices[i].active)
        {
            defer_release(&audio_state.voice_handles[i]);
        }
    }
}

static void publish_music_state(uint32_t half_fade_samples)
{
    pal_audio_music_command_t command;
    pal_audio_music_command_t replaced;

    if (!gAudioDevice.fOpened)
    {
        return;
    }
    drain_deferred_releases();
    memset(&command, 0, sizeof(command));
    handle_invalidate(&command.resource);
    command.track = audio_state.desired_track;
    command.loop = audio_state.desired_loop;
    command.enabled = gAudioDevice.fMusicEnabled != FALSE;
    command.sound_enabled = gAudioDevice.fSoundEnabled != FALSE;
    command.music_gain_q15 = volume_to_gain(gAudioDevice.iMusicVolume);
    command.sound_gain_q15 = volume_to_gain(gAudioDevice.iSoundVolume);
    command.half_fade_samples = half_fade_samples;
    audio_state.desired_generation = pal_audio_generation_next(
        audio_state.desired_generation);
    command.generation = audio_state.desired_generation;

    if (command.track > 0 &&
        !pal_audio_cache_acquire(
            &audio_state.cache, PAL_AUDIO_ARCHIVE_MUSIC,
            (uint16_t)command.track, &command.resource))
    {
        command.track = 0;
        audio_state.desired_track = 0;
    }
    (void)pal_audio_queue_publish_music(
        &audio_state.queue, &command, &replaced);
    if (handle_valid(&replaced.resource))
    {
        pal_audio_cache_release(&audio_state.cache, &replaced.resource);
    }
}

INT AUDIO_OpenDevice(VOID)
{
    size_t i;

    if (gAudioDevice.fOpened)
    {
        return -1;
    }
    memset(&gAudioDevice, 0, sizeof(gAudioDevice));
    memset(&audio_state, 0, sizeof(audio_state));
    handle_invalidate(&audio_state.music_current);
    handle_invalidate(&audio_state.music_pending);
    for (i = 0u; i < PAL_AUDIO_MIXER_MAX_VOICES; ++i)
    {
        handle_invalidate(&audio_state.voice_handles[i]);
    }
    pal_audio_resources_init(&audio_state.resources);
    (void)pal_audio_resources_open(
        &audio_state.resources,
        gConfig.pszGamePath != NULL ? gConfig.pszGamePath : PAL_PREFIX);
    pal_audio_resources_cache_init(
        &audio_state.resources, &audio_state.cache,
        PAL_AUDIO_CACHE_MAX_BYTES);
    pal_audio_queue_init(&audio_state.queue, audio_enter_critical,
                         audio_exit_critical, NULL);
    pal_audio_runtime_init(&audio_state.runtime);
    pal_audio_mixer_init(&audio_state.mixer, PAL_AUDIO_OUTPUT_RATE);
    pal_rix_music_init(PAL_AUDIO_OUTPUT_RATE);

    gAudioDevice.spec.freq = PAL_AUDIO_OUTPUT_RATE;
    gAudioDevice.spec.format = AUDIO_S16SYS;
    gAudioDevice.spec.channels = PAL_AUDIO_OUTPUT_CHANNELS;
#if !SDL_VERSION_ATLEAST(3, 0, 0)
    gAudioDevice.spec.samples = PAL_AUDIO_OUTPUT_SAMPLES;
#endif
    gAudioDevice.fMusicEnabled = TRUE;
    gAudioDevice.fSoundEnabled = TRUE;
    update_device_volumes();
    audio_state.sound_enabled = 1u;
    pal_audio_mixer_set_volume(
        &audio_state.mixer,
        volume_to_gain(gAudioDevice.iMusicVolume),
        volume_to_gain(gAudioDevice.iSoundVolume));
    gAudioDevice.fOpened = TRUE;
    if (pal_audio_port_start(audio_render, NULL) != 0)
    {
        gAudioDevice.fOpened = FALSE;
        pal_audio_cache_clear(&audio_state.cache);
        pal_audio_resources_close(&audio_state.resources);
        return -3;
    }
    return 0;
}

BOOL AUDIO_CD_Available(VOID)
{
    return FALSE;
}

VOID AUDIO_CloseDevice(VOID)
{
    pal_audio_music_command_t music;
    pal_audio_sound_command_t sound;
    size_t i;

    if (!gAudioDevice.fOpened)
    {
        return;
    }
    if (pal_audio_port_stop(PAL_AUDIO_STOP_TIMEOUT_MS) != 0)
    {
        rt_kprintf("SDLPal audio: sound0 stop timeout\n");
        return;
    }
    while (pal_audio_queue_pop_sound(&audio_state.queue, &sound))
    {
        pal_audio_cache_release(&audio_state.cache, &sound.resource);
    }
    if (pal_audio_queue_take_music(&audio_state.queue, &music))
    {
        pal_audio_cache_release(&audio_state.cache, &music.resource);
    }
    drain_deferred_releases();
    pal_audio_cache_release(&audio_state.cache,
                            &audio_state.music_current);
    pal_audio_cache_release(&audio_state.cache,
                            &audio_state.music_pending);
    for (i = 0u; i < PAL_AUDIO_MIXER_MAX_VOICES; ++i)
    {
        pal_audio_cache_release(&audio_state.cache,
                                &audio_state.voice_handles[i]);
    }
    pal_audio_cache_clear(&audio_state.cache);
    pal_audio_resources_close(&audio_state.resources);
    gAudioDevice.fOpened = FALSE;
}

SDL_AudioSpec *AUDIO_GetDeviceSpec(VOID)
{
    return &gAudioDevice.spec;
}

static void change_volume(int delta)
{
    gConfig.iMusicVolume += delta;
    gConfig.iSoundVolume += delta;
    update_device_volumes();
    publish_music_state(0u);
}

VOID AUDIO_IncreaseVolume(VOID)
{
    change_volume(3);
}

VOID AUDIO_DecreaseVolume(VOID)
{
    change_volume(-3);
}

VOID AUDIO_PlayMusic(INT track, BOOL loop, FLOAT fade_time)
{
    if (track <= 0)
    {
        audio_state.desired_track = 0;
        audio_state.desired_loop = 0u;
    }
    else if (track <= INT16_MAX)
    {
        audio_state.desired_track = (int16_t)track;
        audio_state.desired_loop = loop != FALSE;
    }
    else
    {
        return;
    }
    publish_music_state(fade_to_half_samples(fade_time));
}

BOOL AUDIO_PlayCDTrack(INT track)
{
    (void)track;
    return FALSE;
}

VOID AUDIO_PlaySound(INT sound_number)
{
    pal_audio_sound_command_t command;
    int64_t chunk = sound_number;

    if (!gAudioDevice.fOpened || !gAudioDevice.fSoundEnabled)
    {
        return;
    }
    drain_deferred_releases();
    if (chunk < 0)
    {
        chunk = -chunk;
    }
    if (chunk > UINT16_MAX)
    {
        return;
    }
    memset(&command, 0, sizeof(command));
    handle_invalidate(&command.resource);
    command.chunk = (uint16_t)chunk;
    command.gain_q15 = volume_to_gain(gAudioDevice.iSoundVolume);
    if (!pal_audio_cache_acquire(
            &audio_state.cache, PAL_AUDIO_ARCHIVE_SOUND,
            command.chunk, &command.resource))
    {
        return;
    }
    if (!pal_audio_queue_push_sound(&audio_state.queue, &command))
    {
        pal_audio_cache_release(&audio_state.cache, &command.resource);
    }
}

VOID AUDIO_EnableMusic(BOOL enable)
{
    gAudioDevice.fMusicEnabled = enable != FALSE;
    publish_music_state(0u);
}

BOOL AUDIO_MusicEnabled(VOID)
{
    return gAudioDevice.fMusicEnabled;
}

VOID AUDIO_EnableSound(BOOL enable)
{
    gAudioDevice.fSoundEnabled = enable != FALSE;
    publish_music_state(0u);
}

BOOL AUDIO_SoundEnabled(VOID)
{
    return gAudioDevice.fSoundEnabled;
}

void AUDIO_Lock(void)
{
}

void AUDIO_Unlock(void)
{
}
