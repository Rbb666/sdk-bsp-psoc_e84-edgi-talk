#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "SDL.h"
#include "rtthread.h"

static rt_tick_t fake_tick;
static rt_tick_t fake_milliseconds;
static rt_int32_t delay_calls[4];
static unsigned delay_call_count;

rt_tick_t rt_tick_get(void)
{
    return fake_tick;
}

rt_tick_t rt_tick_get_millisecond(void)
{
    return fake_milliseconds;
}

rt_err_t rt_thread_mdelay(rt_int32_t milliseconds)
{
    assert(delay_call_count < 4u);
    delay_calls[delay_call_count++] = milliseconds;
    return 0;
}

void PalEngineBridge_RenderPresent(const void *pixels, int pitch, int w, int h)
{
    (void)pixels;
    (void)pitch;
    (void)w;
    (void)h;
}

int PalEngineBridge_PollEvent(SDL_Event *event)
{
    (void)event;
    return 0;
}

int main(void)
{
    fake_milliseconds = 0x12345678u;
    fake_tick = 0x00abcdefu;

    assert(SDL_GetTicks() == 0x12345678u);
    assert(SDL_GetPerformanceCounter() == 0x00abcdefu);
    assert(SDL_GetPerformanceFrequency() == RT_TICK_PER_SECOND);

    SDL_Delay(0u);
    assert(delay_call_count == 0u);

    SDL_Delay(25u);
    assert(delay_call_count == 1u);
    assert(delay_calls[0] == 25);

    delay_call_count = 0u;
    SDL_Delay(UINT32_MAX);
    assert(delay_call_count == 3u);
    assert(delay_calls[0] == 2147483646);
    assert(delay_calls[1] == 2147483646);
    assert(delay_calls[2] == 3);

    puts("sdl_rtthread_time: PASS");
    return 0;
}
