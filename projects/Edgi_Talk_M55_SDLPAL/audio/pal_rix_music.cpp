#include "pal_rix_music.h"

#include "pal_mame_opl2_static.h"
#include "third_party/adplug/rix.h"

#include <cstring>

namespace
{

constexpr uint32_t kRixTickRate = 70u;
constexpr size_t kOplTickSamples = 315u;
constexpr uint32_t kOutputRate = 16000u;
constexpr size_t kMaximumOutputTickSamples = 229u;
constexpr uint32_t kFadePhaseOne = UINT32_C(1) << 31;

#if defined(__GNUC__)
#define PAL_AUDIO_SRAM __attribute__((section(".sdlpal_audio"), aligned(8)))
#else
#define PAL_AUDIO_SRAM
#endif

enum class FadeState : uint8_t
{
    None,
    Out,
    In
};

class PalRixOpl final : public Copl
{
public:
    PalRixOpl() : Copl(TYPE_OPL2) {}

    void init() override
    {
        PalMameOpl2_Reset();
    }

    void write(int reg, int value) override
    {
        PalMameOpl2_Write(static_cast<uint8_t>(reg),
                          static_cast<uint8_t>(value));
    }

    void update(short *samples, int frames) override
    {
        if (samples != nullptr && frames > 0)
        {
            PalMameOpl2_Render(reinterpret_cast<int16_t *>(samples),
                               static_cast<size_t>(frames));
        }
    }

    bool getstereo() override
    {
        return false;
    }
};

struct MusicState
{
    const uint8_t *current_data;
    size_t current_size;
    const uint8_t *pending_data;
    size_t pending_size;
    uint32_t output_rate;
    uint32_t tick_phase;
    uint32_t pending_fade_in;
    uint32_t fade_remaining;
    uint32_t fade_step_base;
    uint32_t fade_step_remainder;
    uint32_t fade_step_error;
    uint32_t fade_step_denominator;
    uint32_t fade_phase_q31;
    size_t output_tick_count;
    size_t output_tick_index;
    uint16_t volume_q15;
    bool current_loop;
    bool pending_loop;
    bool playing;
    bool enabled;
    FadeState fade;
    pal_rix_music_metrics_t metrics;
    int16_t opl_tick[kOplTickSamples];
    int16_t output_tick[kMaximumOutputTickSamples];
};

static PalRixOpl music_opl PAL_AUDIO_SRAM;
static CrixPlayer music_decoder PAL_AUDIO_SRAM(&music_opl);
static MusicState state PAL_AUDIO_SRAM;

static void clear_fade()
{
    state.fade = FadeState::None;
    state.fade_remaining = 0u;
    state.fade_step_base = 0u;
    state.fade_step_remainder = 0u;
    state.fade_step_error = 0u;
    state.fade_step_denominator = 0u;
    state.fade_phase_q31 = kFadePhaseOne;
}

static void configure_fade(FadeState fade, uint32_t samples,
                           uint32_t phase)
{
    uint32_t distance = fade == FadeState::Out
                            ? phase
                            : kFadePhaseOne - phase;
    uint32_t remaining;

    state.fade = fade;
    state.fade_phase_q31 = phase;
    state.fade_step_error = 0u;
    if (samples == 0u || distance == 0u)
    {
        if (fade == FadeState::Out)
        {
            state.fade_remaining = 0u;
            state.fade_phase_q31 = 0u;
        }
        else
        {
            clear_fade();
        }
        return;
    }

    remaining = static_cast<uint32_t>(
        (static_cast<uint64_t>(samples) * distance +
         kFadePhaseOne - 1u) >> 31);
    if (remaining == 0u)
    {
        remaining = 1u;
    }
    else if (remaining > samples)
    {
        remaining = samples;
    }
    state.fade_remaining = remaining;
    state.fade_step_base = distance / remaining;
    state.fade_step_remainder = distance % remaining;
    state.fade_step_denominator = remaining;
}

static bool advance_fade()
{
    uint32_t step;

    if (state.fade == FadeState::None || state.fade_remaining == 0u ||
        state.fade_step_denominator == 0u)
    {
        return false;
    }
    step = state.fade_step_base;
    state.fade_step_error += state.fade_step_remainder;
    if (state.fade_step_error >= state.fade_step_denominator)
    {
        state.fade_step_error -= state.fade_step_denominator;
        ++step;
    }

    if (state.fade == FadeState::Out)
    {
        state.fade_phase_q31 = step >= state.fade_phase_q31
                                   ? 0u
                                   : state.fade_phase_q31 - step;
    }
    else
    {
        state.fade_phase_q31 =
            step >= kFadePhaseOne - state.fade_phase_q31
                ? kFadePhaseOne
                : state.fade_phase_q31 + step;
    }
    --state.fade_remaining;
    if (state.fade_remaining != 0u)
    {
        return false;
    }
    if (state.fade == FadeState::Out)
    {
        state.fade_phase_q31 = 0u;
        return true;
    }
    clear_fade();
    return false;
}

static void stop_now()
{
    state.current_data = nullptr;
    state.current_size = 0u;
    state.current_loop = false;
    state.playing = false;
    state.output_tick_count = 0u;
    state.output_tick_index = 0u;
    clear_fade();
    PalMameOpl2_Reset();
}

static bool start_track(const uint8_t *data, size_t size, bool loop,
                        uint32_t fade_in_samples)
{
    if (data == nullptr || size == 0u)
    {
        stop_now();
        return true;
    }
    if (size > UINT32_MAX ||
        !music_decoder.load_buffer(data, static_cast<uint32_t>(size)))
    {
        ++state.metrics.failed_tracks;
        stop_now();
        return false;
    }

    state.current_data = data;
    state.current_size = size;
    state.current_loop = loop;
    state.playing = true;
    state.output_tick_count = 0u;
    state.output_tick_index = 0u;
    if (fade_in_samples == 0u)
    {
        clear_fade();
    }
    else
    {
        configure_fade(FadeState::In, fade_in_samples, 0u);
    }
    return true;
}

static bool start_pending()
{
    const uint8_t *data = state.pending_data;
    size_t size = state.pending_size;
    bool loop = state.pending_loop;
    uint32_t fade_in = state.pending_fade_in;

    state.pending_data = nullptr;
    state.pending_size = 0u;
    state.pending_loop = false;
    state.pending_fade_in = 0u;
    return start_track(data, size, loop, fade_in);
}

static bool render_opl_tick()
{
    if (!state.playing)
    {
        return false;
    }
    if (!music_decoder.update())
    {
        if (!state.current_loop)
        {
            stop_now();
            return false;
        }
        music_decoder.rewindReInit(0, false);
        if (!music_decoder.update())
        {
            ++state.metrics.failed_tracks;
            stop_now();
            return false;
        }
        ++state.metrics.completed_loops;
    }
    PalMameOpl2_Render(state.opl_tick, kOplTickSamples);
    ++state.metrics.rendered_ticks;
    return true;
}

static bool prepare_output_tick()
{
    uint16_t frames;
    uint64_t position = 0u;
    uint64_t step;
    size_t i;

    if (!render_opl_tick())
    {
        return false;
    }
    frames = pal_rix_music_next_tick_frames(&state.tick_phase,
                                             state.output_rate);
    if (frames == 0u || frames > kMaximumOutputTickSamples)
    {
        ++state.metrics.failed_tracks;
        stop_now();
        return false;
    }
    step = (static_cast<uint64_t>(kOplTickSamples - 1u) << 32) /
           (frames - 1u);
    for (i = 0u; i < frames; ++i)
    {
        size_t source = static_cast<size_t>(position >> 32);
        uint32_t fraction = static_cast<uint32_t>(position);
        int32_t first = state.opl_tick[source];
        int32_t second = state.opl_tick[
            source + 1u < kOplTickSamples ? source + 1u : source];
        int64_t delta = static_cast<int64_t>(second - first) * fraction;

        state.output_tick[i] = static_cast<int16_t>(
            first + static_cast<int32_t>(delta / (UINT64_C(1) << 32)));
        position += step;
    }
    state.output_tick_count = frames;
    state.output_tick_index = 0u;
    return true;
}

static int16_t apply_gain(int16_t sample)
{
    uint32_t fade_q15 = state.fade == FadeState::None
                            ? PAL_RIX_MUSIC_GAIN_ONE
                            : state.fade_phase_q31 >> 16;
    uint32_t gain = static_cast<uint32_t>(
        (static_cast<uint64_t>(fade_q15) * state.volume_q15 +
         (1u << 14)) >> 15);

    return static_cast<int16_t>(
        (static_cast<int64_t>(sample) * gain) >> 15);
}

} // namespace

extern "C" uint16_t
pal_rix_music_next_tick_frames(uint32_t *phase, uint32_t output_rate)
{
    uint32_t frames;

    if (phase == nullptr || output_rate == 0u)
    {
        return 0u;
    }
    *phase += output_rate;
    frames = *phase / kRixTickRate;
    *phase %= kRixTickRate;
    return frames <= UINT16_MAX ? static_cast<uint16_t>(frames) : 0u;
}

extern "C" void
pal_rix_music_init(uint32_t output_rate)
{
    std::memset(&state, 0, sizeof(state));
    state.output_rate = output_rate == kOutputRate ? output_rate : kOutputRate;
    state.volume_q15 = PAL_RIX_MUSIC_GAIN_ONE;
    state.enabled = true;
    state.fade_phase_q31 = kFadePhaseOne;
    PalMameOpl2_Init();
}

extern "C" int
pal_rix_music_play(const void *data, size_t size, int loop,
                   uint32_t half_fade_samples)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);

    if (bytes == state.current_data && state.pending_data == nullptr &&
        state.playing)
    {
        state.current_loop = loop != 0;
        return 1;
    }
    if (!state.playing || half_fade_samples == 0u)
    {
        state.pending_data = nullptr;
        state.pending_size = 0u;
        return start_track(bytes, size, loop != 0, half_fade_samples) ? 1 : 0;
    }

    state.pending_data = bytes;
    state.pending_size = size;
    state.pending_loop = loop != 0;
    state.pending_fade_in = half_fade_samples;
    if (state.fade != FadeState::Out)
    {
        uint32_t phase = state.fade == FadeState::None
                             ? kFadePhaseOne
                             : state.fade_phase_q31;
        configure_fade(FadeState::Out, half_fade_samples, phase);
    }
    return 1;
}

extern "C" void
pal_rix_music_stop(uint32_t half_fade_samples)
{
    (void)pal_rix_music_play(nullptr, 0u, 0, half_fade_samples);
}

extern "C" void
pal_rix_music_enable(int enabled)
{
    state.enabled = enabled != 0;
}

extern "C" void
pal_rix_music_set_volume(uint16_t gain_q15)
{
    state.volume_q15 = gain_q15;
}

extern "C" void
pal_rix_music_render(int16_t *output, size_t sample_count)
{
    size_t i;

    if (output == nullptr)
    {
        return;
    }
    for (i = 0u; i < sample_count; ++i)
    {
        int16_t sample = 0;

        if (!state.enabled || state.volume_q15 == 0u)
        {
            output[i] = 0;
            continue;
        }
        if (state.fade == FadeState::Out && state.fade_remaining == 0u)
        {
            (void)start_pending();
        }
        if (state.playing &&
            state.output_tick_index == state.output_tick_count)
        {
            (void)prepare_output_tick();
        }
        if (state.playing &&
            state.output_tick_index < state.output_tick_count)
        {
            sample = state.output_tick[state.output_tick_index++];
        }
        output[i] = apply_gain(sample);
        if (advance_fade())
        {
            (void)start_pending();
        }
    }
}

extern "C" const void *
pal_rix_music_current_resource(void)
{
    return state.current_data;
}

extern "C" void
pal_rix_music_metrics_get(pal_rix_music_metrics_t *metrics)
{
    if (metrics == nullptr)
    {
        return;
    }
    *metrics = state.metrics;
    metrics->fade_gain_q15 = state.fade == FadeState::None
                                 ? PAL_RIX_MUSIC_GAIN_ONE
                                 : static_cast<uint16_t>(
                                       state.fade_phase_q31 >> 16);
    metrics->playing = state.playing ? 1u : 0u;
}
