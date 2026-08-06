#include "pal_mame_opl2_static.h"
#include "pal_rix_music.h"
#include "third_party/adplug/rix.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

class TestOpl final : public Copl
{
public:
    TestOpl() : Copl(TYPE_OPL2) {}

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
        PalMameOpl2_Render(reinterpret_cast<int16_t *>(samples),
                           static_cast<size_t>(frames));
    }

    bool getstereo() override
    {
        return false;
    }
};

static void program_voice()
{
    PalMameOpl2_Write(0x01u, 0x20u);
    PalMameOpl2_Write(0x20u, 0x21u);
    PalMameOpl2_Write(0x23u, 0x01u);
    PalMameOpl2_Write(0x40u, 0x28u);
    PalMameOpl2_Write(0x43u, 0x00u);
    PalMameOpl2_Write(0x60u, 0xf3u);
    PalMameOpl2_Write(0x63u, 0xf2u);
    PalMameOpl2_Write(0x80u, 0x55u);
    PalMameOpl2_Write(0x83u, 0x34u);
    PalMameOpl2_Write(0xc0u, 0x04u);
    PalMameOpl2_Write(0xa0u, 0x98u);
    PalMameOpl2_Write(0xb0u, 0x31u);
}

static void test_rix_validation()
{
    uint8_t valid[16] = {0};
    uint8_t invalid[16] = {0};
    TestOpl opl;
    CrixPlayer decoder(&opl);

    valid[0] = 0xaau;
    valid[1] = 0x55u;
    valid[8] = 2u;
    valid[12] = 14u;
    valid[15] = 0x80u;

    assert(!decoder.load_buffer(invalid, sizeof(invalid)));
    assert(decoder.load_buffer(valid, sizeof(valid)));
    assert(!decoder.update());
}

static void test_opl_output_and_reset_determinism()
{
    int16_t first[315] = {0};
    int16_t second[315] = {0};
    size_t nonzero = 0u;

    PalMameOpl2_Init();
    program_voice();
    PalMameOpl2_Render(first, 315u);
    PalMameOpl2_Reset();
    program_voice();
    PalMameOpl2_Render(second, 315u);

    assert(std::memcmp(first, second, sizeof(first)) == 0);
    for (size_t i = 0u; i < 315u; ++i)
    {
        nonzero += first[i] != 0;
    }
    assert(nonzero != 0u);
    assert(PalMameOpl2_StateBytes() > 0u);
    assert(PalMameOpl2_StateBytes() < 8192u);
    assert(PalMameOpl2_TableBytes() == 25706u);
}

static void make_delay_track(uint8_t *track, uint16_t delay)
{
    std::memset(track, 0, 18u);
    track[0] = 0xaau;
    track[1] = 0x55u;
    track[8] = 2u;
    track[12] = 14u;
    track[14] = static_cast<uint8_t>(delay);
    track[15] = static_cast<uint8_t>(delay >> 8);
    track[17] = 0x80u;
}

static void test_exact_output_clock_and_pause()
{
    uint8_t track[18];
    int16_t output[16000];
    pal_rix_music_metrics_t metrics;
    uint32_t phase = 0u;
    uint32_t total = 0u;

    for (unsigned i = 0u; i < 70u; ++i)
    {
        uint16_t frames = pal_rix_music_next_tick_frames(&phase, 16000u);
        assert(frames == 228u || frames == 229u);
        total += frames;
    }
    assert(total == 16000u);

    make_delay_track(track, 0x2000u);
    pal_rix_music_init(16000u);
    assert(pal_rix_music_play(track, sizeof(track), 1, 0u));
    pal_rix_music_render(output, 16000u);
    pal_rix_music_metrics_get(&metrics);
    assert(metrics.rendered_ticks == 70u);
    assert(metrics.playing == 1u);

    pal_rix_music_enable(0);
    pal_rix_music_render(output, 1000u);
    pal_rix_music_metrics_get(&metrics);
    assert(metrics.rendered_ticks == 70u);
    pal_rix_music_enable(1);
}

static void test_loop_and_fade_switch()
{
    uint8_t first[18];
    uint8_t second[18];
    int16_t output[2000];
    pal_rix_music_metrics_t metrics;

    make_delay_track(first, 14u);
    make_delay_track(second, 28u);
    pal_rix_music_init(16000u);
    assert(pal_rix_music_play(first, sizeof(first), 1, 0u));
    pal_rix_music_render(output, 2000u);
    pal_rix_music_metrics_get(&metrics);
    assert(metrics.completed_loops != 0u);

    pal_rix_music_init(16000u);
    assert(pal_rix_music_play(first, sizeof(first), 1, 0u));
    pal_rix_music_render(output, 64u);
    assert(pal_rix_music_current_resource() == first);
    assert(pal_rix_music_play(second, sizeof(second), 1, 10u));
    pal_rix_music_render(output, 9u);
    assert(pal_rix_music_current_resource() == first);
    pal_rix_music_render(output, 1u);
    assert(pal_rix_music_current_resource() == second);
    pal_rix_music_metrics_get(&metrics);
    assert(metrics.fade_gain_q15 == 0u);
    pal_rix_music_render(output, 10u);
    pal_rix_music_metrics_get(&metrics);
    assert(metrics.fade_gain_q15 == PAL_RIX_MUSIC_GAIN_ONE);
}

int main()
{
    test_rix_validation();
    test_opl_output_and_reset_determinism();
    test_exact_output_clock_and_pause();
    test_loop_and_fade_switch();
    std::puts("rix_output: PASS");
    return 0;
}
