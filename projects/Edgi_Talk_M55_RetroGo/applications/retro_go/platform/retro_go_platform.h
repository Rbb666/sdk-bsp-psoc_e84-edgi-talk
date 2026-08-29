#ifndef RETRO_GO_PLATFORM_H
#define RETRO_GO_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define RETRO_GO_DTCM __attribute__((section(".cy_dtcm"), aligned(8)))
#define RETRO_GO_SOCMEM __attribute__((section(".cy_socmem_data"), aligned(8)))
#define RETRO_GO_GFXMEM __attribute__((section(".cy_gpu_buf"), aligned(64)))
#define RETRO_GO_ITCM __attribute__((section(".cy_itcm")))
#else
#define RETRO_GO_DTCM
#define RETRO_GO_SOCMEM
#define RETRO_GO_GFXMEM
#define RETRO_GO_ITCM
#endif

#define RETRO_GO_GB_WIDTH 160u
#define RETRO_GO_GB_HEIGHT 144u

typedef enum retro_go_button
{
    RETRO_GO_BUTTON_RIGHT  = (1u << 0),
    RETRO_GO_BUTTON_LEFT   = (1u << 1),
    RETRO_GO_BUTTON_UP     = (1u << 2),
    RETRO_GO_BUTTON_DOWN   = (1u << 3),
    RETRO_GO_BUTTON_A      = (1u << 4),
    RETRO_GO_BUTTON_B      = (1u << 5),
    RETRO_GO_BUTTON_SELECT = (1u << 6),
    RETRO_GO_BUTTON_START  = (1u << 7),
    RETRO_GO_BUTTON_L      = (1u << 8),
    RETRO_GO_BUTTON_R      = (1u << 9),
} retro_go_button_t;

typedef enum retro_go_input_event
{
    RETRO_GO_EVENT_SAVE  = (1u << 0),
    RETRO_GO_EVENT_LOAD  = (1u << 1),
    RETRO_GO_EVENT_RESET = (1u << 2),
    RETRO_GO_EVENT_QUIT  = (1u << 3),
} retro_go_input_event_t;

#endif
