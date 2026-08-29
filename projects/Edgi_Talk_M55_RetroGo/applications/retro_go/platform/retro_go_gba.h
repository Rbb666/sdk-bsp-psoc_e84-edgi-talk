#ifndef RETRO_GO_GBA_H
#define RETRO_GO_GBA_H

#include <stdbool.h>

bool retro_go_gba_is_rom(const char *path);
int retro_go_gba_run(const char *rom_path, const char *sram_path,
                     const char *state_path);

#endif
