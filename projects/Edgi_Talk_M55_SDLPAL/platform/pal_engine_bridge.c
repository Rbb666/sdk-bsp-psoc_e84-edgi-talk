#include <rtthread.h>

#include "pal_engine_bridge.h"

#include <limits.h>
#include <string.h>

#include "pal_controls.h"
#include "pal_display_port.h"
#include "pal_memory.h"
#include "pal_touch_port.h"

#ifndef BSP_LCD_ROTATION_DEGREES
#define BSP_LCD_ROTATION_DEGREES 0
#endif

#define PAL_BRIDGE_EVENT_QUEUE_LENGTH 8u

typedef struct pal_key_mapping
{
    uint32_t control;
    int key;
} pal_key_mapping_t;

static const pal_key_mapping_t key_mappings[PAL_CONTROL_COUNT] = {
    {PAL_CONTROL_UP, SDLK_UP},
    {PAL_CONTROL_DOWN, SDLK_DOWN},
    {PAL_CONTROL_LEFT, SDLK_LEFT},
    {PAL_CONTROL_RIGHT, SDLK_RIGHT},
    {PAL_CONTROL_A, SDLK_RETURN},
    {PAL_CONTROL_B, SDLK_ESCAPE},
    {PAL_CONTROL_PGUP, SDLK_PAGEUP},
    {PAL_CONTROL_PGDN, SDLK_PAGEDOWN},
};

static SDL_Event pending_events[PAL_BRIDGE_EVENT_QUEUE_LENGTH];
static unsigned pending_head;
static unsigned pending_tail;
static uint32_t previous_control_mask;
static uint32_t deferred_press_mask;
static pal_rgb_t present_palette[256];

static void queue_control_events(uint32_t mask, Uint32 type)
{
    size_t index;

    for (index = 0u; index < PAL_CONTROL_COUNT; ++index) {
        SDL_Event event;

        if ((mask & key_mappings[index].control) == 0u) {
            continue;
        }
        memset(&event, 0, sizeof(event));
        event.type = type;
        event.key.type = type;
        event.key.keysym.sym = key_mappings[index].key;
        pending_events[pending_head % PAL_BRIDGE_EVENT_QUEUE_LENGTH] = event;
        ++pending_head;
    }
}

static int pop_pending_event(SDL_Event *event)
{
    if (pending_tail == pending_head) {
        return 0;
    }
    if (event != NULL) {
        *event = pending_events[pending_tail % PAL_BRIDGE_EVENT_QUEUE_LENGTH];
    }
    ++pending_tail;
    return 1;
}

static void update_control_events(uint32_t current_mask)
{
    uint32_t released;
    uint32_t pressed;

    if (pending_tail != pending_head || deferred_press_mask != 0u ||
        current_mask == previous_control_mask) {
        return;
    }

    released = previous_control_mask & ~current_mask;
    pressed = current_mask & ~previous_control_mask;
    previous_control_mask = current_mask;

    if (released != 0u) {
        queue_control_events(released, SDL_KEYUP);
        deferred_press_mask = pressed;
    } else {
        queue_control_events(pressed, SDL_KEYDOWN);
    }
}

void PalEngineBridge_RenderPresent(const void *pixels, int pitch, int width,
                                   int height)
{
    (void)pixels;
    (void)pitch;
    (void)width;
    (void)height;
}

void PalEngineBridge_RenderPresentIndexed(const void *pixels, int pitch,
                                          int width, int height,
                                          const void *palette_rgba)
{
    const SDL_Color *source_palette = (const SDL_Color *)palette_rgba;
    size_t index;

    if (pixels == NULL || source_palette == NULL || width != 320 ||
        height != 200 || pitch < width) {
        return;
    }

    for (index = 0u; index < 256u; ++index) {
        present_palette[index].r = source_palette[index].r;
        present_palette[index].g = source_palette[index].g;
        present_palette[index].b = source_palette[index].b;
    }
    (void)pal_display_present_indexed((const uint8_t *)pixels,
                                      (size_t)pitch, present_palette);
}

int PalEngineBridge_PollEvent(SDL_Event *event)
{
    static const pal_touch_calibration_t calibration = {false, false, false};
    pal_touch_point_t points[PAL_TOUCH_MAX_POINTS];
    size_t point_count = 0u;
    uint32_t current_mask;

    if (pop_pending_event(event)) {
        return 1;
    }
    if (deferred_press_mask != 0u) {
        queue_control_events(deferred_press_mask, SDL_KEYDOWN);
        deferred_press_mask = 0u;
        return pop_pending_event(event);
    }

    if (!pal_touch_port_poll(points, PAL_TOUCH_MAX_POINTS, &point_count)) {
        point_count = 0u;
    }
    current_mask = pal_touch_controls(points, point_count,
                                      BSP_LCD_ROTATION_DEGREES,
                                      &calibration);
    pal_display_controls_set(current_mask);
    update_control_events(current_mask);
    return pop_pending_event(event);
}

Uint32 PalEngineBridge_GetTicks(void)
{
    return (Uint32)rt_tick_get_millisecond();
}

void PalEngineBridge_Delay(Uint32 milliseconds)
{
    if (milliseconds != 0u) {
        rt_thread_mdelay((rt_int32_t)(milliseconds > (Uint32)INT_MAX
                                          ? INT_MAX
                                          : milliseconds));
    }
}

void pal_engine_fatal(const char *message)
{
    rt_kprintf("SDLPal fatal: %s\n", message != RT_NULL ? message : "unknown");
    pal_memory_report("fatal-exit");
}
