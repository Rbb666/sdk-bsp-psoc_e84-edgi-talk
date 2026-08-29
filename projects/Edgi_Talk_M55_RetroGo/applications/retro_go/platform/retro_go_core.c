#include "retro_go_core.h"

#include "cpu.h"
#include "hw.h"
#include "sound.h"

#define RETRO_GO_GB_LINES_PER_FRAME 154
#define RETRO_GO_GB_CYCLES_PER_LINE 228
#define RETRO_GO_LCD_LOOP_GUARD 200u

static void submit_audio(void)
{
    if (GB.audio.callback != NULL && GB.audio.pos > 0u)
    {
        GB.audio.callback(GB.audio.buffer, GB.audio.pos);
    }
}

bool retro_go_core_run_frame(bool draw)
{
    int cycles = 0;
    unsigned iterations = 0u;

    GB.video.enabled = draw;
    GB.audio.pos = 0u;

    if ((R_LCDC & 0x80u) == 0u)
    {
        cycles = RETRO_GO_GB_LINES_PER_FRAME * RETRO_GO_GB_CYCLES_PER_LINE;
        (void)gb_cpu_emulate(cycles);
        gb_sound_emulate();
        submit_audio();
        return false;
    }

    while (R_LY <= 144u && iterations++ < RETRO_GO_LCD_LOOP_GUARD)
    {
        cycles += RETRO_GO_GB_CYCLES_PER_LINE;
        cycles -= gb_cpu_emulate(cycles);

        /* Some games disable LCDC while initializing VRAM. In that state LY
         * is held at zero by hardware, so the upstream `while (LY <= 144)`
         * frame loop cannot terminate unless the host handles this edge. */
        if ((R_LCDC & 0x80u) == 0u)
        {
            gb_sound_emulate();
            submit_audio();
            return false;
        }
    }

    if (iterations >= RETRO_GO_LCD_LOOP_GUARD)
    {
        gb_sound_emulate();
        submit_audio();
        return false;
    }

    if (draw && GB.video.callback != NULL)
    {
        GB.video.callback(GB.video.buffer);
    }

    gb_hw_vblank();
    iterations = 0u;
    while (R_LY > 0u && iterations++ < RETRO_GO_LCD_LOOP_GUARD)
    {
        cycles += RETRO_GO_GB_CYCLES_PER_LINE;
        cycles -= gb_cpu_emulate(cycles);
        if ((R_LCDC & 0x80u) == 0u)
        {
            break;
        }
    }

    submit_audio();
    return true;
}
