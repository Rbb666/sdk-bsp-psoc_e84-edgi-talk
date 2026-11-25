/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SPINE_BOARD_H
#define SPINE_BOARD_H

#include <rtthread.h>
#include "drv_common.h"
#include "drv_gpio.h"
#include "cybsp.h"
#include "mtb_hal.h"

extern unsigned char __HeapBase;
extern unsigned char __HeapLimit;
#define HEAP_BEGIN (&__HeapBase)
#define HEAP_END   (&__HeapLimit)

void cy_bsp_all_init(void);
void spine_board_backlight_on(void);

#endif
