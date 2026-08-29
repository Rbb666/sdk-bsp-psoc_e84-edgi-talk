#include "retro_go_menu.h"

#include "retro_go_display.h"
#include "retro_go_cjk_font.h"
#include "retro_go_input.h"
#include "retro_go_launcher_assets.h"
#include "retro_go_launcher_font.h"
#include "retro_go_platform.h"

#include <rtthread.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define UI_WIDTH RETRO_GO_LAUNCHER_WIDTH
#define UI_HEIGHT RETRO_GO_LAUNCHER_HEIGHT
#define UI_HEADER_HEIGHT 50u
#define UI_LOGO_WIDTH 46u
#define UI_STATUS_X (UI_LOGO_WIDTH + 12u)
#define UI_STATUS_Y (UI_HEADER_HEIGHT - 16u)
#define UI_LIST_TOP 58u
#define UI_LINE_HEIGHT 16u
#define UI_VISIBLE_LINES 11u
#define UI_LABEL_CAPACITY 96u
#define UI_KEY_REPEAT_DELAY_MS 400u
#define UI_KEY_REPEAT_BASE_MS 400u

#define COLOR_SNOW 0xffdeu
#define COLOR_WHITE 0xffffu
#define COLOR_GRAY 0x8410u
#define COLOR_BLACK 0x0000u
#define COLOR_TRANSPARENT 0xf81fu

typedef enum launcher_tab
{
    LAUNCHER_TAB_GB,
    LAUNCHER_TAB_GBC,
    LAUNCHER_TAB_GBA,
} launcher_tab_t;

typedef struct repeat_state
{
    uint32_t button;
    uint32_t deadline;
    unsigned repeats;
} repeat_state_t;

static uint16_t *ui_framebuffer(void)
{
    return retro_go_display_launcher_framebuffer();
}

static void fill_rect(unsigned x, unsigned y, unsigned width,
                      unsigned height, uint16_t color)
{
    uint16_t *framebuffer = ui_framebuffer();
    unsigned row;

    if (x >= UI_WIDTH || y >= UI_HEIGHT || framebuffer == NULL)
    {
        return;
    }
    if (x + width > UI_WIDTH)
    {
        width = UI_WIDTH - x;
    }
    if (y + height > UI_HEIGHT)
    {
        height = UI_HEIGHT - y;
    }
    for (row = 0u; row < height; ++row)
    {
        uint16_t *destination = framebuffer + (y + row) * UI_WIDTH + x;
        unsigned column;

        for (column = 0u; column < width; ++column)
        {
            destination[column] = color;
        }
    }
}

static void clear_screen(uint16_t color)
{
    fill_rect(0u, 0u, UI_WIDTH, UI_HEIGHT, color);
}

static uint8_t asset_index(const retro_go_launcher_asset_t *asset,
                           size_t pixel)
{
    unsigned bits = asset->bits_per_pixel;
    size_t bit_offset = pixel * bits;
    unsigned shift = 8u - bits - (unsigned)(bit_offset & 7u);
    unsigned mask = (1u << bits) - 1u;

    return (uint8_t)((asset->data[bit_offset >> 3u] >> shift) & mask);
}

static uint16_t shade_color(uint16_t color, unsigned shade)
{
    unsigned red;
    unsigned green;
    unsigned blue;

    if (shade <= 1u)
    {
        return color;
    }
    if (shade == 4u)
    {
        red = ((color >> 11u) & 0x1fu) >> 2u;
        green = ((color >> 5u) & 0x3fu) >> 2u;
        blue = (color & 0x1fu) >> 2u;
    }
    else
    {
        red = ((color >> 11u) & 0x1fu) / shade;
        green = ((color >> 5u) & 0x3fu) / shade;
        blue = (color & 0x1fu) / shade;
    }
    return (uint16_t)((red << 11u) | (green << 5u) | blue);
}

static void draw_asset(unsigned x, unsigned y,
                       const retro_go_launcher_asset_t *asset,
                       unsigned shade)
{
    uint16_t *framebuffer = ui_framebuffer();
    uint16_t row;

    if (framebuffer == NULL || asset == NULL)
    {
        return;
    }
    for (row = 0u; row < asset->height && y + row < UI_HEIGHT; ++row)
    {
        uint16_t column;

        for (column = 0u;
             column < asset->width && x + column < UI_WIDTH; ++column)
        {
            uint8_t index = asset_index(
                asset, (size_t)row * asset->width + column);

            if (index < asset->palette_size && asset->alpha[index] >= 0x80u &&
                asset->palette[index] != COLOR_TRANSPARENT)
            {
                framebuffer[(size_t)(y + row) * UI_WIDTH + x + column] =
                    shade_color(asset->palette[index], shade);
            }
        }
    }
}

static const retro_go_launcher_glyph_t *find_glyph(uint16_t code)
{
    const uint8_t *cursor = retro_go_launcher_font.data;

    for (;;)
    {
        const retro_go_launcher_glyph_t *glyph =
            (const retro_go_launcher_glyph_t *)cursor;
        size_t bitmap_size;

        if (glyph->code == code)
        {
            return glyph;
        }
        if (glyph->code == 0u)
        {
            return code == '?' ? NULL : find_glyph('?');
        }
        bitmap_size = glyph->width == 0u ? 0u :
                      ((size_t)glyph->width * glyph->height + 7u) / 8u;
        cursor += sizeof(*glyph) + bitmap_size;
    }
}

static bool utf8_continuation(unsigned char value)
{
    return (value & 0xc0u) == 0x80u;
}

static uint32_t utf8_next(const char **text)
{
    const unsigned char *cursor = (const unsigned char *)*text;
    unsigned char first = cursor[0];
    uint32_t code;

    if (first < 0x80u)
    {
        *text += first != 0u ? 1 : 0;
        return first;
    }
    if (first >= 0xc2u && first <= 0xdfu && cursor[1] != 0u &&
        utf8_continuation(cursor[1]))
    {
        *text += 2;
        return ((uint32_t)(first & 0x1fu) << 6u) |
               (uint32_t)(cursor[1] & 0x3fu);
    }
    if (first >= 0xe0u && first <= 0xefu && cursor[1] != 0u &&
        cursor[2] != 0u && utf8_continuation(cursor[1]) &&
        utf8_continuation(cursor[2]))
    {
        code = ((uint32_t)(first & 0x0fu) << 12u) |
               ((uint32_t)(cursor[1] & 0x3fu) << 6u) |
               (uint32_t)(cursor[2] & 0x3fu);
        if (code >= 0x800u && (code < 0xd800u || code > 0xdfffu))
        {
            *text += 3;
            return code;
        }
    }
    if (first >= 0xf0u && first <= 0xf4u && cursor[1] != 0u &&
        cursor[2] != 0u && cursor[3] != 0u &&
        utf8_continuation(cursor[1]) && utf8_continuation(cursor[2]) &&
        utf8_continuation(cursor[3]))
    {
        code = ((uint32_t)(first & 0x07u) << 18u) |
               ((uint32_t)(cursor[1] & 0x3fu) << 12u) |
               ((uint32_t)(cursor[2] & 0x3fu) << 6u) |
               (uint32_t)(cursor[3] & 0x3fu);
        if (code >= 0x10000u && code <= 0x10ffffu)
        {
            *text += 4;
            return code;
        }
    }
    ++*text;
    return '?';
}

static unsigned glyph_bitmap(uint32_t *rows, uint32_t code)
{
    const uint8_t *cjk_bitmap = NULL;
    const retro_go_launcher_glyph_t *glyph;
    unsigned advance;

#ifdef BSP_RETRO_GO_CJK_MENU
    if (code >= 0x80u)
    {
        cjk_bitmap = retro_go_cjk_font_bitmap(code);
    }
#endif
    memset(rows, 0, 16u * sizeof(rows[0]));
    if (cjk_bitmap != NULL)
    {
        unsigned row;

        for (row = 0u; row < RETRO_GO_CJK_FONT_HEIGHT; ++row)
        {
            uint16_t packed = (uint16_t)(
                ((uint16_t)cjk_bitmap[row * 2u] << 8u) |
                cjk_bitmap[row * 2u + 1u]);
            unsigned column;

            for (column = 0u; column < RETRO_GO_CJK_FONT_WIDTH; ++column)
            {
                if ((packed & (uint16_t)(0x8000u >> column)) != 0u)
                {
                    rows[row] |= 1u << column;
                }
            }
        }
        return RETRO_GO_CJK_FONT_WIDTH;
    }
    glyph = find_glyph(code <= UINT16_MAX ? (uint16_t)code : '?');
    if (glyph == NULL)
    {
        return 0u;
    }
    advance = glyph->width > glyph->x_delta ? glyph->width : glyph->x_delta;
    if (glyph->width != 0u)
    {
        size_t bit = 0u;
        uint8_t y;

        for (y = 0u; y < glyph->height; ++y)
        {
            uint8_t x;
            int destination_y = (int)glyph->y_offset + y;
            int x_offset = glyph->x_offset < 0x80u ? glyph->x_offset :
                           -(0xff - glyph->x_offset);

            for (x = 0u; x < glyph->width; ++x, ++bit)
            {
                unsigned mask = 0x80u >> (bit & 7u);
                int destination_x = x_offset + x;

                if ((glyph->data[bit >> 3u] & mask) != 0u &&
                    destination_y >= 0 &&
                    destination_y < retro_go_launcher_font.height &&
                    destination_x >= 0 && destination_x < 32)
                {
                    rows[destination_y] |= 1u << destination_x;
                }
            }
        }
    }
    return advance;
}

static unsigned text_width(const char *text)
{
    unsigned width = 2u;

    while (text != NULL && *text != '\0')
    {
        uint32_t rows[16];
        uint32_t character = utf8_next(&text);

        width += glyph_bitmap(rows, character);
    }
    return width;
}

static void draw_text(unsigned x, unsigned y, unsigned max_width,
                      const char *text, uint16_t color)
{
    unsigned cursor = x + 1u;
    unsigned limit = max_width == 0u ? UI_WIDTH : x + max_width;

    while (text != NULL && *text != '\0')
    {
        uint32_t rows[16];
        uint32_t character = utf8_next(&text);
        unsigned advance = glyph_bitmap(rows, character);
        unsigned glyph_height = advance == RETRO_GO_CJK_FONT_WIDTH ?
            RETRO_GO_CJK_FONT_HEIGHT : retro_go_launcher_font.height;
        unsigned y_offset = advance == RETRO_GO_CJK_FONT_WIDTH ? 0u : 1u;
        unsigned row;

        if (cursor + advance > limit)
        {
            break;
        }
        for (row = 0u; row < glyph_height; ++row)
        {
            uint32_t bits = rows[row];
            unsigned column;

            for (column = 0u; column < advance; ++column)
            {
                if ((bits & (1u << column)) != 0u)
                {
                    fill_rect(cursor + column, y + y_offset + row,
                              1u, 1u, color);
                }
            }
        }
        cursor += advance;
    }
}

static void draw_text_centered(unsigned y, const char *text, uint16_t color)
{
    unsigned width = text_width(text);
    unsigned x = width < UI_WIDTH ? (UI_WIDTH - width) / 2u : 0u;

    draw_text(x, y, width, text, color);
}

static void present(void)
{
    (void)retro_go_display_present_launcher(ui_framebuffer());
}

static const retro_go_launcher_asset_t *tab_background(launcher_tab_t tab)
{
    if (tab == LAUNCHER_TAB_GBC)
    {
        return &retro_go_asset_background_gbc;
    }
    if (tab == LAUNCHER_TAB_GBA)
    {
        return &retro_go_asset_background_gba;
    }
    return &retro_go_asset_background_gb;
}

static const retro_go_launcher_asset_t *tab_logo(launcher_tab_t tab)
{
    if (tab == LAUNCHER_TAB_GBA)
    {
        return NULL;
    }
    return tab == LAUNCHER_TAB_GBC ? &retro_go_asset_logo_gbc :
                                     &retro_go_asset_logo_gb;
}

static const retro_go_launcher_asset_t *tab_banner(launcher_tab_t tab)
{
    if (tab == LAUNCHER_TAB_GBA)
    {
        return NULL;
    }
    return tab == LAUNCHER_TAB_GBC ? &retro_go_asset_banner_gbc :
                                     &retro_go_asset_banner_gb;
}

static const char *tab_description(launcher_tab_t tab)
{
    if (tab == LAUNCHER_TAB_GBA)
    {
        return "Nintendo Gameboy Advance";
    }
    return tab == LAUNCHER_TAB_GBC ? "Nintendo Gameboy Color" :
                                     "Nintendo Gameboy";
}

static const char *tab_short_name(launcher_tab_t tab)
{
    return tab == LAUNCHER_TAB_GBA ? "GBA" :
           tab == LAUNCHER_TAB_GBC ? "GBC" : "GB";
}

static launcher_tab_t next_tab(launcher_tab_t tab)
{
#ifdef BSP_RETRO_GO_GBA
    return tab == LAUNCHER_TAB_GB ? LAUNCHER_TAB_GBC :
           tab == LAUNCHER_TAB_GBC ? LAUNCHER_TAB_GBA : LAUNCHER_TAB_GB;
#else
    return tab == LAUNCHER_TAB_GB ? LAUNCHER_TAB_GBC : LAUNCHER_TAB_GB;
#endif
}

static launcher_tab_t previous_tab(launcher_tab_t tab)
{
#ifdef BSP_RETRO_GO_GBA
    return tab == LAUNCHER_TAB_GB ? LAUNCHER_TAB_GBA :
           tab == LAUNCHER_TAB_GBA ? LAUNCHER_TAB_GBC : LAUNCHER_TAB_GB;
#else
    return tab == LAUNCHER_TAB_GB ? LAUNCHER_TAB_GBC : LAUNCHER_TAB_GB;
#endif
}

static void draw_launcher(launcher_tab_t tab, bool browser)
{
    unsigned header_y = browser ? 0u : (UI_HEIGHT - UI_HEADER_HEIGHT) / 2u;

    clear_screen(COLOR_BLACK);
    draw_asset(0u, 0u, tab_background(tab), browser ? 4u : 1u);
    if (tab_logo(tab) != NULL)
    {
        draw_asset(0u, header_y, tab_logo(tab), 1u);
    }
    if (tab_banner(tab) != NULL)
    {
        draw_asset(UI_LOGO_WIDTH + 1u, header_y + 8u,
                   tab_banner(tab), 1u);
    }
    else
    {
        draw_text(UI_LOGO_WIDTH + 8u, header_y + 8u,
                  UI_WIDTH - UI_LOGO_WIDTH - 8u,
                  tab_description(tab), COLOR_SNOW);
    }
}

static bool entry_matches_tab(const retro_go_rom_entry_t *entry,
                              launcher_tab_t tab)
{
    const char *extension = entry != NULL ? strrchr(entry->name, '.') : NULL;

    if (extension == NULL)
    {
        return false;
    }
    if (tab == LAUNCHER_TAB_GBA)
    {
        return strcasecmp(extension, ".gba") == 0;
    }
    return tab == LAUNCHER_TAB_GBC ? strcasecmp(extension, ".gbc") == 0 :
                                     strcasecmp(extension, ".gb") == 0;
}

static launcher_tab_t entry_tab(const retro_go_rom_entry_t *entry)
{
    if (entry_matches_tab(entry, LAUNCHER_TAB_GBA))
    {
        return LAUNCHER_TAB_GBA;
    }
    return entry_matches_tab(entry, LAUNCHER_TAB_GBC) ?
               LAUNCHER_TAB_GBC : LAUNCHER_TAB_GB;
}

static size_t tab_entry_count(const retro_go_rom_entry_t *entries,
                              size_t count, launcher_tab_t tab)
{
    size_t matches = 0u;
    size_t index;

    for (index = 0u; index < count; ++index)
    {
        if (entry_matches_tab(&entries[index], tab))
        {
            ++matches;
        }
    }
    return matches;
}

static size_t catalog_index(const retro_go_rom_entry_t *entries,
                            size_t count, launcher_tab_t tab,
                            size_t tab_index)
{
    size_t match = 0u;
    size_t index;

    for (index = 0u; index < count; ++index)
    {
        if (entry_matches_tab(&entries[index], tab))
        {
            if (match == tab_index)
            {
                return index;
            }
            ++match;
        }
    }
    return count;
}

static size_t tab_index_for_catalog(const retro_go_rom_entry_t *entries,
                                    size_t count, launcher_tab_t tab,
                                    size_t catalog)
{
    size_t match = 0u;
    size_t index;

    for (index = 0u; index < count; ++index)
    {
        if (entry_matches_tab(&entries[index], tab))
        {
            if (index == catalog)
            {
                return match;
            }
            ++match;
        }
    }
    return 0u;
}

static void make_menu_label(const char *name, char *label, size_t capacity)
{
    const char *extension = name != NULL ? strrchr(name, '.') : NULL;
    size_t source_length = name != NULL ? strlen(name) : 0u;
    size_t output = 0u;
    size_t input = 0u;

    if (extension != NULL &&
        (strcasecmp(extension, ".gb") == 0 ||
         strcasecmp(extension, ".gbc") == 0 ||
         strcasecmp(extension, ".gba") == 0))
    {
        source_length = (size_t)(extension - name);
    }
    while (input < source_length && output + 1u < capacity)
    {
        unsigned char character = (unsigned char)name[input++];
        size_t sequence = 1u;

        if (character >= 0xc2u && character <= 0xdfu &&
            input < source_length &&
            utf8_continuation((unsigned char)name[input]))
        {
            sequence = 2u;
        }
        else if (character >= 0xe0u && character <= 0xefu &&
                 input + 1u < source_length &&
                 utf8_continuation((unsigned char)name[input]) &&
                 utf8_continuation((unsigned char)name[input + 1u]))
        {
            sequence = 3u;
        }
        else if (character >= 0xf0u && character <= 0xf4u &&
                 input + 2u < source_length &&
                 utf8_continuation((unsigned char)name[input]) &&
                 utf8_continuation((unsigned char)name[input + 1u]) &&
                 utf8_continuation((unsigned char)name[input + 2u]))
        {
            sequence = 4u;
        }
        else if (character >= 0x20u && character <= 0x7eu)
        {
            label[output++] = (char)character;
            continue;
        }
        else
        {
            label[output++] = '?';
            continue;
        }
        if (output + sequence >= capacity)
        {
            break;
        }
        label[output++] = (char)character;
        memcpy(label + output, name + input, sequence - 1u);
        output += sequence - 1u;
        input += sequence - 1u;
    }
    label[output] = '\0';
}

static void render_carousel(launcher_tab_t tab, const char *status)
{
    draw_launcher(tab, false);
    if (status != NULL && *status != '\0')
    {
        draw_text_centered(UI_HEIGHT - UI_LINE_HEIGHT - 1u,
                           status, COLOR_SNOW);
    }
    else
    {
        draw_text(124u, UI_HEIGHT - UI_LINE_HEIGHT - 1u, 24u, "GB",
                  tab == LAUNCHER_TAB_GB ? COLOR_WHITE : COLOR_GRAY);
        draw_text(148u, UI_HEIGHT - UI_LINE_HEIGHT - 1u, 28u, "GBC",
                  tab == LAUNCHER_TAB_GBC ? COLOR_WHITE : COLOR_GRAY);
#ifdef BSP_RETRO_GO_GBA
        draw_text(180u, UI_HEIGHT - UI_LINE_HEIGHT - 1u, 28u, "GBA",
                  tab == LAUNCHER_TAB_GBA ? COLOR_WHITE : COLOR_GRAY);
#endif
    }
    present();
}

static void render_browser(const retro_go_rom_entry_t *entries,
                           size_t catalog_count, launcher_tab_t tab,
                           size_t selected)
{
    size_t count = tab_entry_count(entries, catalog_count, tab);
    int first = (int)selected - (int)(UI_VISIBLE_LINES / 2u);
    char status[24];
    unsigned row;

    draw_launcher(tab, true);
    if (count == 0u)
    {
        (void)snprintf(status, sizeof(status), "List empty");
    }
    else
    {
        (void)snprintf(status, sizeof(status), "%u / %u",
                       (unsigned)(selected + 1u), (unsigned)count);
    }
    draw_text(UI_STATUS_X, UI_STATUS_Y, UI_WIDTH - UI_STATUS_X,
              status, COLOR_SNOW);
    {
        const char *name = tab_short_name(tab);
        unsigned width = text_width(name);

        draw_text(UI_WIDTH - width, UI_STATUS_Y, width, name, COLOR_SNOW);
    }

    for (row = 0u; row < UI_VISIBLE_LINES; ++row)
    {
        int tab_position = first + (int)row;
        unsigned y = UI_LIST_TOP + row * UI_LINE_HEIGHT;

        if (tab_position >= 0 && (size_t)tab_position < count)
        {
            size_t index = catalog_index(entries, catalog_count, tab,
                                         (size_t)tab_position);
            char label[UI_LABEL_CAPACITY];

            make_menu_label(entries[index].name, label, sizeof(label));
            draw_text(0u, y, UI_WIDTH, label,
                      (size_t)tab_position == selected ? COLOR_WHITE :
                                                        COLOR_GRAY);
        }
    }
    present();
}

static void render_empty_browser(launcher_tab_t tab)
{
    draw_launcher(tab, true);
    draw_text(UI_STATUS_X, UI_STATUS_Y, UI_WIDTH - UI_STATUS_X,
              "List empty", COLOR_SNOW);
    {
        const char *name = tab_short_name(tab);
        unsigned width = text_width(name);

        draw_text(UI_WIDTH - width, UI_STATUS_Y, width, name, COLOR_SNOW);
    }
    draw_text_centered(78u, "Welcome to Retro-Go!", COLOR_WHITE);
    draw_text_centered(108u, "Place roms in folder:", COLOR_GRAY);
    draw_text_centered(123u, "/sdcard/roms", COLOR_GRAY);
    draw_text_centered(153u, "With file extension:", COLOR_GRAY);
    draw_text_centered(
        168u, tab == LAUNCHER_TAB_GBA ? "gba" :
              tab == LAUNCHER_TAB_GBC ? "gbc" : "gb", COLOR_GRAY);
    present();
}

static void wait_buttons_released(uint32_t mask)
{
    while ((retro_go_input_buttons_get() & mask) != 0u)
    {
        rt_thread_mdelay(20u);
    }
}

#ifndef BSP_RETRO_GO_INPUT_RELEASE_TIMEOUT_MS
#define BSP_RETRO_GO_INPUT_RELEASE_TIMEOUT_MS 1000
#endif

static int repeat_direction(uint32_t buttons, uint32_t pressed,
                            repeat_state_t *repeat)
{
    const uint32_t directions = RETRO_GO_BUTTON_UP |
                                RETRO_GO_BUTTON_DOWN |
                                RETRO_GO_BUTTON_LEFT |
                                RETRO_GO_BUTTON_RIGHT;
    uint32_t held = buttons & directions;
    uint32_t newly_pressed = pressed & directions;
    uint32_t now = rt_tick_get_millisecond();

    if (newly_pressed != 0u)
    {
        repeat->button = newly_pressed & (0u - newly_pressed);
        repeat->deadline = now + UI_KEY_REPEAT_DELAY_MS;
        repeat->repeats = 0u;
        return (int)repeat->button;
    }
    if (repeat->button != 0u && (held & repeat->button) != 0u &&
        (int32_t)(now - repeat->deadline) >= 0)
    {
        unsigned interval;

        ++repeat->repeats;
        interval = UI_KEY_REPEAT_BASE_MS / (repeat->repeats + 1u);
        repeat->deadline = now + (interval == 0u ? 1u : interval);
        return (int)repeat->button;
    }
    if (held == 0u)
    {
        memset(repeat, 0, sizeof(*repeat));
    }
    return 0;
}

void retro_go_menu_show_status(const char *status)
{
    render_carousel(LAUNCHER_TAB_GB, status);
}

void retro_go_menu_show_no_roms(void)
{
    render_empty_browser(LAUNCHER_TAB_GB);
}

bool retro_go_menu_choose_game(const retro_go_rom_entry_t *entries,
                               size_t count, size_t preferred_index,
                               size_t *selected_index)
{
    const uint32_t accept_mask = RETRO_GO_BUTTON_START | RETRO_GO_BUTTON_A;
    const uint32_t back_mask = RETRO_GO_BUTTON_B;
    const uint32_t state_mask = accept_mask | back_mask |
                                RETRO_GO_BUTTON_SELECT |
                                RETRO_GO_BUTTON_L | RETRO_GO_BUTTON_R |
                                RETRO_GO_BUTTON_UP | RETRO_GO_BUTTON_DOWN |
                                RETRO_GO_BUTTON_LEFT | RETRO_GO_BUTTON_RIGHT;
    launcher_tab_t tab;

    if (entries == NULL || count == 0u || selected_index == NULL)
    {
        return false;
    }
    tab = entry_tab(&entries[preferred_index < count ? preferred_index : 0u]);
    rt_kprintf("[retro-go] ROM catalog: GB=%u GBC=%u GBA=%u\n",
               (unsigned)tab_entry_count(entries, count, LAUNCHER_TAB_GB),
               (unsigned)tab_entry_count(entries, count, LAUNCHER_TAB_GBC),
#ifdef BSP_RETRO_GO_GBA
               (unsigned)tab_entry_count(entries, count, LAUNCHER_TAB_GBA)
#else
               0u
#endif
    );
    wait_buttons_released(state_mask);
    retro_go_input_session_barrier(
        BSP_RETRO_GO_INPUT_RELEASE_TIMEOUT_MS);

    for (;;)
    {
        uint32_t previous = 0u;
        bool enter_browser = false;

        render_carousel(tab, NULL);
        while (!enter_browser)
        {
            uint32_t buttons = retro_go_input_buttons_get();
            uint32_t pressed = buttons & ~previous;

            if ((retro_go_input_events_take() & RETRO_GO_EVENT_QUIT) != 0u)
            {
                return false;
            }
            if ((pressed & (RETRO_GO_BUTTON_LEFT | RETRO_GO_BUTTON_UP |
                            RETRO_GO_BUTTON_SELECT |
                            RETRO_GO_BUTTON_L)) != 0u)
            {
                tab = previous_tab(tab);
                render_carousel(tab, NULL);
            }
            else if ((pressed & (RETRO_GO_BUTTON_RIGHT |
                                 RETRO_GO_BUTTON_DOWN |
                                 RETRO_GO_BUTTON_R)) != 0u)
            {
                tab = next_tab(tab);
                render_carousel(tab, NULL);
            }
            else if ((pressed & accept_mask) != 0u)
            {
                wait_buttons_released(accept_mask);
                enter_browser = true;
            }
            previous = buttons;
            rt_thread_mdelay(20u);
        }

        for (;;)
        {
            size_t tab_count = tab_entry_count(entries, count, tab);
            size_t selected =
                preferred_index < count &&
                entry_matches_tab(&entries[preferred_index], tab) ?
                    tab_index_for_catalog(entries, count, tab,
                                          preferred_index) : 0u;
            uint32_t previous = 0u;
            repeat_state_t repeat = {0u, 0u, 0u};
            bool return_to_carousel = false;

            if (tab_count == 0u)
            {
                render_empty_browser(tab);
            }
            else
            {
                render_browser(entries, count, tab, selected);
            }

            while (!return_to_carousel)
            {
                uint32_t buttons = retro_go_input_buttons_get();
                uint32_t pressed = buttons & ~previous;
                int direction;

                if ((retro_go_input_events_take() & RETRO_GO_EVENT_QUIT) != 0u)
                {
                    return false;
                }
                if ((pressed & back_mask) != 0u)
                {
                    wait_buttons_released(back_mask);
                    return_to_carousel = true;
                    previous = 0u;
                    continue;
                }
                if ((pressed & (RETRO_GO_BUTTON_SELECT |
                                RETRO_GO_BUTTON_L |
                                RETRO_GO_BUTTON_R)) != 0u)
                {
                    tab = (pressed & RETRO_GO_BUTTON_L) != 0u ?
                              previous_tab(tab) : next_tab(tab);
                    wait_buttons_released(RETRO_GO_BUTTON_SELECT |
                                           RETRO_GO_BUTTON_L |
                                           RETRO_GO_BUTTON_R);
                    break;
                }
                if ((pressed & accept_mask) != 0u && tab_count != 0u)
                {
                    size_t index = catalog_index(entries, count, tab, selected);

                    wait_buttons_released(accept_mask);
                    if (index < count)
                    {
                        *selected_index = index;
                        return true;
                    }
                }

                direction = repeat_direction(buttons, pressed, &repeat);
                if (tab_count != 0u && direction != 0)
                {
                    if ((direction & RETRO_GO_BUTTON_UP) != 0)
                    {
                        selected = selected == 0u ? tab_count - 1u :
                                                   selected - 1u;
                    }
                    else if ((direction & RETRO_GO_BUTTON_DOWN) != 0)
                    {
                        selected = selected + 1u == tab_count ? 0u :
                                                               selected + 1u;
                    }
                    else if ((direction & RETRO_GO_BUTTON_LEFT) != 0)
                    {
                        if (tab_count <= UI_VISIBLE_LINES)
                        {
                            tab = previous_tab(tab);
                            break;
                        }
                        selected = selected > UI_VISIBLE_LINES ?
                                       selected - UI_VISIBLE_LINES : 0u;
                    }
                    else if ((direction & RETRO_GO_BUTTON_RIGHT) != 0)
                    {
                        if (tab_count <= UI_VISIBLE_LINES)
                        {
                            tab = next_tab(tab);
                            break;
                        }
                        size_t next = selected + UI_VISIBLE_LINES;

                        selected = next < tab_count ? next : tab_count - 1u;
                    }
                    render_browser(entries, count, tab, selected);
                }
                previous = buttons;
                rt_thread_mdelay(20u);
            }
            if (return_to_carousel)
            {
                break;
            }
        }
        wait_buttons_released(state_mask);
        (void)retro_go_input_events_take();
    }
}
