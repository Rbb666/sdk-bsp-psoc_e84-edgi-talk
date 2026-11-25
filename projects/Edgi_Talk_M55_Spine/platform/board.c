/* SPDX-License-Identifier: Apache-2.0 */
#include "board.h"
#include <rtdevice.h>

#define BASEBOARD_POWER  GET_PIN(7, 2)
#define LCD_POWER        GET_PIN(15, 6)
#define LCD_BACKLIGHT_ENABLE_PIN  7U
#define LCD_BACKLIGHT_CTRL_PIN    6U

void cy_bsp_all_init(void)
{
    cy_rslt_t result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
}

void _start(void)
{
    extern int entry(void);
    entry();
    for (;;)
    {
        __WFI();
    }
}

static int spine_board_power(void)
{
    /* Keep the backlight off until the display has scanned the first frame. */
    Cy_GPIO_Pin_FastInit(GPIO_PRT15, LCD_BACKLIGHT_ENABLE_PIN, CY_GPIO_DM_STRONG, 0U, HSIOM_SEL_GPIO);
    Cy_GPIO_Pin_FastInit(GPIO_PRT20, LCD_BACKLIGHT_CTRL_PIN, CY_GPIO_DM_STRONG, 0U, HSIOM_SEL_GPIO);
    rt_pin_mode(BASEBOARD_POWER, PIN_MODE_OUTPUT);
    rt_pin_mode(LCD_POWER, PIN_MODE_OUTPUT);
    rt_pin_write(LCD_POWER, PIN_LOW);
    rt_pin_write(BASEBOARD_POWER, PIN_LOW);
    Cy_SysLib_Delay(50u);
    rt_pin_write(BASEBOARD_POWER, PIN_HIGH);
    Cy_SysLib_Delay(200u);
    rt_pin_write(LCD_POWER, PIN_HIGH);
    Cy_SysLib_Delay(200u);
    return RT_EOK;
}
INIT_BOARD_EXPORT(spine_board_power);

void spine_board_backlight_on(void)
{
    Cy_GPIO_Set(GPIO_PRT20, LCD_BACKLIGHT_CTRL_PIN);
    Cy_GPIO_Set(GPIO_PRT15, LCD_BACKLIGHT_ENABLE_PIN);
}
