/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RETRO_GO_BOARD_H
#define RETRO_GO_BOARD_H

#include <rtthread.h>

#include "drv_common.h"
#include "drv_gpio.h"
#include "cy_result.h"
#include "cybsp.h"
#include "mtb_hal.h"

#define IFX_SRAM_SIZE (1408)
#define IFX_SRAM_END (0x26060000 + IFX_SRAM_SIZE * 1024)

#if defined(__ARMCC_VERSION)
extern int Image$$RW_IRAM1$$ZI$$Limit;
#define HEAP_BEGIN (&Image$$RW_IRAM1$$ZI$$Limit)
#define HEAP_END IFX_SRAM_END
#elif defined(__ICCARM__)
#pragma section="HEAP"
#define HEAP_BEGIN (__segment_end("HEAP"))
#else
extern unsigned int __end__;
extern unsigned int __HeapLimit;
#define HEAP_BEGIN ((void *)&__end__)
#define HEAP_END ((void *)&__HeapLimit)
#endif

void cy_bsp_all_init(void);

#endif
