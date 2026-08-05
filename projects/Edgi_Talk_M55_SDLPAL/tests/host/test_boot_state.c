#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pal_boot_state.h"

typedef struct fake_boot
{
    bool mounted;
    pal_storage_result_t resources;
    bool io_ready;
    bool engine_ready;
    unsigned start_count;
} fake_boot_t;

static bool fake_mount_ready(void *context)
{
    return ((fake_boot_t *)context)->mounted;
}

static pal_storage_result_t fake_validate(void *context)
{
    return ((fake_boot_t *)context)->resources;
}

static bool fake_init_io(void *context)
{
    return ((fake_boot_t *)context)->io_ready;
}

static bool fake_start_engine(void *context)
{
    fake_boot_t *fake = (fake_boot_t *)context;
    ++fake->start_count;
    return fake->engine_ready;
}

static pal_boot_ops_t fake_ops(fake_boot_t *fake)
{
    pal_boot_ops_t ops = {
        fake_mount_ready,
        fake_validate,
        fake_init_io,
        fake_start_engine,
        fake,
    };
    return ops;
}

int main(void)
{
    pal_boot_context_t boot;
    fake_boot_t fake = {false, {false, {0}}, true, true, 0u};
    pal_boot_ops_t ops = fake_ops(&fake);

    pal_boot_init(&boot);
    assert(pal_boot_step(&boot, &ops) == PAL_BOOT_WAIT_SD);
    assert(strcmp(boot.code, "E01") == 0);

    fake.mounted = true;
    assert(pal_boot_step(&boot, &ops) == PAL_BOOT_CHECK_RESOURCES);
    (void)snprintf(fake.resources.missing_name,
                   sizeof(fake.resources.missing_name), "map.mkf");
    assert(pal_boot_step(&boot, &ops) == PAL_BOOT_FATAL);
    assert(strcmp(boot.code, "E02") == 0);
    assert(strcmp(boot.detail, "map.mkf") == 0);

    memset(&fake, 0, sizeof(fake));
    fake.mounted = true;
    fake.resources.ready = true;
    fake.io_ready = true;
    fake.engine_ready = true;
    ops = fake_ops(&fake);
    pal_boot_init(&boot);
    assert(pal_boot_step(&boot, &ops) == PAL_BOOT_CHECK_RESOURCES);
    assert(pal_boot_step(&boot, &ops) == PAL_BOOT_INIT_IO);
    assert(pal_boot_step(&boot, &ops) == PAL_BOOT_RUN_ENGINE);
    assert(pal_boot_step(&boot, &ops) == PAL_BOOT_RUN_ENGINE);
    assert(pal_boot_step(&boot, &ops) == PAL_BOOT_RUN_ENGINE);
    assert(fake.start_count == 1u);
    assert(boot.engine_started);

    puts("boot_state: PASS");
    return 0;
}
