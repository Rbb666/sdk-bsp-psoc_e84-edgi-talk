#ifndef RETRO_GO_LCD_API_H
#define RETRO_GO_LCD_API_H

#include <stdint.h>

#include <rtthread.h>

/* Public framebuffer helper implemented by the existing board LCD driver. */
void lcd_flush_rgb565_area(const void *pixels, uint32_t x, uint32_t y,
uint32_t width, uint32_t height,
uint32_t src_stride, rt_bool_t present);
rt_err_t lcd_wait_frame_done(uint32_t timeout_ms);

#endif
