#ifndef PAL_ENGINE_BRIDGE_H
#define PAL_ENGINE_BRIDGE_H

#include "SDL.h"

void PalEngineBridge_RenderPresent(const void *pixels, int pitch,
                                   int width, int height);
void PalEngineBridge_RenderPresentIndexed(const void *pixels, int pitch,
                                          int width, int height,
                                          const void *palette_rgba);
int PalEngineBridge_PollEvent(SDL_Event *event);

#endif
