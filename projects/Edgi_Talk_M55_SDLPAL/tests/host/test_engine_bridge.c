#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <rtthread.h>

#include "pal_engine_bridge.h"
#include "pal_display_port.h"
#include "pal_input_port.h"

static uint32_t fake_input_mask;
static unsigned present_count;
static const uint8_t *present_pixels;
static size_t present_pitch;
static pal_rgb_t present_palette[256];

bool pal_display_present_indexed(const uint8_t *pixels, size_t pitch,
                                 const pal_rgb_t palette[256])
{
    present_pixels = pixels;
    present_pitch = pitch;
    memcpy(present_palette, palette, sizeof(present_palette));
    ++present_count;
    return true;
}

uint32_t pal_input_port_poll(void)
{
    return fake_input_mask;
}

int rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}

void pal_memory_report(const char *stage)
{
    (void)stage;
}

static void expect_key(Uint32 type, int key)
{
    SDL_Event event;

    memset(&event, 0, sizeof(event));
    assert(PalEngineBridge_PollEvent(&event) == 1);
    assert(event.type == type);
    assert(event.key.keysym.sym == key);
}

int main(void)
{
    uint8_t pixels[320u * 200u] = {0u};
    SDL_Color palette[256] = {{0u, 0u, 0u, 255u}};
    SDL_Event event;

    palette[7].r = 11u;
    palette[7].g = 22u;
    palette[7].b = 33u;
    PalEngineBridge_RenderPresentIndexed(pixels, 320, 320, 200, palette);
    assert(present_count == 1u);
    assert(present_pixels == pixels);
    assert(present_pitch == 320u);
    assert(present_palette[7].r == 11u);
    assert(present_palette[7].g == 22u);
    assert(present_palette[7].b == 33u);

    fake_input_mask = PAL_CONTROL_RIGHT | PAL_CONTROL_A;
    expect_key(SDL_KEYDOWN, SDLK_RIGHT);
    expect_key(SDL_KEYDOWN, SDLK_RETURN);

    fake_input_mask = 0u;
    expect_key(SDL_KEYUP, SDLK_RIGHT);
    expect_key(SDL_KEYUP, SDLK_RETURN);
    memset(&event, 0, sizeof(event));
    assert(PalEngineBridge_PollEvent(&event) == 0);

    fake_input_mask = PAL_CONTROL_RIGHT;
    expect_key(SDL_KEYDOWN, SDLK_RIGHT);
    fake_input_mask = PAL_CONTROL_A;
    expect_key(SDL_KEYUP, SDLK_RIGHT);
    expect_key(SDL_KEYDOWN, SDLK_RETURN);
    fake_input_mask = 0u;
    expect_key(SDL_KEYUP, SDLK_RETURN);

    puts("engine_bridge: PASS");
    return 0;
}
