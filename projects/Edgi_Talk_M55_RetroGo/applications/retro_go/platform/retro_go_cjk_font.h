#ifndef RETRO_GO_CJK_FONT_H
#define RETRO_GO_CJK_FONT_H

#include <stdint.h>

#define RETRO_GO_CJK_FONT_WIDTH 16u
#define RETRO_GO_CJK_FONT_HEIGHT 16u

const uint8_t *retro_go_cjk_font_bitmap(uint32_t codepoint);

#endif
