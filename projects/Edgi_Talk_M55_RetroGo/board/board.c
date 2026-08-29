/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board.h"

#define BOARD_BASE_POWER_PIN GET_PIN(7, 2)
#define BOARD_AUDIO_POWER_PIN GET_PIN(16, 2)
#define BOARD_SPEAKER_POWER_PIN GET_PIN(21, 6)
#define BOARD_WIFI_POWER_PIN GET_PIN(16, 3)
#define BOARD_WIFI_REG_POWER_PIN GET_PIN(11, 6)
#define BOARD_LCD_BACKLIGHT_PIN GET_PIN(15, 7)
#define BOARD_LCD_POWER_PIN GET_PIN(15, 6)
#define BOARD_LCD_PWM_PIN GET_PIN(20, 6)

#define BOARD_POWER_OFF_DELAY_MS 50u
#define BOARD_POWER_STABLE_DELAY_MS 200u
#define BOARD_LCD_STABLE_DELAY_MS 200u

void cy_bsp_all_init(void)
{
    cy_rslt_t result = cybsp_init();

    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }
}

void _start(void)
{
    extern int entry(void);

    entry();
    while (1)
    {
    }
}

static int retro_go_board_power_init(void)
{
    rt_pin_mode(BOARD_BASE_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(BOARD_AUDIO_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(BOARD_SPEAKER_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(BOARD_WIFI_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(BOARD_WIFI_REG_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(BOARD_LCD_BACKLIGHT_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(BOARD_LCD_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(BOARD_LCD_PWM_PIN, PIN_MODE_OUTPUT);

    rt_pin_write(BOARD_AUDIO_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_SPEAKER_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_WIFI_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_WIFI_REG_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_LCD_BACKLIGHT_PIN, PIN_LOW);
    rt_pin_write(BOARD_LCD_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_LCD_PWM_PIN, PIN_LOW);
    rt_pin_write(BOARD_BASE_POWER_PIN, PIN_LOW);
    Cy_SysLib_Delay(BOARD_POWER_OFF_DELAY_MS);

    rt_pin_write(BOARD_BASE_POWER_PIN, PIN_HIGH);
    Cy_SysLib_Delay(BOARD_POWER_STABLE_DELAY_MS);

    /* Only power peripherals used by this standalone target. */
#ifdef BSP_RETRO_GO_AUDIO
    rt_pin_write(BOARD_AUDIO_POWER_PIN, PIN_HIGH);
    rt_pin_write(BOARD_SPEAKER_POWER_PIN, PIN_HIGH);
#endif
    rt_pin_write(BOARD_LCD_POWER_PIN, PIN_HIGH);
    Cy_SysLib_Delay(BOARD_LCD_STABLE_DELAY_MS);
    return RT_EOK;
}
INIT_BOARD_EXPORT(retro_go_board_power_init);

static void retro_go_poweroff(void)
{
    rt_pin_write(BOARD_LCD_BACKLIGHT_PIN, PIN_LOW);
    rt_pin_write(BOARD_LCD_PWM_PIN, PIN_LOW);
    rt_pin_write(BOARD_LCD_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_SPEAKER_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_AUDIO_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_WIFI_REG_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_WIFI_POWER_PIN, PIN_LOW);
    rt_pin_write(BOARD_BASE_POWER_PIN, PIN_LOW);
    Cy_SysClk_PllDisable(SRSS_DPLL_LP_0_PATH_NUM);
    Cy_SysPm_SystemEnterHibernate();
}

#ifdef RT_USING_MSH
MSH_CMD_EXPORT_ALIAS(retro_go_poweroff, poweroff,
                     power off the Retro-Go board peripherals);
#endif
