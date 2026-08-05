#include "pal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "pal_display_port.h"

static const uint8_t digit_glyphs[10][5] = {
    {0x3eu, 0x51u, 0x49u, 0x45u, 0x3eu},
    {0x00u, 0x42u, 0x7fu, 0x40u, 0x00u},
    {0x42u, 0x61u, 0x51u, 0x49u, 0x46u},
    {0x21u, 0x41u, 0x45u, 0x4bu, 0x31u},
    {0x18u, 0x14u, 0x12u, 0x7fu, 0x10u},
    {0x27u, 0x45u, 0x45u, 0x45u, 0x39u},
    {0x3cu, 0x4au, 0x49u, 0x49u, 0x30u},
    {0x01u, 0x71u, 0x09u, 0x05u, 0x03u},
    {0x36u, 0x49u, 0x49u, 0x49u, 0x36u},
    {0x06u, 0x49u, 0x49u, 0x29u, 0x1eu},
};

static const uint8_t letter_glyphs[26][5] = {
    {0x7eu, 0x11u, 0x11u, 0x11u, 0x7eu},
    {0x7fu, 0x49u, 0x49u, 0x49u, 0x36u},
    {0x3eu, 0x41u, 0x41u, 0x41u, 0x22u},
    {0x7fu, 0x41u, 0x41u, 0x22u, 0x1cu},
    {0x7fu, 0x49u, 0x49u, 0x49u, 0x41u},
    {0x7fu, 0x09u, 0x09u, 0x09u, 0x01u},
    {0x3eu, 0x41u, 0x49u, 0x49u, 0x7au},
    {0x7fu, 0x08u, 0x08u, 0x08u, 0x7fu},
    {0x00u, 0x41u, 0x7fu, 0x41u, 0x00u},
    {0x20u, 0x40u, 0x41u, 0x3fu, 0x01u},
    {0x7fu, 0x08u, 0x14u, 0x22u, 0x41u},
    {0x7fu, 0x40u, 0x40u, 0x40u, 0x40u},
    {0x7fu, 0x02u, 0x0cu, 0x02u, 0x7fu},
    {0x7fu, 0x04u, 0x08u, 0x10u, 0x7fu},
    {0x3eu, 0x41u, 0x41u, 0x41u, 0x3eu},
    {0x7fu, 0x09u, 0x09u, 0x09u, 0x06u},
    {0x3eu, 0x41u, 0x51u, 0x21u, 0x5eu},
    {0x7fu, 0x09u, 0x19u, 0x29u, 0x46u},
    {0x46u, 0x49u, 0x49u, 0x49u, 0x31u},
    {0x01u, 0x01u, 0x7fu, 0x01u, 0x01u},
    {0x3fu, 0x40u, 0x40u, 0x40u, 0x3fu},
    {0x1fu, 0x20u, 0x40u, 0x20u, 0x1fu},
    {0x3fu, 0x40u, 0x38u, 0x40u, 0x3fu},
    {0x63u, 0x14u, 0x08u, 0x14u, 0x63u},
    {0x07u, 0x08u, 0x70u, 0x08u, 0x07u},
    {0x61u, 0x51u, 0x49u, 0x45u, 0x43u},
};

static const uint8_t glyph_space[5] = {0u, 0u, 0u, 0u, 0u};
static const uint8_t glyph_slash[5] = {0x20u, 0x10u, 0x08u, 0x04u, 0x02u};
static const uint8_t glyph_dot[5] = {0u, 0x60u, 0x60u, 0u, 0u};
static const uint8_t glyph_dash[5] = {0x08u, 0x08u, 0x08u, 0x08u, 0x08u};
static const uint8_t glyph_underscore[5] = {0x40u, 0x40u, 0x40u, 0x40u, 0x40u};

static const uint8_t *status_glyph(char character)
{
    if (character >= 'a' && character <= 'z')
    {
        character = (char)(character - 'a' + 'A');
    }
    if (character >= '0' && character <= '9')
    {
        return digit_glyphs[(unsigned int)(character - '0')];
    }
    if (character >= 'A' && character <= 'Z')
    {
        return letter_glyphs[(unsigned int)(character - 'A')];
    }

    switch (character)
    {
    case '/':
        return glyph_slash;
    case '.':
        return glyph_dot;
    case '-':
        return glyph_dash;
    case '_':
        return glyph_underscore;
    default:
        return glyph_space;
    }
}

static bool status_text_pixel(const char *text, uint16_t origin_x,
                              uint16_t origin_y, uint8_t scale,
                              uint16_t x, uint16_t y)
{
    size_t length;
    uint32_t relative_x;
    uint32_t relative_y;
    size_t character_index;
    uint32_t glyph_x;
    uint32_t glyph_y;
    const uint8_t *glyph;

    if (text == NULL || scale == 0u || x < origin_x || y < origin_y)
    {
        return false;
    }

    length = strlen(text);
    relative_x = x - origin_x;
    relative_y = y - origin_y;
    if (relative_y >= 7u * scale ||
        relative_x >= length * 6u * scale)
    {
        return false;
    }

    character_index = relative_x / (6u * scale);
    glyph_x = (relative_x % (6u * scale)) / scale;
    glyph_y = relative_y / scale;
    if (glyph_x >= 5u)
    {
        return false;
    }

    glyph = status_glyph(text[character_index]);
    return ((glyph[glyph_x] >> glyph_y) & 1u) != 0u;
}

static uint16_t status_foreground(uint16_t background)
{
    uint16_t red = (uint16_t)((background >> 11) & 0x1fu);
    uint16_t green = (uint16_t)((background >> 5) & 0x3fu);
    uint16_t blue = (uint16_t)(background & 0x1fu);
    return (red + green + blue) > 63u ? 0x0000u : 0xffffu;
}

void pal_status_show(uint16_t background, const char *code,
                     const char *detail)
{
    size_t capacity;
    uint16_t *buffer = pal_display_port_work_buffer(&capacity);
    uint16_t screen_width;
    uint16_t screen_height;
    uint16_t foreground = status_foreground(background);
    uint16_t first_y;

    pal_display_port_get_dimensions(&screen_width, &screen_height);
    if (buffer == NULL || capacity <
            PAL_DISPLAY_STRIP_WIDTH * PAL_DISPLAY_STRIP_ROWS)
    {
        return;
    }

    for (first_y = 0u; first_y < screen_height;
         first_y = (uint16_t)(first_y + PAL_DISPLAY_STRIP_ROWS))
    {
        uint16_t rows = (uint16_t)(screen_height - first_y);
        uint16_t first_x;
        if (rows > PAL_DISPLAY_STRIP_ROWS)
        {
            rows = PAL_DISPLAY_STRIP_ROWS;
        }

        for (first_x = 0u; first_x < screen_width;
             first_x = (uint16_t)(first_x + PAL_DISPLAY_STRIP_WIDTH))
        {
            uint16_t columns = (uint16_t)(screen_width - first_x);
            uint16_t local_y;
            bool present;

            if (columns > PAL_DISPLAY_STRIP_WIDTH)
            {
                columns = PAL_DISPLAY_STRIP_WIDTH;
            }

            for (local_y = 0u; local_y < rows; ++local_y)
            {
                uint16_t local_x;
                uint16_t y = (uint16_t)(first_y + local_y);
                for (local_x = 0u; local_x < columns; ++local_x)
                {
                    uint16_t x = (uint16_t)(first_x + local_x);
                    bool text_pixel =
                        status_text_pixel(code, 32u, 80u, 4u, x, y) ||
                        status_text_pixel(detail, 32u, 160u, 2u, x, y);
                    buffer[(size_t)local_y * columns + local_x] =
                        text_pixel ? foreground : background;
                }
            }

            present = (uint16_t)(first_y + rows) == screen_height &&
                      (uint16_t)(first_x + columns) == screen_width;
            (void)pal_display_port_flush_work_area(
                first_x, first_y, columns, rows, columns, present);
        }
    }
}
