#ifndef RETRO_GO_TIME_H
#define RETRO_GO_TIME_H

#include <stdbool.h>
#include <stdint.h>

bool retro_go_time_init(void);
bool retro_go_time_dwt_active(void);
uint32_t retro_go_time_now_cycles(void);
uint32_t retro_go_time_elapsed_cycles(uint32_t started_cycles);
uint32_t retro_go_time_cycles_to_us(uint64_t cycles);
uint32_t retro_go_time_cycles_to_tenths_ms(uint64_t cycles);
uint32_t retro_go_time_clock_hz(void);

#endif
