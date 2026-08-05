#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rtdevice.h"

#undef assert
#define assert(expression)                                                     \
    do                                                                         \
    {                                                                          \
        if (!(expression))                                                     \
        {                                                                      \
            fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, \
                    #expression);                                              \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (0)

#define ST7102_MAX_TOUCH 10u
#define ST7102_READ_BYTES 80u
#define CANARY_BYTES 64u

enum i2c_scenario
{
    I2C_EMPTY_80,
    I2C_TOUCH_SLOT4_80,
    I2C_INVALID_STALE_SLOT_80,
    I2C_TWO_VALID_TOUCHES_80,
    I2C_FALLBACK_EMPTY_32,
    I2C_FALLBACK_EMPTY_16
};

static enum i2c_scenario scenario;
static unsigned read_transfer_count;

static void set_touch_point(rt_uint8_t *data, unsigned slot,
                            rt_uint16_t x, rt_uint16_t y,
                            rt_uint8_t intensity, int valid)
{
    data[0x04u + slot * 7u] =
        (rt_uint8_t)((valid ? 0x80u : 0u) | ((x >> 8) & 0x3fu));
    data[0x05u + slot * 7u] = (rt_uint8_t)(x & 0xffu);
    data[0x06u + slot * 7u] = (rt_uint8_t)((y >> 8) & 0x3fu);
    data[0x07u + slot * 7u] = (rt_uint8_t)(y & 0xffu);
    data[0x09u + slot * 7u] = intensity;
}

rt_size_t ST7102_host_test_read_point(void *buf, rt_size_t read_num);
void ST7102_host_test_reset(void);

int rt_i2c_transfer(struct rt_i2c_bus_device *bus,
                    struct rt_i2c_msg messages[], int count)
{
    rt_uint8_t *data;
    rt_uint16_t len;

    (void)bus;
    if (count == 1)
    {
        return 1;
    }

    assert(count == 2);
    ++read_transfer_count;
    data = messages[1].buf;
    len = messages[1].len;

    if ((scenario == I2C_FALLBACK_EMPTY_32 && len > 32u) ||
        (scenario == I2C_FALLBACK_EMPTY_16 && len > 16u))
    {
        return 0;
    }

    memset(data, 0, len);
    if (scenario == I2C_TOUCH_SLOT4_80)
    {
        assert(len == ST7102_READ_BYTES);
        data[0] = 0x08u;
        set_touch_point(data, 4u, 100u, 120u, 1u, 1);
    }
    else if (scenario == I2C_INVALID_STALE_SLOT_80)
    {
        assert(len == ST7102_READ_BYTES);
        data[0] = 0x08u;
        set_touch_point(data, 2u, 75u, 90u, 5u, 0);
    }
    else if (scenario == I2C_TWO_VALID_TOUCHES_80)
    {
        assert(len == ST7102_READ_BYTES);
        data[0] = 0x08u;
        set_touch_point(data, 0u, 100u, 120u, 2u, 1);
        set_touch_point(data, 3u, 700u, 420u, 3u, 1);
    }
    return 2;
}

void rt_pin_write(rt_base_t pin, rt_base_t value)
{
    (void)pin;
    (void)value;
}

int rt_pin_read(rt_base_t pin)
{
    (void)pin;
    return PIN_LOW;
}

void rt_pin_mode(rt_base_t pin, rt_base_t mode)
{
    (void)pin;
    (void)mode;
}

void rt_thread_mdelay(int milliseconds)
{
    (void)milliseconds;
}

rt_device_t rt_device_find(const char *name)
{
    (void)name;
    return NULL;
}

rt_err_t rt_device_open(rt_device_t device, rt_uint16_t flags)
{
    (void)device;
    (void)flags;
    return RT_EOK;
}

int rt_hw_touch_register(rt_touch_t touch, const char *name,
                         rt_uint32_t flags, void *data)
{
    (void)touch;
    (void)name;
    (void)flags;
    (void)data;
    return RT_EOK;
}

static void assert_canary(const unsigned char *canary)
{
    size_t i;

    for (i = 0; i < CANARY_BYTES; ++i)
    {
        assert(canary[i] == 0xa5u);
    }
}

static void test_one_point_buffer_is_not_overwritten(void)
{
    struct guarded_buffer
    {
        struct rt_touch_data points[1];
        unsigned char canary[CANARY_BYTES];
    } guarded;

    ST7102_host_test_reset();
    scenario = I2C_EMPTY_80;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));

    assert(ST7102_host_test_read_point(guarded.points, 1u) == 0u);
    assert_canary(guarded.canary);
}

static void test_zero_capacity_and_null_buffer_do_not_read_i2c(void)
{
    struct rt_touch_data point;

    ST7102_host_test_reset();
    scenario = I2C_EMPTY_80;
    read_transfer_count = 0u;

    assert(ST7102_host_test_read_point(&point, 0u) == 0u);
    assert(ST7102_host_test_read_point(NULL, 1u) == 0u);
    assert(read_transfer_count == 0u);
}

static void test_fallback_releases_unread_but_writable_slot(void)
{
    struct guarded_buffer
    {
        struct rt_touch_data points[5];
        unsigned char canary[CANARY_BYTES];
    } guarded;

    ST7102_host_test_reset();
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));

    scenario = I2C_TOUCH_SLOT4_80;
    assert(ST7102_host_test_read_point(guarded.points, 5u) == 1u);
    assert(guarded.points[4].event == RT_TOUCH_EVENT_DOWN);

    memset(guarded.points, 0, sizeof(guarded.points));
    scenario = I2C_FALLBACK_EMPTY_32;
    assert(ST7102_host_test_read_point(guarded.points, 5u) == 0u);
    assert(guarded.points[4].event == RT_TOUCH_EVENT_UP);
    assert_canary(guarded.canary);
}

static void test_hardware_limit_does_not_overwrite_ten_slots(void)
{
    struct guarded_buffer
    {
        struct rt_touch_data points[ST7102_MAX_TOUCH];
        unsigned char canary[CANARY_BYTES];
    } guarded;

    ST7102_host_test_reset();
    scenario = I2C_EMPTY_80;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));

    assert(ST7102_host_test_read_point(guarded.points, 11u) == 0u);
    assert_canary(guarded.canary);
}

static void test_second_fallback_to_sixteen_bytes_stays_in_bounds(void)
{
    struct guarded_buffer
    {
        struct rt_touch_data points[5];
        unsigned char canary[CANARY_BYTES];
    } guarded;

    ST7102_host_test_reset();
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));

    scenario = I2C_FALLBACK_EMPTY_32;
    assert(ST7102_host_test_read_point(guarded.points, 5u) == 0u);
    scenario = I2C_FALLBACK_EMPTY_16;
    assert(ST7102_host_test_read_point(guarded.points, 5u) == 0u);
    assert_canary(guarded.canary);
}

static void test_invalid_slot_with_stale_intensity_is_not_reported(void)
{
    struct rt_touch_data points[5];

    ST7102_host_test_reset();
    scenario = I2C_INVALID_STALE_SLOT_80;
    memset(points, 0, sizeof(points));

    assert(ST7102_host_test_read_point(points, 5u) == 0u);
    assert(points[2].event == RT_TOUCH_EVENT_NONE);
}

static void test_two_valid_points_keep_distinct_slot_ids(void)
{
    struct rt_touch_data points[5];

    ST7102_host_test_reset();
    scenario = I2C_TWO_VALID_TOUCHES_80;
    memset(points, 0, sizeof(points));

    assert(ST7102_host_test_read_point(points, 5u) == 2u);
    assert(points[0].event == RT_TOUCH_EVENT_DOWN);
    assert(points[0].track_id == 0u);
    assert(points[3].event == RT_TOUCH_EVENT_DOWN);
    assert(points[3].track_id == 3u);
}

static void test_invalid_next_frame_releases_previous_points(void)
{
    struct rt_touch_data points[5];

    ST7102_host_test_reset();
    scenario = I2C_TWO_VALID_TOUCHES_80;
    memset(points, 0, sizeof(points));
    assert(ST7102_host_test_read_point(points, 5u) == 2u);

    scenario = I2C_INVALID_STALE_SLOT_80;
    memset(points, 0, sizeof(points));
    assert(ST7102_host_test_read_point(points, 5u) == 0u);
    assert(points[0].event == RT_TOUCH_EVENT_UP);
    assert(points[2].event == RT_TOUCH_EVENT_NONE);
    assert(points[3].event == RT_TOUCH_EVENT_UP);
}

int main(void)
{
    test_one_point_buffer_is_not_overwritten();
    test_zero_capacity_and_null_buffer_do_not_read_i2c();
    test_fallback_releases_unread_but_writable_slot();
    test_hardware_limit_does_not_overwrite_ten_slots();
    test_second_fallback_to_sixteen_bytes_stays_in_bounds();
    test_invalid_slot_with_stale_intensity_is_not_reported();
    test_two_valid_points_keep_distinct_slot_ids();
    test_invalid_next_frame_releases_previous_points();
    puts("touch_driver: PASS");
    return 0;
}
