#include <board.h>
#include <rtdevice.h>
#include <rthw.h>
#include <rtthread.h>

#include "retro_go_app.h"
#include "retro_go_memory.h"
#include "retro_go_time.h"

#define RETRO_GO_LED_PIN GET_PIN(16, 6)
#define RETRO_GO_LCD_BACKLIGHT_PIN GET_PIN(15, 7)
#define RETRO_GO_LCD_PWM_PIN GET_PIN(20, 6)
#define RETRO_GO_LCD_STABILIZE_MS 1500u

#if defined(FINSH_THREAD_PRIORITY) && \
    defined(BSP_RETRO_GO_AUDIO_BACKEND_RTTHREAD)
#if !(FINSH_THREAD_PRIORITY < BSP_RETRO_GO_I2S_THREAD_PRIORITY && \
      BSP_RETRO_GO_I2S_THREAD_PRIORITY < \
          BSP_RETRO_GO_AUDIO_THREAD_PRIORITY && \
      BSP_RETRO_GO_AUDIO_THREAD_PRIORITY < CONFIG_USBHOST_PSC_PRIO && \
      CONFIG_USBHOST_PSC_PRIO < BSP_RETRO_GO_INPUT_THREAD_PRIORITY && \
      BSP_RETRO_GO_INPUT_THREAD_PRIORITY < RT_MAIN_THREAD_PRIORITY)
#error "Retro-Go priorities must follow MSH<I2S<audio<USB<HID<main"
#endif
#elif defined(FINSH_THREAD_PRIORITY)
#if !(FINSH_THREAD_PRIORITY < CONFIG_USBHOST_PSC_PRIO && \
      CONFIG_USBHOST_PSC_PRIO < BSP_RETRO_GO_INPUT_THREAD_PRIORITY && \
      BSP_RETRO_GO_INPUT_THREAD_PRIORITY < RT_MAIN_THREAD_PRIORITY)
#error "Retro-Go priorities must follow MSH<USB<HID<main"
#endif
#endif

static void enable_cpu_cache(void)
{
#if defined(SCB_CCR_BP_Msk)
    const unsigned branch_prediction_before =
        (unsigned)((SCB->CCR & SCB_CCR_BP_Msk) != 0u);
#else
    const unsigned branch_prediction_before = 0u;
#endif
#if defined(BSP_RETRO_GO_M55_BRANCH_PREDICTION) && \
    defined(SCB_CCR_BP_Msk)
    if ((SCB->CCR & SCB_CCR_BP_Msk) == 0u)
    {
        __DSB();
        __ISB();
        SCB->BPIALL = 0u;
        __DSB();
        __ISB();
        SCB->CCR |= SCB_CCR_BP_Msk;
        __DSB();
        __ISB();
    }
#endif
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    if (!rt_hw_cpu_icache_status())
    {
        rt_hw_cpu_icache_enable();
    }
#endif
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (!rt_hw_cpu_dcache_status())
    {
        rt_hw_cpu_dcache_enable();
    }
#endif
    rt_kprintf("[retro-go] M55 cache I=%d D=%d BP=%u->%u CCR=%08x\n",
               rt_hw_cpu_icache_status(), rt_hw_cpu_dcache_status(),
               branch_prediction_before,
#if defined(SCB_CCR_BP_Msk)
               (unsigned)((SCB->CCR & SCB_CCR_BP_Msk) != 0u),
#else
               0u,
#endif
               (unsigned)SCB->CCR);
}

static void enable_lcd_backlight(void)
{
    rt_pin_mode(RETRO_GO_LCD_BACKLIGHT_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(RETRO_GO_LCD_PWM_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(RETRO_GO_LCD_BACKLIGHT_PIN, PIN_HIGH);
    rt_pin_write(RETRO_GO_LCD_PWM_PIN, PIN_HIGH);
}

int main(void)
{
    uint32_t last_led_ms;
    rt_bool_t led_on = RT_FALSE;
    int result;

    rt_kprintf("PSoC Edge M55 standalone Retro-Go start\n");
    rt_pin_mode(RETRO_GO_LED_PIN, PIN_MODE_OUTPUT);
    retro_go_memory_prepare();
    enable_cpu_cache();
    (void)retro_go_time_init();
    rt_thread_mdelay(RETRO_GO_LCD_STABILIZE_MS);
    enable_lcd_backlight();

    result = retro_go_app_run();
    rt_kprintf("[retro-go] emulator exited: %d\n", result);
    last_led_ms = rt_tick_get_millisecond();
    while (1)
    {
        uint32_t now = rt_tick_get_millisecond();
        if ((now - last_led_ms) >= 500u)
        {
            led_on = !led_on;
            rt_pin_write(RETRO_GO_LED_PIN, led_on ? PIN_HIGH : PIN_LOW);
            last_led_ms = now;
        }
        rt_thread_mdelay(50u);
    }
}
