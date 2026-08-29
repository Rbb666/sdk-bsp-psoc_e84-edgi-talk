#include "retro_go_storage.h"

#include <dirent.h>
#include <dfs_fs.h>
#include <errno.h>
#include <rtdevice.h>
#include <rtthread.h>
#include <sys/stat.h>
#include <sys/statfs.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifndef BSP_RETRO_GO_ROM_DIR
#define BSP_RETRO_GO_ROM_DIR "/sdcard/roms"
#endif

#ifndef BSP_RETRO_GO_ROM_PATH
#define BSP_RETRO_GO_ROM_PATH ""
#endif

#ifndef BSP_RETRO_GO_SAVE_DIR
#define BSP_RETRO_GO_SAVE_DIR "/sdcard/retro-go/saves"
#endif

static bool is_directory(const char *path)
{
    struct stat info;

    return path != NULL && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool is_sdcard_mounted(void)
{
    static const char *const device_names[] = {"sd", "sd0", "sd1", "sd2"};
    struct statfs filesystem_info;
    size_t index;

    for (index = 0u; index < sizeof(device_names) / sizeof(device_names[0]);
         ++index)
    {
        rt_device_t device = rt_device_find(device_names[index]);
        const char *mounted_path = device != RT_NULL
                                       ? dfs_filesystem_get_mounted_path(device)
                                       : RT_NULL;

        if (mounted_path != RT_NULL && strcmp(mounted_path, "/sdcard") == 0 &&
            statfs("/sdcard", &filesystem_info) == 0 &&
            filesystem_info.f_bsize != 0u)
        {
            return true;
        }
    }
    return false;
}

static bool has_rom_extension(const char *name)
{
    size_t length;
    const char *extension;

    if (name == NULL)
    {
        return false;
    }
    length = strlen(name);
    if (length < 3u)
    {
        return false;
    }
    extension = strrchr(name, '.');
    return extension != NULL && extension != name &&
           (strcasecmp(extension, ".gb") == 0 ||
            strcasecmp(extension, ".gbc") == 0
#ifdef BSP_RETRO_GO_GBA
            || strcasecmp(extension, ".gba") == 0
#endif
           );
}

static bool ensure_directory(const char *path)
{
    if (is_directory(path))
    {
        return true;
    }
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

static const char *path_filename(const char *path)
{
    const char *separator = path != NULL ? strrchr(path, '/') : NULL;

    return separator != NULL ? separator + 1 : path;
}

static bool append_rom(retro_go_rom_entry_t *entries, size_t capacity,
                       size_t *count, const char *path, const char *name)
{
    struct stat info;
    size_t index;
    int name_written;
    int path_written;

    if (entries == NULL || count == NULL || path == NULL || name == NULL ||
        *count >= capacity || !has_rom_extension(name) ||
        stat(path, &info) != 0 || S_ISDIR(info.st_mode))
    {
        return false;
    }
    for (index = 0u; index < *count; ++index)
    {
        if (strcasecmp(entries[index].path, path) == 0)
        {
            return false;
        }
    }
    name_written = snprintf(entries[*count].name,
                            sizeof(entries[*count].name), "%s", name);
    path_written = snprintf(entries[*count].path,
                            sizeof(entries[*count].path), "%s", path);
    if (name_written <= 0 ||
        (size_t)name_written >= sizeof(entries[*count].name) ||
        path_written <= 0 ||
        (size_t)path_written >= sizeof(entries[*count].path))
    {
        return false;
    }
    ++(*count);
    return true;
}

static void sort_roms(retro_go_rom_entry_t *entries, size_t count)
{
    size_t index;

    for (index = 1u; index < count; ++index)
    {
        retro_go_rom_entry_t entry = entries[index];
        size_t position = index;

        while (position > 0u &&
               strcasecmp(entries[position - 1u].name, entry.name) > 0)
        {
            entries[position] = entries[position - 1u];
            --position;
        }
        entries[position] = entry;
    }
}

bool retro_go_storage_wait_ready(unsigned timeout_ms)
{
    uint32_t started = rt_tick_get_millisecond();
    unsigned consecutive_ready = 0u;

    rt_kprintf("[retro-go] waiting for /sdcard filesystem mount\n");
    do
    {
        if (is_sdcard_mounted())
        {
            ++consecutive_ready;
            if (consecutive_ready >= 2u)
            {
                rt_kprintf("[retro-go] /sdcard FAT filesystem is ready\n");
                return true;
            }
        }
        else
        {
            consecutive_ready = 0u;
        }
        rt_thread_mdelay(100u);
    } while ((uint32_t)(rt_tick_get_millisecond() - started) < timeout_ms);
    rt_kprintf("[retro-go] /sdcard mount wait timed out after %u ms\n",
               timeout_ms);
    return false;
}

size_t retro_go_storage_list_roms(retro_go_rom_entry_t *entries,
                                  size_t capacity,
                                  size_t *preferred_index)
{
    DIR *directory;
    struct dirent *entry;
    size_t count = 0u;
    size_t preferred = 0u;
    bool truncated = false;

    if (entries == NULL || capacity == 0u)
    {
        return 0u;
    }
    if (BSP_RETRO_GO_ROM_PATH[0] != '\0')
    {
        const char *filename = path_filename(BSP_RETRO_GO_ROM_PATH);

        if (!append_rom(entries, capacity, &count, BSP_RETRO_GO_ROM_PATH,
                        filename))
        {
            rt_kprintf("[retro-go] configured ROM missing: %s\n",
                       BSP_RETRO_GO_ROM_PATH);
        }
    }

    directory = opendir(BSP_RETRO_GO_ROM_DIR);
    if (directory != NULL)
    {
        while ((entry = readdir(directory)) != NULL)
        {
            char path[RETRO_GO_ROM_PATH_CAPACITY];
            int written;

            if (!has_rom_extension(entry->d_name))
            {
                continue;
            }
            if (count >= capacity)
            {
                truncated = true;
                continue;
            }
            written = snprintf(path, sizeof(path), "%s/%s",
                               BSP_RETRO_GO_ROM_DIR, entry->d_name);
            if (written > 0 && (size_t)written < sizeof(path))
            {
                (void)append_rom(entries, capacity, &count, path,
                                 entry->d_name);
            }
        }
        closedir(directory);
    }

    if (truncated)
    {
        rt_kprintf("[retro-go] ROM menu limited to %u entries\n",
                   (unsigned)capacity);
    }

    sort_roms(entries, count);
    if (BSP_RETRO_GO_ROM_PATH[0] != '\0')
    {
        size_t index;

        for (index = 0u; index < count; ++index)
        {
            if (strcasecmp(entries[index].path, BSP_RETRO_GO_ROM_PATH) == 0)
            {
                preferred = index;
                break;
            }
        }
    }
    if (preferred_index != NULL)
    {
        *preferred_index = preferred;
    }
    return count;
}

bool retro_go_storage_select_rom(char *path, size_t capacity)
{
    retro_go_rom_entry_t entry;
    size_t preferred = 0u;
    size_t count;
    int written;

    if (path == NULL || capacity == 0u)
    {
        return false;
    }
    count = retro_go_storage_list_roms(&entry, 1u, &preferred);
    if (count == 0u)
    {
        return false;
    }
    written = snprintf(path, capacity, "%s", entry.path);
    return written > 0 && (size_t)written < capacity;
}

static bool prepare_save_paths(const char *save_directory,
                               const char *rom_path,
                               char *sram_path,
                               size_t sram_capacity,
                               char *state_path,
                               size_t state_capacity)
{
    const char *filename;
    const char *extension;
    size_t stem_length;
    int sram_written;
    int state_written;

    if (rom_path == NULL || sram_path == NULL || state_path == NULL)
    {
        return false;
    }
    if (!ensure_directory(save_directory))
    {
        return false;
    }

    filename = strrchr(rom_path, '/');
    filename = filename != NULL ? filename + 1 : rom_path;
    extension = strrchr(filename, '.');
    stem_length = extension != NULL ? (size_t)(extension - filename) :
                                      strlen(filename);
    if (stem_length == 0u || stem_length > 128u)
    {
        return false;
    }
    sram_written = snprintf(sram_path, sram_capacity, "%s/%.*s.sav",
                            save_directory,
                            (int)stem_length, filename);
    state_written = snprintf(state_path, state_capacity, "%s/%.*s.state",
                             save_directory,
                             (int)stem_length, filename);
    return sram_written > 0 && (size_t)sram_written < sram_capacity &&
           state_written > 0 && (size_t)state_written < state_capacity;
}

bool retro_go_storage_prepare_save_paths(const char *rom_path,
                                         char *sram_path,
                                         size_t sram_capacity,
                                         char *state_path,
                                         size_t state_capacity)
{
    (void)ensure_directory("/sdcard/retro-go");
    return prepare_save_paths(BSP_RETRO_GO_SAVE_DIR, rom_path,
                              sram_path, sram_capacity,
                              state_path, state_capacity);
}

bool retro_go_storage_prepare_gba_save_paths(const char *rom_path,
                                             char *sram_path,
                                             size_t sram_capacity,
                                             char *state_path,
                                             size_t state_capacity)
{
    char directory[RETRO_GO_ROM_PATH_CAPACITY];
    int written;

    (void)ensure_directory("/sdcard/retro-go");
    if (!ensure_directory(BSP_RETRO_GO_SAVE_DIR))
    {
        return false;
    }
    written = snprintf(directory, sizeof(directory), "%s/gba",
                       BSP_RETRO_GO_SAVE_DIR);
    return written > 0 && (size_t)written < sizeof(directory) &&
           prepare_save_paths(directory, rom_path,
                              sram_path, sram_capacity,
                              state_path, state_capacity);
}
