#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <rtthread.h>

#include "SDL.h"

uint32_t rt_tick_get_millisecond(void)
{
    return 0u;
}

rt_tick_t rt_tick_get(void)
{
    return 0u;
}

void rt_thread_mdelay(rt_int32_t milliseconds)
{
    (void)milliseconds;
}

int main(void)
{
    Uint8 source_pixels[4] = {1u, 2u, 3u, 4u};
    Uint8 destination_pixels[4] = {0u, 0u, 0u, 0u};
    SDL_Color colors[4] = {
        {0u, 0u, 0u, 255u},
        {255u, 0u, 0u, 255u},
        {0u, 255u, 0u, 255u},
        {0u, 0u, 255u, 255u},
    };
    SDL_Surface *source;
    SDL_Surface *destination;
    SDL_Surface *owned;
    SDL_Palette *palette;
    SDL_Rect rectangle = {0, 0, 2, 2};
    SDL_Event event;
    const Uint8 *keyboard;

    source = SDL_CreateRGBSurfaceFrom(source_pixels, 2, 2, 8, 2,
                                      0u, 0u, 0u, 0u);
    destination = SDL_CreateRGBSurfaceFrom(destination_pixels, 2, 2, 8, 2,
                                           0u, 0u, 0u, 0u);
    assert(source != NULL && destination != NULL);

    palette = SDL_AllocPalette(4);
    assert(palette != NULL);
    assert(SDL_SetPaletteColors(palette, colors, 0, 4) == 0);
    assert(SDL_SetSurfacePalette(source, palette) == 0);
    assert(SDL_SetSurfacePalette(destination, palette) == 0);
    assert(SDL_BlitSurface(source, &rectangle, destination, &rectangle) == 0);
    assert(memcmp(source_pixels, destination_pixels,
                  sizeof(source_pixels)) == 0);

    owned = SDL_CreateRGBSurface(0u, 2, 2, 8, 0u, 0u, 0u, 0u);
    assert(owned != NULL && owned->pixels != NULL);
    assert(owned->pixels != source_pixels && owned->pixels != destination_pixels);
    SDL_FreeSurface(owned);

    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_RIGHT;
    assert(SDL_PushEvent(&event) == 1);
    keyboard = SDL_GetKeyboardState(NULL);
    assert(keyboard[SDL_GetScancodeFromKey(SDLK_RIGHT)] == 1u);
    memset(&event, 0, sizeof(event));
    assert(SDL_PollEvent(&event) == 1 && event.type == SDL_KEYDOWN);

    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYUP;
    event.key.keysym.sym = SDLK_RIGHT;
    assert(SDL_PushEvent(&event) == 1);
    assert(keyboard[SDL_GetScancodeFromKey(SDLK_RIGHT)] == 0u);
    assert(SDL_PollEvent(&event) == 1 && event.type == SDL_KEYUP);

    SDL_FreePalette(palette);
    SDL_FreeSurface(destination);
    SDL_FreeSurface(source);
    puts("sdl_shim: PASS");
    return 0;
}
