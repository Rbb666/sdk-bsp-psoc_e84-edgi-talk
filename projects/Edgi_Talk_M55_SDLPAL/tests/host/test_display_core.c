#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pal_display_core.h"

static void test_palette_and_scaling(void)
{
    uint8_t indexed[PAL_GAME_WIDTH * PAL_GAME_HEIGHT];
    uint16_t output[PAL_PORTRAIT_WIDTH * 3u];
    pal_rgb_t colors[256];
    pal_display_palette_t palette;

    memset(indexed, 0, sizeof(indexed));
    memset(colors, 0, sizeof(colors));
    colors[1].r = 255u;
    colors[2].g = 255u;
    colors[3].b = 255u;
    colors[4].r = 0x12u;
    colors[4].g = 0x34u;
    colors[4].b = 0x56u;
    indexed[0] = 1u;
    indexed[1] = 2u;
    indexed[PAL_GAME_WIDTH] = 3u;

    pal_display_palette_set(&palette, colors);
    assert(palette.rgb565[1] == 0xf800u);
    assert(palette.rgb565[2] == 0x07e0u);
    assert(palette.rgb565[3] == 0x001fu);
    assert(palette.argb8888[1] == 0xffff0000u);
    assert(palette.argb8888[2] == 0xff00ff00u);
    assert(palette.argb8888[3] == 0xff0000ffu);
    assert(palette.argb8888[4] == 0xff123456u);

    assert(pal_display_convert_rows(indexed, PAL_GAME_WIDTH, 0u, 3u,
                                    &palette, output,
                                    PAL_PORTRAIT_WIDTH) == 3u);
    assert(output[0] == 0xf800u);
    assert(output[1] == 0xf800u);
    assert(output[2] == 0x07e0u);
    assert(output[PAL_PORTRAIT_WIDTH] == 0xf800u);
    assert(output[PAL_PORTRAIT_WIDTH * 2u] == 0x001fu);
}

static void test_pressed_control_changes_color(void)
{
    const size_t pixels = PAL_PORTRAIT_WIDTH * PAL_PORTRAIT_HEIGHT;
    pal_control_rect_t rects[PAL_CONTROL_COUNT];
    uint16_t *output = (uint16_t *)calloc(pixels, sizeof(*output));
    uint16_t released;
    uint16_t pressed;
    size_t i;

    assert(output != NULL);
    assert(pal_controls_layout(PAL_PORTRAIT_WIDTH, PAL_PORTRAIT_HEIGHT,
                               rects) == PAL_CONTROL_COUNT);

    for (i = 0u; i < PAL_CONTROL_COUNT; ++i)
    {
        if (rects[i].mask == PAL_CONTROL_A)
        {
            size_t sample = (rects[i].y + rects[i].height / 2u) *
                            PAL_PORTRAIT_WIDTH +
                            rects[i].x + rects[i].width / 2u;
            pal_display_draw_controls(output, PAL_PORTRAIT_WIDTH,
                                      PAL_PORTRAIT_WIDTH,
                                      PAL_PORTRAIT_HEIGHT, 0u);
            released = output[sample];
            pal_display_draw_controls(output, PAL_PORTRAIT_WIDTH,
                                      PAL_PORTRAIT_WIDTH,
                                      PAL_PORTRAIT_HEIGHT, PAL_CONTROL_A);
            pressed = output[sample];
            assert(released != pressed);

            pal_display_render_controls_area(output, 1u,
                                             PAL_PORTRAIT_WIDTH,
                                             PAL_PORTRAIT_HEIGHT,
                                             (uint16_t)(rects[i].x +
                                                        rects[i].width / 2u),
                                             (uint16_t)(rects[i].y +
                                                        rects[i].height / 2u),
                                             1u, 1u, 0u);
            released = output[0];
            pal_display_render_controls_area(output, 1u,
                                             PAL_PORTRAIT_WIDTH,
                                             PAL_PORTRAIT_HEIGHT,
                                             (uint16_t)(rects[i].x +
                                                        rects[i].width / 2u),
                                             (uint16_t)(rects[i].y +
                                                        rects[i].height / 2u),
                                             1u, 1u, PAL_CONTROL_A);
            assert(released != output[0]);
            free(output);
            return;
        }
    }

    assert(!"A control rectangle missing");
}

int main(void)
{
    test_palette_and_scaling();
    test_pressed_control_changes_color();
    puts("display_core: PASS");
    return 0;
}
