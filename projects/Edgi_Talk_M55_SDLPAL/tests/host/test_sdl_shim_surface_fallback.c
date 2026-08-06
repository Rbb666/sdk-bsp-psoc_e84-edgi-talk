#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"
#include "pal_memory_policy.h"

static unsigned hot_failures;
static unsigned cold_allocations;

static void *hot_alloc(void *context, size_t size)
{
    (void)context;
    (void)size;
    ++hot_failures;
    return NULL;
}

static void hot_free(void *context, void *pointer)
{
    (void)context;
    (void)pointer;
}

static void *cold_alloc(void *context, size_t size)
{
    (void)context;
    ++cold_allocations;
    return malloc(size);
}

static void cold_free(void *context, void *pointer)
{
    (void)context;
    free(pointer);
}

int main(void)
{
    SDL_Surface *first;
    SDL_Surface *second;
    SDL_Surface *third;
    pal_memory_stats_t cold_stats;

    pal_memory_policy_configure(hot_alloc, hot_free, NULL,
                                cold_alloc, cold_free, NULL);

    first = SDL_CreateRGBSurface(0u, 320, 200, 8, 0u, 0u, 0u, 0u);
    second = SDL_CreateRGBSurface(0u, 320, 200, 8, 0u, 0u, 0u, 0u);
    third = SDL_CreateRGBSurface(0u, 320, 200, 8, 0u, 0u, 0u, 0u);
    assert(first != NULL && first->pixels != NULL);
    assert(second != NULL && second->pixels != NULL);
    assert(third != NULL && third->pixels != NULL);
    assert(hot_failures == 3u);
    assert(cold_allocations == 1u);

    SDL_FreeSurface(first);
    SDL_FreeSurface(second);
    SDL_FreeSurface(third);
    pal_memory_stats_get(PAL_MEMORY_CLASS_COLD, &cold_stats);
    assert(cold_stats.current_bytes == 0u);

    puts("sdl_shim surface fallback: PASS");
    return 0;
}
