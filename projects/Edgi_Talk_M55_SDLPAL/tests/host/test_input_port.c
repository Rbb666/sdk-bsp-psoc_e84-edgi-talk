#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pal_controls.h"
#include "pal_input_port.h"

static unsigned display_control_calls;
static uint32_t displayed_controls;

void pal_display_controls_set(uint32_t controls)
{
    ++display_control_calls;
    displayed_controls = controls;
}

#if defined(BSP_SDLPAL_INPUT_TOUCH)

#include "pal_touch_port.h"

static bool touch_init_result;
static bool touch_poll_result;
static uint32_t touch_controls;
static unsigned touch_init_calls;
static unsigned touch_poll_calls;
static unsigned touch_control_calls;

bool pal_touch_port_init(void)
{
    ++touch_init_calls;
    return touch_init_result;
}

bool pal_touch_port_poll(pal_touch_point_t *points, size_t capacity,
                         size_t *count)
{
    ++touch_poll_calls;
    assert(points != NULL);
    assert(capacity == PAL_TOUCH_MAX_POINTS);
    assert(count != NULL);
    if (!touch_poll_result)
    {
        return false;
    }

    points[0].x = 100u;
    points[0].y = 200u;
    points[0].active = 1u;
    *count = 1u;
    return true;
}

uint32_t pal_touch_controls(const pal_touch_point_t *points, size_t count,
                            uint16_t rotation,
                            const pal_touch_calibration_t *calibration)
{
    ++touch_control_calls;
    assert(points != NULL);
    assert(rotation == 90u);
    assert(calibration != NULL);
    assert(!calibration->swap_xy);
    assert(!calibration->invert_x);
    assert(!calibration->invert_y);
    return count == 0u ? 0u : touch_controls;
}

static void test_touch_mode(void)
{
    touch_init_result = false;
    assert(!pal_input_port_init());
    touch_init_result = true;
    assert(pal_input_port_init());
    assert(touch_init_calls == 2u);

    touch_poll_result = true;
    touch_controls = PAL_CONTROL_LEFT;
    assert(pal_input_port_poll() == PAL_CONTROL_LEFT);
    assert(touch_poll_calls == 1u);
    assert(touch_control_calls == 1u);
    assert(display_control_calls == 1u);
    assert(displayed_controls == PAL_CONTROL_LEFT);

    touch_poll_result = false;
    assert(pal_input_port_poll() == 0u);
    assert(touch_poll_calls == 2u);
    assert(touch_control_calls == 2u);
    assert(display_control_calls == 2u);
    assert(displayed_controls == 0u);
}

#elif defined(BSP_SDLPAL_INPUT_USB_KEYBOARD)

static bool keyboard_init_result;
static uint32_t keyboard_controls;
static unsigned keyboard_init_calls;
static unsigned keyboard_poll_calls;

bool pal_usb_keyboard_host_start(void)
{
    ++keyboard_init_calls;
    return keyboard_init_result;
}

uint32_t pal_usb_keyboard_controls_get(void)
{
    ++keyboard_poll_calls;
    return keyboard_controls;
}

static void test_keyboard_mode(void)
{
    keyboard_init_result = false;
    assert(!pal_input_port_init());
    keyboard_init_result = true;
    assert(pal_input_port_init());
    assert(keyboard_init_calls == 2u);

    keyboard_controls = PAL_CONTROL_RIGHT | PAL_CONTROL_A;
    assert(pal_input_port_poll() == keyboard_controls);
    assert(keyboard_poll_calls == 1u);
    assert(display_control_calls == 0u);
}

#else
#error "Test requires one SDLPal input mode"
#endif

int main(void)
{
#if defined(BSP_SDLPAL_INPUT_TOUCH)
    test_touch_mode();
    puts("input_port_touch: PASS");
#else
    test_keyboard_mode();
    puts("input_port_keyboard: PASS");
#endif
    return 0;
}
