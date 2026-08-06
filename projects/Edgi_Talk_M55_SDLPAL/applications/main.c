#include <rtdevice.h>
#include <rtthread.h>

#include <board.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pal_boot_state.h"
#include "pal_input_port.h"
#include "pal_memory.h"
#include "pal_status.h"
#include "pal_storage.h"

#define PAL_LED_PIN GET_PIN(16, 6)
#define PAL_LCD_BACKLIGHT_ENABLE_PIN GET_PIN(15, 7)
#define PAL_LCD_PWM_CONTROL_PIN GET_PIN(20, 6)

#define PAL_ENGINE_STACK_BYTES (24u * 1024u)
#define PAL_BOOT_RETRY_MS 1000u
#define PAL_MAIN_POLL_MS 50u
#define PAL_HEARTBEAT_MS 500u

#if defined(__GNUC__)
#define PAL_ENGINE_THREAD_STORAGE \
    __attribute__((section(".sdlpal_thread"), aligned(8)))
#else
#define PAL_ENGINE_THREAD_STORAGE
#endif

#ifndef BSP_LCD_STARTUP_STABILIZE_MS
#define BSP_LCD_STARTUP_STABILIZE_MS 1500u
#endif

extern int PAL_EngineMain(int argc, char *argv[]);

static struct rt_thread engine_thread_storage PAL_ENGINE_THREAD_STORAGE;
static rt_uint8_t engine_stack[PAL_ENGINE_STACK_BYTES]
    PAL_ENGINE_THREAD_STORAGE;
static bool engine_thread_started;

static void enable_lcd_backlight(void)
{
    rt_pin_mode(PAL_LCD_BACKLIGHT_ENABLE_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(PAL_LCD_PWM_CONTROL_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(PAL_LCD_BACKLIGHT_ENABLE_PIN, PIN_HIGH);
    rt_pin_write(PAL_LCD_PWM_CONTROL_PIN, PIN_HIGH);
}

static bool boot_mount_ready(void *context)
{
    (void)context;
    return pal_storage_path_is_directory("/sdcard");
}

static pal_storage_result_t boot_validate_resources(void *context)
{
    (void)context;
    return pal_storage_validate(PAL_STORAGE_ROOT, NULL, NULL);
}

static bool boot_initialize_io(void *context)
{
    (void)context;
    return rt_device_find("lcd") != RT_NULL &&
           pal_storage_prepare_save_dir(PAL_STORAGE_SAVE_DIR) &&
           pal_input_port_init();
}

static void engine_entry(void *parameter)
{
    char *argv[] = {"sdlpal", NULL};
    int result;

    (void)parameter;
    pal_memory_report("engine-entry");
    result = PAL_EngineMain(1, argv);
    rt_kprintf("SDLPal engine exited: %d\n", result);
    pal_memory_report("engine-exit");
    pal_status_show(0xc800u, "E05", "ENGINE EXIT");
}

static bool boot_start_engine(void *context)
{
    rt_err_t result;

    (void)context;
    if (engine_thread_started) {
        return true;
    }

    result = rt_thread_init(
        &engine_thread_storage, "sdlpal", engine_entry, RT_NULL,
        engine_stack, sizeof(engine_stack), RT_THREAD_PRIORITY_MAX / 2, 10u);
    if (result != RT_EOK) {
        return false;
    }
    if (rt_thread_startup(&engine_thread_storage) != RT_EOK) {
        (void)rt_thread_detach(&engine_thread_storage);
        return false;
    }
    engine_thread_started = true;
    return true;
}

static uint16_t status_background(const pal_boot_context_t *boot)
{
    return strcmp(boot->code, "E00") == 0 ? 0x0010u : 0xc800u;
}

int main(void)
{
    rt_thread_mdelay(2000);
    pal_boot_context_t boot;
    const pal_boot_ops_t ops = {
        boot_mount_ready,
        boot_validate_resources,
        boot_initialize_io,
        boot_start_engine,
        NULL,
    };
    char shown_code[sizeof(boot.code)] = "";
    char shown_detail[sizeof(boot.detail)] = "";
    uint32_t last_led_ms = rt_tick_get_millisecond();
    bool led_on = false;
    bool backlight_enabled = false;

    rt_kprintf("SDLPal PSoC Edge start, resources=%s\n", PAL_STORAGE_ROOT);
    pal_memory_init_allocators();
    rt_pin_mode(PAL_LED_PIN, PIN_MODE_OUTPUT);
    pal_boot_init(&boot);

    rt_thread_mdelay(BSP_LCD_STARTUP_STABILIZE_MS);
    if (rt_device_find("lcd") != RT_NULL) {
        pal_status_show(status_background(&boot), boot.code, boot.detail);
        (void)snprintf(shown_code, sizeof(shown_code), "%s", boot.code);
        (void)snprintf(shown_detail, sizeof(shown_detail), "%s", boot.detail);
        enable_lcd_backlight();
        backlight_enabled = true;
    }

    while (1) {
        pal_boot_state_t previous_state = boot.state;
        uint32_t now;

        (void)pal_boot_step(&boot, &ops);
        if (strcmp(shown_code, boot.code) != 0 ||
            strcmp(shown_detail, boot.detail) != 0) {
            if (rt_device_find("lcd") != RT_NULL) {
                pal_status_show(status_background(&boot),
                                boot.code, boot.detail);
                (void)snprintf(shown_code, sizeof(shown_code), "%s",
                               boot.code);
                (void)snprintf(shown_detail, sizeof(shown_detail), "%s",
                               boot.detail);
                if (!backlight_enabled) {
                    enable_lcd_backlight();
                    backlight_enabled = true;
                }
            }
            rt_kprintf("SDLPal boot %s: %s\n", boot.code, boot.detail);
        }

        now = rt_tick_get_millisecond();
        if ((now - last_led_ms) >= PAL_HEARTBEAT_MS) {
            led_on = !led_on;
            rt_pin_write(PAL_LED_PIN, led_on ? PIN_HIGH : PIN_LOW);
            last_led_ms = now;
        }

        if (boot.state == PAL_BOOT_WAIT_SD &&
            previous_state == PAL_BOOT_WAIT_SD) {
            rt_thread_mdelay(PAL_BOOT_RETRY_MS);
        } else {
            rt_thread_mdelay(PAL_MAIN_POLL_MS);
        }
    }
}
