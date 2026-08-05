#ifndef PAL_MEMORY_H
#define PAL_MEMORY_H

#include <stdint.h>

#include "pal_memory_policy.h"

#define PAL_INDEXED_SLOT_BYTES (64u * 1024u)

extern uint8_t pal_framebuffer_primary[PAL_INDEXED_SLOT_BYTES];
extern uint8_t pal_framebuffer_backup[PAL_INDEXED_SLOT_BYTES];

void pal_memory_clear_framebuffers(void);
void pal_memory_init_allocators(void);
void pal_memory_report(const char *stage);

#endif
