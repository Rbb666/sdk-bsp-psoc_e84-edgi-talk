#include "pal_audio_mixer.h"

#include <limits.h>
#include <string.h>

static int32_t apply_gain(int16_t sample, uint16_t gain_q15)
{
    return (int32_t)(((int64_t)sample * gain_q15) / PAL_AUDIO_GAIN_ONE);
}

static int16_t saturate_pcm16(int32_t sample)
{
    if (sample > INT16_MAX)
    {
        return INT16_MAX;
    }
    if (sample < INT16_MIN)
    {
        return INT16_MIN;
    }
    return (int16_t)sample;
}

size_t pal_audio_mixer_active_voices(const pal_audio_mixer_t *mixer)
{
    size_t active = 0u;
    size_t i;

    if (mixer == NULL)
    {
        return 0u;
    }
    for (i = 0u; i < PAL_AUDIO_MIXER_MAX_VOICES; ++i)
    {
        if (mixer->voices[i].active)
        {
            ++active;
        }
    }
    return active;
}

void pal_audio_mixer_init(pal_audio_mixer_t *mixer, uint32_t output_rate)
{
    if (mixer == NULL)
    {
        return;
    }
    memset(mixer, 0, sizeof(*mixer));
    mixer->output_rate = output_rate;
    mixer->music_gain_q15 = PAL_AUDIO_GAIN_ONE;
    mixer->sound_gain_q15 = PAL_AUDIO_GAIN_ONE;
}

void pal_audio_mixer_set_volume(pal_audio_mixer_t *mixer,
                                uint16_t music_gain_q15,
                                uint16_t sound_gain_q15)
{
    if (mixer == NULL)
    {
        return;
    }
    mixer->music_gain_q15 = music_gain_q15;
    mixer->sound_gain_q15 = sound_gain_q15;
}

int pal_audio_mixer_start_voc(pal_audio_mixer_t *mixer,
                              const void *data, size_t size)
{
    pal_voc_view_t view;
    size_t selected = PAL_AUDIO_MIXER_MAX_VOICES;
    size_t i;

    if (mixer == NULL || mixer->output_rate == 0u ||
        !pal_voc_open(&view, data, size))
    {
        if (mixer != NULL)
        {
            ++mixer->metrics.rejected_voices;
        }
        return 0;
    }

    for (i = 0u; i < PAL_AUDIO_MIXER_MAX_VOICES; ++i)
    {
        if (!mixer->voices[i].active)
        {
            selected = i;
            break;
        }
    }
    if (selected == PAL_AUDIO_MIXER_MAX_VOICES)
    {
        selected = 0u;
        for (i = 1u; i < PAL_AUDIO_MIXER_MAX_VOICES; ++i)
        {
            if (mixer->voices[i].serial < mixer->voices[selected].serial)
            {
                selected = i;
            }
        }
        ++mixer->metrics.replaced_voices;
    }

    if (!pal_voc_stream_start(&mixer->voices[selected].stream, &view,
                              mixer->output_rate))
    {
        ++mixer->metrics.rejected_voices;
        return 0;
    }
    ++mixer->serial;
    if (mixer->serial == 0u)
    {
        mixer->serial = 1u;
    }
    mixer->voices[selected].serial = mixer->serial;
    mixer->voices[selected].active = 1u;

    i = pal_audio_mixer_active_voices(mixer);
    if (i > mixer->metrics.peak_voices)
    {
        mixer->metrics.peak_voices = (uint32_t)i;
    }
    return 1;
}

void pal_audio_mixer_render(pal_audio_mixer_t *mixer,
                            const int16_t *music, int16_t *output,
                            size_t sample_count)
{
    size_t sample_index;

    if (mixer == NULL || output == NULL)
    {
        return;
    }
    for (sample_index = 0u; sample_index < sample_count; ++sample_index)
    {
        int32_t mixed = music != NULL
                            ? apply_gain(music[sample_index],
                                         mixer->music_gain_q15)
                            : 0;
        size_t voice_index;

        for (voice_index = 0u;
             voice_index < PAL_AUDIO_MIXER_MAX_VOICES;
             ++voice_index)
        {
            pal_audio_voice_t *voice = &mixer->voices[voice_index];
            int16_t voice_sample = 0;

            if (!voice->active)
            {
                continue;
            }
            (void)pal_voc_stream_render(&voice->stream, &voice_sample, 1u);
            mixed += apply_gain(voice_sample, mixer->sound_gain_q15);
            if (!pal_voc_stream_active(&voice->stream))
            {
                voice->active = 0u;
            }
        }
        output[sample_index] = saturate_pcm16(mixed);
    }
}

void pal_audio_mixer_metrics_get(const pal_audio_mixer_t *mixer,
                                 pal_audio_mixer_metrics_t *metrics)
{
    if (metrics == NULL)
    {
        return;
    }
    if (mixer == NULL)
    {
        memset(metrics, 0, sizeof(*metrics));
        return;
    }
    *metrics = mixer->metrics;
}
