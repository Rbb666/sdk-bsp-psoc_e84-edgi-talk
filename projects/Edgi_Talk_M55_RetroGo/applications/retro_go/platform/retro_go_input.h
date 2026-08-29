#ifndef RETRO_GO_INPUT_H
#define RETRO_GO_INPUT_H

#include <stdbool.h>
#include <stdint.h>

bool retro_go_input_init(void);
uint32_t retro_go_input_buttons_get(void);
uint32_t retro_go_input_events_take(void);
bool retro_go_input_fast_forward_get(void);
void retro_go_input_session_barrier(uint32_t timeout_ms);

#endif
