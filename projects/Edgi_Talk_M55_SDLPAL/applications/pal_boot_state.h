#ifndef PAL_BOOT_STATE_H
#define PAL_BOOT_STATE_H

#include <stdbool.h>

#include "pal_storage.h"

typedef enum pal_boot_state
{
    PAL_BOOT_WAIT_SD,
    PAL_BOOT_CHECK_RESOURCES,
    PAL_BOOT_INIT_IO,
    PAL_BOOT_RUN_ENGINE,
    PAL_BOOT_FATAL
} pal_boot_state_t;

typedef struct pal_boot_context
{
    pal_boot_state_t state;
    bool engine_started;
    char code[4];
    char detail[16];
} pal_boot_context_t;

typedef struct pal_boot_ops
{
    bool (*mount_ready)(void *context);
    pal_storage_result_t (*validate_resources)(void *context);
    bool (*initialize_io)(void *context);
    bool (*start_engine)(void *context);
    void *context;
} pal_boot_ops_t;

void pal_boot_init(pal_boot_context_t *boot);
pal_boot_state_t pal_boot_step(pal_boot_context_t *boot,
                               const pal_boot_ops_t *ops);

#endif
