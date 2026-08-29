#include "retro_go_app.h"

#include "gnuboy.h"
#include "retro_go_audio.h"
#include "retro_go_core.h"
#include "retro_go_display.h"
#include "retro_go_gba.h"
#include "retro_go_input.h"
#include "retro_go_menu.h"
#include "retro_go_perf.h"
#include "retro_go_platform.h"
#include "retro_go_storage.h"
#include "retro_go_time.h"

#include <rtthread.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#define RETRO_GO_AUDIO_RATE 16000u
#define RETRO_GO_AUDIO_BUFFER_SAMPLES 256u
#define RETRO_GO_FRAME_US 16743u
#define RETRO_GO_PATH_CAPACITY RETRO_GO_ROM_PATH_CAPACITY

#ifndef BSP_RETRO_GO_AUTOSAVE_SECONDS
#define BSP_RETRO_GO_AUTOSAVE_SECONDS 10
#endif

#ifndef BSP_RETRO_GO_SD_MOUNT_TIMEOUT_MS
#define BSP_RETRO_GO_SD_MOUNT_TIMEOUT_MS 30000
#endif

#ifndef BSP_RETRO_GO_MAX_FRAME_SKIP
#define BSP_RETRO_GO_MAX_FRAME_SKIP 2
#endif

#ifndef BSP_RETRO_GO_MAX_ROMS
#define BSP_RETRO_GO_MAX_ROMS 64
#endif

#ifndef BSP_RETRO_GO_INPUT_RELEASE_TIMEOUT_MS
#define BSP_RETRO_GO_INPUT_RELEASE_TIMEOUT_MS 1000
#endif

#ifndef BSP_RETRO_GO_GB_BIOS_PATH
#define BSP_RETRO_GO_GB_BIOS_PATH "/sdcard/retro-go/bios/gb_bios.bin"
#endif

#ifndef BSP_RETRO_GO_GBC_BIOS_PATH
#define BSP_RETRO_GO_GBC_BIOS_PATH "/sdcard/retro-go/bios/gbc_bios.bin"
#endif

static int16_t audio_buffer[RETRO_GO_AUDIO_BUFFER_SAMPLES] RETRO_GO_DTCM;
static char rom_path[RETRO_GO_PATH_CAPACITY] RETRO_GO_SOCMEM;
static char sram_path[RETRO_GO_PATH_CAPACITY] RETRO_GO_SOCMEM;
static char state_path[RETRO_GO_PATH_CAPACITY] RETRO_GO_SOCMEM;
static retro_go_rom_entry_t rom_entries[BSP_RETRO_GO_MAX_ROMS]
    RETRO_GO_SOCMEM;

static void video_callback(void *buffer)
{
    (void)retro_go_display_present((const uint16_t *)buffer);
}

static void audio_callback(void *buffer, size_t sample_count)
{
    retro_go_audio_submit((const int16_t *)buffer, sample_count);
}

#ifdef BSP_RETRO_GO_GB_BOOT_BIOS
static bool load_gameboy_boot_bios(void)
{
    uint8_t buffer[0x900u];
    const bool color = gnuboy_get_hwtype() == GB_HW_CGB;
    const char *path = color ? BSP_RETRO_GO_GBC_BIOS_PATH :
                               BSP_RETRO_GO_GB_BIOS_PATH;
    const size_t expected_size = color ? 0x900u : 0x100u;
    struct stat information;
    FILE *file;
    size_t received;
    int result;

    if (stat(path, &information) != 0)
    {
        rt_kprintf("[retro-go] %s boot BIOS not found; "
                   "startup animation skipped: %s\n",
                   color ? "GBC" : "GB", path);
        return false;
    }
    if ((size_t)information.st_size != expected_size)
    {
        rt_kprintf("[retro-go] invalid %s boot BIOS size: %u, expected %u: "
                   "%s\n", color ? "GBC" : "GB",
                   (unsigned)information.st_size,
                   (unsigned)expected_size, path);
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL)
    {
        rt_kprintf("[retro-go] %s boot BIOS open failed: %s\n",
                   color ? "GBC" : "GB", path);
        return false;
    }
    received = fread(buffer, 1u, expected_size, file);
    fclose(file);
    if (received != expected_size)
    {
        rt_kprintf("[retro-go] %s boot BIOS read failed: %u/%u\n",
                   color ? "GBC" : "GB", (unsigned)received,
                   (unsigned)expected_size);
        return false;
    }
    result = gnuboy_load_bios(buffer, expected_size);
    if (result != 0)
    {
        rt_kprintf("[retro-go] %s boot BIOS rejected: %d\n",
                   color ? "GBC" : "GB", result);
        return false;
    }
    rt_kprintf("[retro-go] %s boot BIOS ready: %s (%u bytes)\n",
               color ? "GBC" : "GB", path, (unsigned)expected_size);
    return true;
}
#endif

static void sync_rtc(void)
{
    time_t now = time(NULL);
    struct tm *local = localtime(&now);

    if (local != NULL)
    {
        gnuboy_set_time(local->tm_yday, local->tm_hour,
                        local->tm_min, local->tm_sec);
    }
}

static uint32_t buttons_to_gnuboy(uint32_t buttons)
{
    uint32_t pad = 0u;

    if ((buttons & RETRO_GO_BUTTON_RIGHT) != 0u) pad |= GB_PAD_RIGHT;
    if ((buttons & RETRO_GO_BUTTON_LEFT) != 0u) pad |= GB_PAD_LEFT;
    if ((buttons & RETRO_GO_BUTTON_UP) != 0u) pad |= GB_PAD_UP;
    if ((buttons & RETRO_GO_BUTTON_DOWN) != 0u) pad |= GB_PAD_DOWN;
    if ((buttons & RETRO_GO_BUTTON_A) != 0u) pad |= GB_PAD_A;
    if ((buttons & RETRO_GO_BUTTON_B) != 0u) pad |= GB_PAD_B;
    if ((buttons & RETRO_GO_BUTTON_SELECT) != 0u) pad |= GB_PAD_SELECT;
    if ((buttons & RETRO_GO_BUTTON_START) != 0u) pad |= GB_PAD_START;
    return pad;
}

static void handle_events(uint32_t events, bool *quit)
{
    if ((events & RETRO_GO_EVENT_SAVE) != 0u)
    {
        int result = gnuboy_save_state(state_path);
        rt_kprintf("[retro-go] save state: %s (%d)\n", state_path, result);
    }
    if ((events & RETRO_GO_EVENT_LOAD) != 0u)
    {
        int result = gnuboy_load_state(state_path);
        rt_kprintf("[retro-go] load state: %s (%d)\n", state_path, result);
    }
    if ((events & RETRO_GO_EVENT_RESET) != 0u)
    {
        gnuboy_reset(true);
        sync_rtc();
        rt_kprintf("[retro-go] hard reset\n");
    }
    if ((events & RETRO_GO_EVENT_QUIT) != 0u)
    {
        *quit = true;
    }
}

static int run_gb_game(bool audio_enabled)
{
    uint32_t last_buttons = UINT32_MAX;
    uint32_t next_frame_us;
    uint32_t last_autosave_ms;
    unsigned consecutive_skips = 0u;
    bool quit = false;
    int result;

    result = gnuboy_init(RETRO_GO_AUDIO_RATE, GB_AUDIO_MONO_S16,
                         GB_PIXEL_565_LE, video_callback,
                         audio_enabled ? audio_callback : NULL);
    if (result != 0)
    {
        return -6;
    }
    gnuboy_set_framebuffer(retro_go_display_framebuffer());
    gnuboy_set_soundbuffer(audio_buffer, RETRO_GO_AUDIO_BUFFER_SAMPLES);
    result = gnuboy_load_rom_file(rom_path);
    if (result != 0)
    {
        rt_kprintf("[retro-go] ROM load failed: %s (%d)\n", rom_path, result);
        gnuboy_free_rom();
        gnuboy_free_bios();
        return -7;
    }
#ifdef BSP_RETRO_GO_GB_BOOT_BIOS
    (void)load_gameboy_boot_bios();
#endif
    gnuboy_reset(true);
    (void)gnuboy_load_sram(sram_path);
    sync_rtc();
    rt_kprintf("[retro-go] running %s\n", rom_path);
    rt_kprintf("[retro-go] keys: arrows/WASD, Z/J=A, X/K=B, Enter=Start, "
               "RightShift/Backspace=Select, F5=save, F9=load, R=reset, "
               "Space=fast, Esc=save+launcher\n");
    retro_go_perf_begin("GB", RETRO_GO_FRAME_US);

    next_frame_us = rt_tick_get_millisecond() * 1000u;
    last_autosave_ms = rt_tick_get_millisecond();
    while (!quit)
    {
        uint32_t buttons = retro_go_input_buttons_get();
        uint32_t events = retro_go_input_events_take();
        bool fast_forward = retro_go_input_fast_forward_get();
        uint32_t now_us = rt_tick_get_millisecond() * 1000u;
        bool late = (int32_t)(now_us - next_frame_us) >
                    (int32_t)RETRO_GO_FRAME_US;
        bool draw = true;
        uint32_t work_started_cycles = retro_go_time_now_cycles();

        if (buttons != last_buttons)
        {
            gnuboy_set_pad((int)buttons_to_gnuboy(buttons));
            last_buttons = buttons;
        }
        handle_events(events, &quit);
        if (quit)
        {
            break;
        }

        if (BSP_RETRO_GO_MAX_FRAME_SKIP > 0 &&
            (late || fast_forward) &&
            consecutive_skips < (unsigned)BSP_RETRO_GO_MAX_FRAME_SKIP)
        {
            draw = false;
            ++consecutive_skips;
        }
        else
        {
            consecutive_skips = 0u;
        }
        (void)retro_go_core_run_frame(draw);
        if (BSP_RETRO_GO_AUTOSAVE_SECONDS > 0 && gnuboy_sram_dirty() &&
            (uint32_t)(rt_tick_get_millisecond() - last_autosave_ms) >=
                (uint32_t)BSP_RETRO_GO_AUTOSAVE_SECONDS * 1000u)
        {
            (void)gnuboy_save_sram(sram_path, true);
            last_autosave_ms = rt_tick_get_millisecond();
        }
        retro_go_perf_frame(
            draw, retro_go_time_elapsed_cycles(work_started_cycles));

        if (!fast_forward)
        {
            next_frame_us += RETRO_GO_FRAME_US;
            do
            {
                now_us = rt_tick_get_millisecond() * 1000u;
                if ((int32_t)(next_frame_us - now_us) > 1000)
                {
                    rt_thread_mdelay(1u);
                }
            } while ((int32_t)(next_frame_us - now_us) > 0);
            if ((int32_t)(now_us - next_frame_us) >
                (int32_t)(RETRO_GO_FRAME_US * 4u))
            {
                next_frame_us = now_us;
            }
        }
        else
        {
            next_frame_us = rt_tick_get_millisecond() * 1000u;
        }
    }

    retro_go_perf_end();
    if (gnuboy_sram_dirty())
    {
        (void)gnuboy_save_sram(sram_path, false);
    }
    gnuboy_free_rom();
    gnuboy_free_bios();
    rt_kprintf("[retro-go] stopped; SRAM=%s\n", sram_path);
    return 0;
}

static size_t find_preferred_rom(size_t count, size_t fallback)
{
    size_t index;

    if (rom_path[0] != '\0')
    {
        for (index = 0u; index < count; ++index)
        {
            if (strcmp(rom_entries[index].path, rom_path) == 0)
            {
                return index;
            }
        }
    }
    return fallback < count ? fallback : 0u;
}

int retro_go_app_run(void)
{
    size_t preferred_rom = 0u;

    rom_path[0] = '\0';
    rt_kprintf("[retro-go] PSoC Edge platform start\n");
    if (!retro_go_display_init())
    {
        return -1;
    }
    retro_go_menu_show_status("STARTING USB");
    if (!retro_go_input_init())
    {
        retro_go_menu_show_status("USB HOST ERROR");
        return -2;
    }
    retro_go_menu_show_status("MOUNTING SD");
    while (!retro_go_storage_wait_ready(BSP_RETRO_GO_SD_MOUNT_TIMEOUT_MS))
    {
        rt_kprintf("[retro-go] SD card is not ready; retrying mount wait\n");
        rt_thread_mdelay(250u);
    }

    for (;;)
    {
        size_t scan_preferred = 0u;
        size_t selected_rom = 0u;
        size_t rom_count = retro_go_storage_list_roms(
            rom_entries, BSP_RETRO_GO_MAX_ROMS, &scan_preferred);
        bool gba_rom = false;
        bool audio_enabled;
        int result;

        if (rom_count == 0u)
        {
            retro_go_menu_show_no_roms();
            rt_kprintf("[retro-go] no .gb/.gbc/.gba ROM in %s\n",
#ifdef BSP_RETRO_GO_ROM_DIR
                       BSP_RETRO_GO_ROM_DIR
#else
                       "/sdcard/roms"
#endif
            );
            return -4;
        }
        preferred_rom = find_preferred_rom(rom_count, scan_preferred);
        if (!retro_go_menu_choose_game(rom_entries, rom_count,
                                       preferred_rom, &selected_rom))
        {
            retro_go_input_session_barrier(
                BSP_RETRO_GO_INPUT_RELEASE_TIMEOUT_MS);
            rt_kprintf("[retro-go] launcher cancel consumed; "
                       "staying in menu\n");
            continue;
        }
        preferred_rom = selected_rom;
        result = snprintf(rom_path, sizeof(rom_path), "%s",
                          rom_entries[selected_rom].path);
        if (result <= 0 || (size_t)result >= sizeof(rom_path))
        {
            rt_kprintf("[retro-go] selected ROM path is too long\n");
            continue;
        }
        retro_go_display_prepare_game();
#ifdef BSP_RETRO_GO_GBA
        gba_rom = retro_go_gba_is_rom(rom_path);
#endif
        if (!(gba_rom ? retro_go_storage_prepare_gba_save_paths(
                             rom_path, sram_path, sizeof(sram_path),
                             state_path, sizeof(state_path)) :
                        retro_go_storage_prepare_save_paths(
                             rom_path, sram_path, sizeof(sram_path),
                             state_path, sizeof(state_path))))
        {
            rt_kprintf("[retro-go] save directory unavailable\n");
            continue;
        }

        audio_enabled = retro_go_audio_init();
#ifdef BSP_RETRO_GO_GBA
        if (gba_rom)
        {
            result = retro_go_gba_run(rom_path, sram_path, state_path);
            rt_kprintf("[retro-go] GBA stopped; SRAM=%s (%d)\n",
                       sram_path, result);
        }
        else
#endif
        {
            result = run_gb_game(audio_enabled);
            rt_kprintf("[retro-go] GB/GBC session finished: %d\n", result);
        }
        retro_go_audio_deinit();
        if (!retro_go_display_wait_gba_idle(500u))
        {
            rt_kprintf("[retro-go] display worker did not become idle; "
                       "menu return aborted\n");
            return -8;
        }
        /* Launcher output is 640 pixels wide. Clear both pillar-box strips
         * and the former performance panel before drawing it again. */
        retro_go_display_prepare_game();
        retro_go_input_session_barrier(
            BSP_RETRO_GO_INPUT_RELEASE_TIMEOUT_MS);
        rt_kprintf("[retro-go] returning to launcher\n");
    }
}
