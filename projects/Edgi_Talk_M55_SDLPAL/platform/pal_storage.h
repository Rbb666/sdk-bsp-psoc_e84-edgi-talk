#ifndef PAL_STORAGE_H
#define PAL_STORAGE_H

#include <stdbool.h>

#define PAL_STORAGE_ROOT "/sdcard/pal"
#define PAL_STORAGE_SAVE_DIR "/sdcard/pal/save"

typedef bool (*pal_storage_probe_fn)(const char *path, void *context);

typedef struct pal_storage_result
{
    bool ready;
    char missing_name[16];
} pal_storage_result_t;

pal_storage_result_t pal_storage_validate(const char *root,
                                          pal_storage_probe_fn probe,
                                          void *context);
bool pal_storage_path_is_directory(const char *path);
bool pal_storage_prepare_save_dir(const char *path);

#endif
