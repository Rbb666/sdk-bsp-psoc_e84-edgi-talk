#ifndef TEST_FAKE_RTDEVICE_H
#define TEST_FAKE_RTDEVICE_H

#include "rtthread.h"

#define RT_I2C_WR 0x00u
#define RT_I2C_RD 0x01u
#define RT_DEVICE_FLAG_RDWR 0x03u
#define RT_DEVICE_FLAG_INT_RX 0x100u

#define PIN_LOW 0
#define PIN_HIGH 1
#define PIN_MODE_OUTPUT 1
#define PIN_MODE_INPUT_PULLUP 2
#define PIN_MODE_INPUT_PULLDOWN 3

#define RT_TOUCH_EVENT_NONE 0
#define RT_TOUCH_EVENT_UP 1
#define RT_TOUCH_EVENT_DOWN 2
#define RT_TOUCH_EVENT_MOVE 3
#define RT_TOUCH_TYPE_CAPACITANCE 1
#define RT_TOUCH_VENDOR_GT 1
#define RT_TOUCH_CTRL_GET_INFO 1

struct rt_i2c_bus_device
{
    int unused;
};

struct rt_i2c_client
{
    rt_uint16_t client_addr;
    struct rt_i2c_bus_device *bus;
};

struct rt_i2c_msg
{
    rt_uint16_t addr;
    rt_uint16_t flags;
    rt_uint8_t *buf;
    rt_uint16_t len;
};

struct rt_touch_data
{
    rt_uint8_t event;
    rt_uint8_t track_id;
    rt_uint8_t width;
    rt_uint16_t x_coordinate;
    rt_uint16_t y_coordinate;
    rt_tick_t timestamp;
};

struct rt_touch_info
{
    rt_uint16_t range_x;
    rt_uint16_t range_y;
    rt_uint8_t point_num;
    rt_uint8_t type;
    rt_uint8_t vendor;
};

struct rt_touch_config
{
    const char *dev_name;
    struct
    {
        rt_base_t pin;
        rt_uint8_t mode;
    } irq_pin;
    void *user_data;
};

struct rt_touch_device;
struct rt_touch_ops
{
    rt_size_t (*touch_readpoint)(struct rt_touch_device *, void *, rt_size_t);
    rt_err_t (*touch_control)(struct rt_touch_device *, int, void *);
};

struct rt_touch_device
{
    struct rt_touch_info info;
    struct rt_touch_config config;
    const struct rt_touch_ops *ops;
};

typedef void *rt_device_t;
typedef struct rt_touch_device *rt_touch_t;

int rt_i2c_transfer(struct rt_i2c_bus_device *bus,
                    struct rt_i2c_msg messages[], int count);
void rt_pin_write(rt_base_t pin, rt_base_t value);
int rt_pin_read(rt_base_t pin);
void rt_pin_mode(rt_base_t pin, rt_base_t mode);
rt_device_t rt_device_find(const char *name);
rt_err_t rt_device_open(rt_device_t device, rt_uint16_t flags);
int rt_hw_touch_register(rt_touch_t touch, const char *name,
                         rt_uint32_t flags, void *data);

#endif
