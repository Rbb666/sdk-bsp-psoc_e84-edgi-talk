#ifndef PAL_PSOC_EDGE_CONFIG_H
#define PAL_PSOC_EDGE_CONFIG_H

#include <stddef.h>
#include <ctype.h>

#include "pal_libc_compat.h"

#define SDL_isspace(character) isspace((unsigned char)(character))
#define strdup                  pal_strdup
#define strnlen                 pal_strnlen
#define strcasestr              pal_strcasestr
#define strncasecmp             SDL_strncasecmp

void pal_engine_fatal(const char *message);

#define PAL_PREFIX                    "/sdcard/pal/"
#define PAL_SAVE_PREFIX               "/sdcard/pal/save/"
#define PAL_CONFIG_PREFIX             PAL_PREFIX

#define PAL_PLATFORM                  "Infineon PSoC Edge M55"
#define PAL_CREDIT                    NULL
#define PAL_PORTYEAR                  NULL

#define PAL_DEFAULT_WINDOW_WIDTH      320
#define PAL_DEFAULT_WINDOW_HEIGHT     200
#define PAL_DEFAULT_FULLSCREEN_HEIGHT 200
#define PAL_DEFAULT_TEXTURE_WIDTH     320
#define PAL_DEFAULT_TEXTURE_HEIGHT    200

#define PAL_HAS_JOYSTICKS             0
#define PAL_HAS_TOUCH                 0
#define PAL_HAS_MOUSE                 0
#define PAL_HAS_NATIVEMIDI            0
#define PAL_HAS_MP3                   0
#define PAL_HAS_OGG                   0
#define PAL_HAS_OPUS                  0
#define PAL_HAS_GLSL                  0
#define PAL_HAS_CONFIG_PAGE           0
#define PAL_FILESYSTEM_IGNORE_CASE    1

#define PAL_VIDEO_INIT_FLAGS          SDL_WINDOW_SHOWN
#define PAL_SDL_INIT_FLAGS            (SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE)
#define PAL_FATAL_OUTPUT(message)     pal_engine_fatal(message)

#endif
