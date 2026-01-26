/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-01-23     Copilot      CANFD config template
 */

#ifndef __CANFD_CONFIG_H__
#define __CANFD_CONFIG_H__

#include <rtthread.h>
#include "board.h"
#include "cy_canfd.h"
#include "cy_sysint.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NOTE:
 * - 请将 CANFD_config/CANFD1_config 等配置结构体由 ModusToolbox 生成或手动实现。
 * - 如有多个 CANFD 实例，请分别定义对应的 config 与中断配置。
 */

#ifdef BSP_USING_CANFD0
extern const cy_stc_canfd_config_t CYBSP_CAN_FD_CH_0_config;

static cy_stc_sysint_t CANFD0_IRQ_cfg =
{
    .intrSrc = canfd_0_interrupts0_0_IRQn,
    .intrPriority = 3u,
};
#endif

#ifdef BSP_USING_CANFD1
extern const cy_stc_canfd_config_t CANFD1_config;

static cy_stc_sysint_t CANFD1_IRQ_cfg =
{
    .intrSrc = canfd_1_interrupts0_0_IRQn,
    .intrPriority = 3u,
};
#endif

/* CANFD0 */
#if defined(BSP_USING_CANFD0)
#ifndef CANFD0_CONFIG
#define CANFD0_CONFIG                                    \
    {                                                    \
        .name = "canfd0",                                \
        .base = CANFD0,                                  \
        .channel = 0u,                                   \
        .channel_mask = (1u << 0),                       \
        .mram_delay_us = 6u,                             \
        .irq = canfd_0_interrupts0_0_IRQn,               \
        .irq_cfg = &CANFD0_IRQ_cfg,                      \
        .isr = RT_NULL,                                  \
        .canfd_config = &CYBSP_CAN_FD_CH_0_config,       \
        .test_mode = CY_CANFD_TEST_MODE_DISABLE,         \
        .enable_brs = true,                              \
        .tx_buffer_index = 0u,                           \
    }
#endif
#endif /* BSP_USING_CANFD0 */

/* CANFD1 */
#if defined(BSP_USING_CANFD1)
#ifndef CANFD1_CONFIG
#define CANFD1_CONFIG                                    \
    {                                                    \
        .name = "canfd1",                                \
        .base = CANFD1,                                  \
        .channel = 0u,                                   \
        .channel_mask = (1u << 0),                       \
        .mram_delay_us = 6u,                             \
        .irq = canfd_1_interrupts0_0_IRQn,               \
        .irq_cfg = &CANFD1_IRQ_cfg,                      \
        .isr = RT_NULL,                                  \
        .canfd_config = &CANFD1_config,                  \
        .test_mode = CY_CANFD_TEST_MODE_DISABLE,         \
        .enable_brs = true,                              \
        .tx_buffer_index = 0u,                           \
    }
#endif
#endif /* BSP_USING_CANFD1 */

#ifdef __cplusplus
}
#endif

#endif /* __CANFD_CONFIG_H__ */
