#include "pal_boot_state.h"

#include <stdio.h>
#include <string.h>

static void set_status(pal_boot_context_t *boot, const char *code,
                       const char *detail)
{
    (void)snprintf(boot->code, sizeof(boot->code), "%s", code);
    (void)snprintf(boot->detail, sizeof(boot->detail), "%s", detail);
}

static pal_boot_state_t set_fatal(pal_boot_context_t *boot,
                                  const char *code, const char *detail)
{
    boot->state = PAL_BOOT_FATAL;
    set_status(boot, code, detail);
    return boot->state;
}

void pal_boot_init(pal_boot_context_t *boot)
{
    if (boot == NULL) {
        return;
    }
    memset(boot, 0, sizeof(*boot));
    boot->state = PAL_BOOT_WAIT_SD;
    set_status(boot, "E00", "BOOT");
}

pal_boot_state_t pal_boot_step(pal_boot_context_t *boot,
                               const pal_boot_ops_t *ops)
{
    if (boot == NULL || ops == NULL) {
        return PAL_BOOT_FATAL;
    }

    switch (boot->state) {
    case PAL_BOOT_WAIT_SD:
        if (ops->mount_ready == NULL ||
            !ops->mount_ready(ops->context)) {
            set_status(boot, "E01", "SD MOUNT");
            return boot->state;
        }
        boot->state = PAL_BOOT_CHECK_RESOURCES;
        set_status(boot, "E00", "CHECK RES");
        break;

    case PAL_BOOT_CHECK_RESOURCES:
        if (ops->validate_resources == NULL) {
            return set_fatal(boot, "E02", "RESOURCE");
        } else {
            pal_storage_result_t result =
                ops->validate_resources(ops->context);
            if (!result.ready) {
                return set_fatal(boot, "E02",
                                 result.missing_name[0] != '\0'
                                     ? result.missing_name
                                     : "RESOURCE");
            }
        }
        boot->state = PAL_BOOT_INIT_IO;
        set_status(boot, "E00", "INIT IO");
        break;

    case PAL_BOOT_INIT_IO:
        if (ops->initialize_io == NULL ||
            !ops->initialize_io(ops->context)) {
            return set_fatal(boot, "E03", "INIT IO");
        }
        boot->state = PAL_BOOT_RUN_ENGINE;
        set_status(boot, "E00", "START");
        break;

    case PAL_BOOT_RUN_ENGINE:
        if (!boot->engine_started) {
            if (ops->start_engine == NULL ||
                !ops->start_engine(ops->context)) {
                return set_fatal(boot, "E04", "THREAD");
            }
            boot->engine_started = true;
            set_status(boot, "E00", "RUN");
        }
        break;

    case PAL_BOOT_FATAL:
    default:
        break;
    }

    return boot->state;
}
