#include "pal_input_port.h"

#if defined(BSP_SDLPAL_INPUT_TOUCH) == \
    defined(BSP_SDLPAL_INPUT_USB_KEYBOARD)
#error "Select exactly one SDLPal input mode"
#endif

#if defined(BSP_SDLPAL_INPUT_TOUCH)

#include <pal_touch_port.h>

#include "pal_display_port.h"

#ifndef BSP_LCD_ROTATION_DEGREES
#define BSP_LCD_ROTATION_DEGREES 0
#endif

bool pal_input_port_init(void)
{
    return pal_touch_port_init();
}

uint32_t pal_input_port_poll(void)
{
    static const pal_touch_calibration_t calibration = {false, false, false};
    pal_touch_point_t points[PAL_TOUCH_MAX_POINTS];
    size_t point_count = 0u;
    uint32_t controls;

    if (!pal_touch_port_poll(points, PAL_TOUCH_MAX_POINTS, &point_count))
    {
        point_count = 0u;
    }
    controls = pal_touch_controls(points, point_count,
                                  BSP_LCD_ROTATION_DEGREES,
                                  &calibration);
    pal_display_controls_set(controls);
    return controls;
}

#elif defined(BSP_SDLPAL_INPUT_USB_KEYBOARD)

#include <pal_usb_keyboard_port.h>

bool pal_input_port_init(void)
{
    return pal_usb_keyboard_host_start();
}

uint32_t pal_input_port_poll(void)
{
    return pal_usb_keyboard_controls_get();
}

#endif
