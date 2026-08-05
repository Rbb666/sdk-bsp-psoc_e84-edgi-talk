#ifndef PAL_TEST_DISPLAY_DRV_LCD_H
#define PAL_TEST_DISPLAY_DRV_LCD_H

#include <stdint.h>

#include "rtthread.h"

void lcd_flush_rgb565_area(const void *pixels, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height,
                           uint32_t src_stride, rt_bool_t present);
rt_bool_t lcd_blit_indexed8(const void *pixels,
                            uint32_t width, uint32_t height,
                            uint32_t src_stride,
                            const uint32_t *clut,
                            uint32_t x, uint32_t y,
                            uint32_t dst_width, uint32_t dst_height,
                            rt_bool_t present);

#endif
