#ifndef PAL_LCD_API_H
#define PAL_LCD_API_H

#include <stdint.h>

#include <rtthread.h>

void lcd_flush_rgb565(const void *pixels, uint32_t width, uint32_t height);
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
rt_err_t lcd_wait_frame_done(uint32_t timeout_ms);

#endif
