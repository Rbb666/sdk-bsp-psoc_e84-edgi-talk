#ifndef PAL_STATUS_H
#define PAL_STATUS_H

#include <stdint.h>

void pal_status_show(uint16_t background, const char *code,
                     const char *detail);

#endif
