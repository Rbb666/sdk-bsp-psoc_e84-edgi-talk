#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pal_save_io_core.h"

typedef struct fake_sink_context
{
    uint8_t output[1600];
    size_t output_bytes;
    size_t requests[4];
    size_t calls;
    size_t short_call;
    size_t short_bytes;
    size_t yields;
} fake_sink_context_t;

static size_t fake_sink(const void *data, size_t bytes, void *opaque)
{
    fake_sink_context_t *context = (fake_sink_context_t *)opaque;
    size_t accepted = bytes;

    context->requests[context->calls++] = bytes;
    if (context->short_call != 0u && context->calls == context->short_call)
    {
        accepted = context->short_bytes;
    }
    memcpy(&context->output[context->output_bytes], data, accepted);
    context->output_bytes += accepted;
    return accepted;
}

static void fake_yield(void *opaque)
{
    fake_sink_context_t *context = (fake_sink_context_t *)opaque;
    ++context->yields;
}

static void test_splits_and_preserves_payload(void)
{
    uint8_t source[1500];
    fake_sink_context_t context = {0};
    size_t i;
    size_t written;

    for (i = 0u; i < sizeof(source); ++i)
    {
        source[i] = (uint8_t)(i * 17u);
    }

    written = pal_save_write_chunked(source, 1u, sizeof(source),
                                     fake_sink, &context,
                                     fake_yield, &context);

    assert(written == sizeof(source));
    assert(context.calls == 3u);
    assert(context.requests[0] == 512u);
    assert(context.requests[1] == 512u);
    assert(context.requests[2] == 476u);
    assert(context.yields == 2u);
    assert(context.output_bytes == sizeof(source));
    assert(memcmp(source, context.output, sizeof(source)) == 0);
}

static void test_stops_after_short_write(void)
{
    uint8_t source[1500] = {0};
    fake_sink_context_t context = {0};
    size_t written;

    context.short_call = 2u;
    context.short_bytes = 100u;
    written = pal_save_write_chunked(source, 1u, sizeof(source),
                                     fake_sink, &context,
                                     fake_yield, &context);

    assert(written == 612u);
    assert(context.calls == 2u);
    assert(context.yields == 1u);
}

static void test_rejects_invalid_and_overflowing_requests(void)
{
    uint8_t source = 0u;
    fake_sink_context_t context = {0};

    assert(pal_save_write_chunked(&source, SIZE_MAX, 2u,
                                  fake_sink, &context,
                                  fake_yield, &context) == 0u);
    assert(pal_save_write_chunked(NULL, 1u, 1u,
                                  fake_sink, &context,
                                  fake_yield, &context) == 0u);
    assert(pal_save_write_chunked(&source, 0u, 1u,
                                  fake_sink, &context,
                                  fake_yield, &context) == 0u);
    assert(context.calls == 0u);
}

int main(void)
{
    test_splits_and_preserves_payload();
    test_stops_after_short_write();
    test_rejects_invalid_and_overflowing_requests();
    puts("save_io_core: PASS");
    return 0;
}
