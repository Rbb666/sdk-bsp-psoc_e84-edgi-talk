#ifndef RETRO_GO_LAUNCHER_FONT_H
#define RETRO_GO_LAUNCHER_FONT_H

#include <stddef.h>
#include <stdint.h>

typedef struct __attribute__((packed)) retro_go_launcher_glyph
{
    uint16_t code;
    uint8_t y_offset;
    uint8_t width;
    uint8_t height;
    uint8_t x_offset;
    uint8_t x_delta;
    uint8_t data[];
} retro_go_launcher_glyph_t;

typedef struct retro_go_launcher_font
{
    char name[16];
    uint8_t type;
    uint8_t width;
    uint8_t height;
    size_t chars;
    uint8_t data[];
} retro_go_launcher_font_t;

extern const retro_go_launcher_font_t retro_go_launcher_font;

#endif
