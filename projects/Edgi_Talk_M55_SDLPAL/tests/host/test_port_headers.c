#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pal_display_port.h"
#include "pal_status.h"
#include "pal_touch_port.h"

static void compile_contract(void)
{
    uint8_t pixels[PAL_GAME_WIDTH * PAL_GAME_HEIGHT] = {0};
    pal_rgb_t palette[256] = {{0u, 0u, 0u}};
    pal_touch_point_t points[PAL_TOUCH_MAX_POINTS];
    size_t count = 0u;

    (void)pal_display_present_indexed(pixels, PAL_GAME_WIDTH, palette);
    (void)pal_touch_port_poll(points, PAL_TOUCH_MAX_POINTS, &count);
    pal_status_show(0xf800u, "E01", "SD MOUNT");
}

int main(void)
{
    compile_contract();
    return 0;
}
