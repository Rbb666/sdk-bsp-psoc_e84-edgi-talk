#ifndef PAL_INPUT_PORT_H
#define PAL_INPUT_PORT_H

#include <stdbool.h>
#include <stdint.h>

bool pal_input_port_init(void);
uint32_t pal_input_port_poll(void);

#endif
