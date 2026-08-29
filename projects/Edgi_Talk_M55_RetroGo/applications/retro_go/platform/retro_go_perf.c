#include "retro_go_perf.h"

#include "retro_go_audio.h"
#include "retro_go_display.h"
#include "retro_go_launcher_font.h"
#include "retro_go_platform.h"
#include "retro_go_time.h"

#include <rthw.h>
#include <rtthread.h>

#include <stdio.h>
#include <string.h>

#ifndef BSP_RETRO_GO_PERF_UPDATE_MS
#define BSP_RETRO_GO_PERF_UPDATE_MS 1000
#endif

/* Metric names and the one-second counter-delta model follow retro-go's
 * rg_system update_statistics() and rg_gui_draw_status_bars() at upstream
 * commit 4ced120669750ca7228fd0414211430c1d923166. The ESP/FreeRTOS GUI is
 * deliberately not linked; this is the RT-Thread platform adaptation. */

#ifdef BSP_RETRO_GO_PERF_OVERLAY

#define PERF_COLOR_BACKGROUND 0x0000u
#define PERF_COLOR_BORDER 0x07e0u
#define PERF_COLOR_TEXT 0xffffu
#define PERF_COLOR_DIM 0x8410u
#define PERF_COLOR_GOOD 0x07e0u
#define PERF_COLOR_WARN 0xffe0u
#define PERF_COLOR_BAD 0xf800u
#define PERF_LINE_HEIGHT 25u

typedef struct retro_go_perf_snapshot
{
    uint32_t fps_tenths;
    uint32_t display_fps_tenths;
    uint32_t speed_percent;
    uint32_t busy_percent;
    uint32_t drawn;
    uint32_t skipped;
    uint32_t max_work_tenths_ms;
    uint32_t audio_input_rate;
    uint32_t audio_output_rate;
    uint32_t audio_stretch_permille;
    uint32_t audio_buffer_ms;
    uint32_t audio_underruns;
} retro_go_perf_snapshot_t;

typedef struct retro_go_perf_state
{
    char system_name[5];
    bool active;
    bool force_render;
    uint32_t target_frame_us;
    uint32_t window_started_ms;
    uint32_t last_render_ms;
    uint32_t frames;
    uint32_t drawn;
    uint32_t skipped;
    uint64_t busy_cycles;
    uint32_t max_work_cycles;
    uint32_t display_completed_frames;
    retro_go_perf_snapshot_t snapshot;
} retro_go_perf_state_t;

static retro_go_perf_state_t perf RETRO_GO_SOCMEM;

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

static unsigned glyph_bitmap(uint32_t *rows, uint16_t code)
{
    const retro_go_launcher_glyph_t *glyph = find_glyph(code);
    unsigned advance;

    memset(rows, 0, retro_go_launcher_font.height * sizeof(rows[0]));
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

static void draw_text(uint16_t *framebuffer, uint16_t width, uint16_t height,
                      uint16_t stride, unsigned x, unsigned y,
                      const char *text, uint16_t color)
{
    unsigned cursor = x;

    while (text != NULL && *text != '\0')
    {
        uint32_t rows[16];
        unsigned char character = (unsigned char)*text++;
        unsigned advance = glyph_bitmap(
            rows, character < 0x80u ? character : '?');
        unsigned row;

        if (cursor + advance > width)
        {
            break;
        }
        for (row = 0u; row < retro_go_launcher_font.height; ++row)
        {
            uint32_t bits = rows[row];
            unsigned column;

            if (y + row >= height)
            {
                break;
            }
            for (column = 0u; column < advance; ++column)
            {
                if ((bits & (1u << column)) != 0u)
                {
                    framebuffer[(size_t)(y + row) * stride +
                                cursor + column] = color;
                }
            }
        }
        cursor += advance;
    }
}

static void fill_rect(uint16_t *framebuffer, uint16_t width, uint16_t height,
                      uint16_t stride, unsigned x, unsigned y,
                      unsigned rect_width, unsigned rect_height,
                      uint16_t color)
{
    unsigned row;

    if (x >= width || y >= height)
    {
        return;
    }
    if (rect_width > width - x) rect_width = width - x;
    if (rect_height > height - y) rect_height = height - y;
    for (row = 0u; row < rect_height; ++row)
    {
        uint16_t *destination = framebuffer + (size_t)(y + row) * stride + x;
        unsigned column;

        for (column = 0u; column < rect_width; ++column)
        {
            destination[column] = color;
        }
    }
}

static void update_snapshot(uint32_t now_ms)
{
    uint32_t elapsed_ms = now_ms - perf.window_started_ms;
    retro_go_audio_stats_t audio_stats;
    retro_go_display_stats_t display_stats;
    uint32_t target_fps_tenths;

    if (elapsed_ms == 0u)
    {
        return;
    }
    perf.snapshot.fps_tenths = perf.frames * 10000u / elapsed_ms;
    retro_go_display_get_stats(&display_stats);
    perf.snapshot.display_fps_tenths =
        (display_stats.completed_frames - perf.display_completed_frames) *
        10000u / elapsed_ms;
    perf.display_completed_frames = display_stats.completed_frames;
    target_fps_tenths = perf.target_frame_us != 0u ?
        10000000u / perf.target_frame_us : perf.snapshot.fps_tenths;
    perf.snapshot.speed_percent = target_fps_tenths != 0u ?
        perf.snapshot.fps_tenths * 100u / target_fps_tenths : 100u;
    if (perf.snapshot.speed_percent > 999u)
    {
        perf.snapshot.speed_percent = 999u;
    }
    {
        uint64_t elapsed_cycles =
            (uint64_t)elapsed_ms * retro_go_time_clock_hz() / 1000u;
        perf.snapshot.busy_percent = elapsed_cycles != 0u ?
            (uint32_t)(perf.busy_cycles * 100u / elapsed_cycles) : 0u;
    }
    if (perf.snapshot.busy_percent > 100u)
    {
        perf.snapshot.busy_percent = 100u;
    }
    perf.snapshot.drawn = perf.drawn;
    perf.snapshot.skipped = perf.skipped;
    perf.snapshot.max_work_tenths_ms =
        retro_go_time_cycles_to_tenths_ms(perf.max_work_cycles);
    retro_go_audio_get_stats(&audio_stats);
    perf.snapshot.audio_input_rate = audio_stats.input_rate_hz;
    perf.snapshot.audio_output_rate = audio_stats.output_rate_hz;
    perf.snapshot.audio_stretch_permille = audio_stats.stretch_permille;
    perf.snapshot.audio_buffer_ms = audio_stats.source_buffer_ms;
    perf.snapshot.audio_underruns = audio_stats.underrun_events;

    perf.window_started_ms = now_ms;
    perf.frames = 0u;
    perf.drawn = 0u;
    perf.skipped = 0u;
    perf.busy_cycles = 0u;
    perf.max_work_cycles = 0u;
}

static uint16_t speed_color(void)
{
    if (perf.snapshot.speed_percent >= 98u) return PERF_COLOR_GOOD;
    if (perf.snapshot.speed_percent >= 90u) return PERF_COLOR_WARN;
    return PERF_COLOR_BAD;
}

static uint16_t busy_color(void)
{
    if (perf.snapshot.busy_percent < 85u) return PERF_COLOR_GOOD;
    if (perf.snapshot.busy_percent < 95u) return PERF_COLOR_WARN;
    return PERF_COLOR_BAD;
}

static uint16_t audio_color(void)
{
    if (perf.snapshot.audio_output_rate >= 15800u &&
        perf.snapshot.audio_output_rate <= 16200u)
    {
        return PERF_COLOR_GOOD;
    }
    if (perf.snapshot.audio_output_rate >= 15000u &&
        perf.snapshot.audio_output_rate <= 17000u)
    {
        return PERF_COLOR_WARN;
    }
    return PERF_COLOR_BAD;
}

static uint16_t audio_input_color(void)
{
    if (perf.snapshot.audio_input_rate >= 15800u &&
        perf.snapshot.audio_input_rate <= 16200u)
    {
        return PERF_COLOR_GOOD;
    }
    if (perf.snapshot.audio_input_rate >= 12000u)
    {
        return PERF_COLOR_WARN;
    }
    return PERF_COLOR_BAD;
}

static void render_panel(uint16_t *framebuffer, uint16_t width,
                         uint16_t height, uint16_t stride)
{
    char line[20];
    unsigned y = 8u;

    fill_rect(framebuffer, width, height, stride, 0u, 0u, width, height,
              PERF_COLOR_BACKGROUND);
    fill_rect(framebuffer, width, height, stride, 0u, 0u, 2u, height,
              PERF_COLOR_BORDER);
    (void)snprintf(line, sizeof(line), "%s PERF", perf.system_name);
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              PERF_COLOR_BORDER);
    y += PERF_LINE_HEIGHT;
    fill_rect(framebuffer, width, height, stride, 5u, y - 6u,
              width > 10u ? width - 10u : 0u, 1u, PERF_COLOR_DIM);

    if (perf.snapshot.fps_tenths == 0u)
    {
        draw_text(framebuffer, width, height, stride, 6u, y,
                  "WARMUP", PERF_COLOR_WARN);
        return;
    }

    (void)snprintf(line, sizeof(line), "FPS %u.%u",
                   (unsigned)(perf.snapshot.fps_tenths / 10u),
                   (unsigned)(perf.snapshot.fps_tenths % 10u));
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              speed_color());
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "DFPS %u.%u",
                   (unsigned)(perf.snapshot.display_fps_tenths / 10u),
                   (unsigned)(perf.snapshot.display_fps_tenths % 10u));
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              PERF_COLOR_TEXT);
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "DRW %u",
                   (unsigned)perf.snapshot.drawn);
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              PERF_COLOR_TEXT);
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "SKP %u",
                   (unsigned)perf.snapshot.skipped);
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              perf.snapshot.skipped == 0u ? PERF_COLOR_GOOD : PERF_COLOR_WARN);
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "BUSY %u%%",
                   (unsigned)perf.snapshot.busy_percent);
    draw_text(framebuffer, width, height, stride, 6u, y, line, busy_color());
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "MAX %u.%u",
                   (unsigned)(perf.snapshot.max_work_tenths_ms / 10u),
                   (unsigned)(perf.snapshot.max_work_tenths_ms % 10u));
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              perf.snapshot.max_work_tenths_ms <= 170u ? PERF_COLOR_GOOD :
              PERF_COLOR_WARN);
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "AIN %u.%uK",
                   (unsigned)(perf.snapshot.audio_input_rate / 1000u),
                   (unsigned)((perf.snapshot.audio_input_rate % 1000u) /
                              100u));
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              audio_input_color());
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "AOUT %u.%uK",
                   (unsigned)(perf.snapshot.audio_output_rate / 1000u),
                   (unsigned)((perf.snapshot.audio_output_rate % 1000u) /
                              100u));
    draw_text(framebuffer, width, height, stride, 6u, y, line, audio_color());
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "STR %u%%",
                   (unsigned)(perf.snapshot.audio_stretch_permille / 10u));
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              PERF_COLOR_TEXT);
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "BUF %ums",
                   (unsigned)perf.snapshot.audio_buffer_ms);
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              PERF_COLOR_TEXT);
    y += PERF_LINE_HEIGHT;
    (void)snprintf(line, sizeof(line), "PLC %u",
                   (unsigned)perf.snapshot.audio_underruns);
    draw_text(framebuffer, width, height, stride, 6u, y, line,
              perf.snapshot.audio_underruns == 0u ?
                  PERF_COLOR_GOOD : PERF_COLOR_BAD);
}

void retro_go_perf_begin(const char *system_name, uint32_t target_frame_us)
{
    uint32_t now_ms = rt_tick_get_millisecond();
    retro_go_display_stats_t display_stats;
    rt_base_t level;

    retro_go_display_get_stats(&display_stats);
    level = rt_hw_interrupt_disable();

    memset(&perf, 0, sizeof(perf));
    perf.active = true;
    perf.force_render = true;
    perf.target_frame_us = target_frame_us;
    perf.display_completed_frames = display_stats.completed_frames;
    perf.window_started_ms = now_ms;
    perf.last_render_ms = now_ms;
    (void)snprintf(perf.system_name, sizeof(perf.system_name), "%s",
                   system_name != NULL ? system_name : "GAME");
    rt_hw_interrupt_enable(level);
}

void retro_go_perf_frame(bool drawn, uint32_t work_cycles)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (!perf.active)
    {
        rt_hw_interrupt_enable(level);
        return;
    }
    ++perf.frames;
    if (drawn)
    {
        ++perf.drawn;
    }
    else
    {
        ++perf.skipped;
    }
    perf.busy_cycles += work_cycles;
    if (work_cycles > perf.max_work_cycles)
    {
        perf.max_work_cycles = work_cycles;
    }
    rt_hw_interrupt_enable(level);
}

void retro_go_perf_end(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    perf.active = false;
    rt_hw_interrupt_enable(level);
}

void retro_go_perf_set_vglite(bool active)
{
    /* VG-Lite fallback is reported by the display path itself. Keep this
     * compatibility hook so callers do not pay for another per-frame lock. */
    (void)active;
}

bool retro_go_perf_render_due(uint16_t *framebuffer, uint16_t width,
                              uint16_t height, uint16_t stride)
{
    uint32_t now_ms;

    if (!perf.active || framebuffer == NULL || width == 0u || height == 0u ||
        stride < width)
    {
        return false;
    }
    now_ms = rt_tick_get_millisecond();
    if (!perf.force_render &&
        (uint32_t)(now_ms - perf.last_render_ms) <
            (uint32_t)BSP_RETRO_GO_PERF_UPDATE_MS)
    {
        return false;
    }
    if (!perf.force_render)
    {
        update_snapshot(now_ms);
    }
    render_panel(framebuffer, width, height, stride);
    perf.force_render = false;
    perf.last_render_ms = now_ms;
    return true;
}

#else

void retro_go_perf_begin(const char *system_name, uint32_t target_frame_us)
{
    (void)system_name;
    (void)target_frame_us;
}

void retro_go_perf_frame(bool drawn, uint32_t work_cycles)
{
    (void)drawn;
    (void)work_cycles;
}

void retro_go_perf_end(void)
{
}

void retro_go_perf_set_vglite(bool active)
{
    (void)active;
}

bool retro_go_perf_render_due(uint16_t *framebuffer, uint16_t width,
                              uint16_t height, uint16_t stride)
{
    (void)framebuffer;
    (void)width;
    (void)height;
    (void)stride;
    return false;
}

#endif
