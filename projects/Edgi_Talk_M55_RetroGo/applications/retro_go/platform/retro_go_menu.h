#ifndef RETRO_GO_MENU_H
#define RETRO_GO_MENU_H

#include "retro_go_storage.h"

#include <stdbool.h>
#include <stddef.h>

void retro_go_menu_show_status(const char *status);
void retro_go_menu_show_no_roms(void);
bool retro_go_menu_choose_game(const retro_go_rom_entry_t *entries,
                               size_t count, size_t preferred_index,
                               size_t *selected_index);

#endif
