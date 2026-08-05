#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pal_storage.h"

typedef struct fake_probe_context
{
    const char *missing;
    unsigned int calls;
} fake_probe_context_t;

static bool fake_probe(const char *path, void *opaque)
{
    fake_probe_context_t *context = (fake_probe_context_t *)opaque;
    size_t path_length = strlen(path);
    size_t missing_length;

    ++context->calls;
    if (context->missing == NULL)
    {
        return true;
    }

    missing_length = strlen(context->missing);
    return path_length < missing_length ||
           strcmp(path + path_length - missing_length,
                  context->missing) != 0;
}

int main(void)
{
    fake_probe_context_t context = {NULL, 0u};
    pal_storage_result_t result =
        pal_storage_validate("/sdcard/pal", fake_probe, &context);

    assert(result.ready);
    assert(result.missing_name[0] == '\0');
    assert(context.calls == 15u);

    context.missing = "map.mkf";
    context.calls = 0u;
    result = pal_storage_validate("/sdcard/pal/", fake_probe, &context);
    assert(!result.ready);
    assert(strcmp(result.missing_name, "map.mkf") == 0);
    assert(context.calls == 8u);

    puts("storage: PASS");
    return 0;
}
