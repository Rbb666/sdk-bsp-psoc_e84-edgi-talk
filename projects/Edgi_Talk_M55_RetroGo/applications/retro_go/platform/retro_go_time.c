#include "retro_go_time.h"

#include "cy_pdl.h"

#include <rtthread.h>

static uint32_t timing_clock_hz;
static bool dwt_active;
extern uint32_t SystemCoreClock;

bool retro_go_time_init(void)
{
    uint32_t before;
    uint32_t after;

    timing_clock_hz = Cy_SysClk_ClkHfGetFrequency(
        CY_SYSCLK_CLK_CORE_HF_PATH_NUM);
    if (timing_clock_hz == 0u)
    {
        timing_clock_hz = SystemCoreClock != 0u ?
            SystemCoreClock : 400000000u;
    }
    dwt_active = false;
#ifndef BSP_RETRO_GO_DWT_TIMING
    rt_kprintf("[retro-go] DWT timing disabled; RT tick fallback active\n");
    return false;
#endif
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    __DSB();
    __ISB();
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0u)
    {
        rt_kprintf("[retro-go] DWT CYCCNT unavailable; timing fallback active\n");
        return false;
    }
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    before = DWT->CYCCNT;
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    after = DWT->CYCCNT;
    dwt_active = after != before;
    rt_kprintf("[retro-go] DWT timing: active=%u clock=%u Hz wrap=%u ms\n",
               dwt_active ? 1u : 0u, (unsigned)timing_clock_hz,
               (unsigned)((uint64_t)UINT32_MAX * 1000u /
                          timing_clock_hz));
    return dwt_active;
}

bool retro_go_time_dwt_active(void)
{
    return dwt_active;
}

uint32_t retro_go_time_now_cycles(void)
{
    if (dwt_active)
    {
        return DWT->CYCCNT;
    }
    return (uint32_t)((uint64_t)rt_tick_get_millisecond() *
                      (timing_clock_hz / 1000u));
}

uint32_t retro_go_time_elapsed_cycles(uint32_t started_cycles)
{
    return retro_go_time_now_cycles() - started_cycles;
}

uint32_t retro_go_time_cycles_to_us(uint64_t cycles)
{
    return timing_clock_hz != 0u ?
        (uint32_t)(cycles * 1000000u / timing_clock_hz) : 0u;
}

uint32_t retro_go_time_cycles_to_tenths_ms(uint64_t cycles)
{
    return timing_clock_hz != 0u ?
        (uint32_t)(cycles * 10000u / timing_clock_hz) : 0u;
}

uint32_t retro_go_time_clock_hz(void)
{
    return timing_clock_hz;
}
