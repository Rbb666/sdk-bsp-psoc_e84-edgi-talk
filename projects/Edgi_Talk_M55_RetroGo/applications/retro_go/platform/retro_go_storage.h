#ifndef RETRO_GO_STORAGE_H
#define RETRO_GO_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#define RETRO_GO_ROM_NAME_CAPACITY 256
#define RETRO_GO_ROM_PATH_CAPACITY 384

typedef struct retro_go_rom_entry
{
    char name[RETRO_GO_ROM_NAME_CAPACITY];
    char path[RETRO_GO_ROM_PATH_CAPACITY];
} retro_go_rom_entry_t;

bool retro_go_storage_wait_ready(unsigned timeout_ms);
size_t retro_go_storage_list_roms(retro_go_rom_entry_t *entries,
                                  size_t capacity,
                                  size_t *preferred_index);
bool retro_go_storage_select_rom(char *path, size_t capacity);
bool retro_go_storage_prepare_save_paths(const char *rom_path,
                                         char *sram_path,
                                         size_t sram_capacity,
                                         char *state_path,
                                         size_t state_capacity);
bool retro_go_storage_prepare_gba_save_paths(const char *rom_path,
                                             char *sram_path,
                                             size_t sram_capacity,
                                             char *state_path,
                                             size_t state_capacity);

#endif
