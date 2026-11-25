/* SPDX-License-Identifier: Apache-2.0 */
#include "spine_display.h"

#include <rtdevice.h>

#include "board.h"
#include "cycfg_peripherals.h"
#include "cy_graphics.h"
#include "display_tl043wvv02.h"
#include "drv_touch.h"
#include "vg_lite_platform.h"
#include "vg_lite_kernel.h"
#include "vg_lite_hal.h"
#include "vg_lite_hw.h"

#define DBG_TAG "spine.display"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define SCANOUT_STRIDE_BYTES (512U * sizeof(rt_uint16_t))
#define SCANOUT_BYTES        (SCANOUT_STRIDE_BYTES * SPINE_DISPLAY_HEIGHT)
#define SCANOUT_COUNT        2U
#define SCANOUT_TIMEOUT_MS   50

#define GPU_COMMAND_BYTES  (64U * 1024U)
#define GPU_TESS_WIDTH     (SPINE_DISPLAY_WIDTH / 4U)
#define GPU_TESS_HEIGHT    (SPINE_DISPLAY_HEIGHT / 4U)
#define GPU_HEAP_BYTES     (184U * 1024U)  /* Two command buffers and the tessellation workspace. */
#define GPU_TIMEOUT_MS     250U
#define GPU_AXI_BUS_ERROR  (1UL << 31)

/* The CM55 board MPU maps gfx_mem as non-cacheable for GPU/DC access. */
CY_SECTION(".cy_gpu_buf") CY_ALIGN(64)
static uint32_t framebuffers[SCANOUT_COUNT][SCANOUT_BYTES / sizeof(uint32_t)];

CY_SECTION(".cy_gpu_buf") CY_ALIGN(64)
static rt_uint8_t gpu_heap[GPU_HEAP_BYTES];

static vg_lite_buffer_t targets[SCANOUT_COUNT];
static cy_stc_gfx_context_t gfx_context;
static struct rt_semaphore scanout_done;
static struct rt_event gpu_events;
static unsigned int front_buffer;
static rt_bool_t display_ready;
static rt_bool_t gpu_finished;
static rt_bool_t flip_pending;
static rt_bool_t backlight_on;
static volatile rt_bool_t waiting_for_frame;

static rt_device_t touch_device;
static struct spine_touch last_touch;

static rt_uint32_t acknowledge_display_irq(void)
{
    const rt_uint32_t status = GFXSS->GFXSS_DC.DCNANO.GCREGDISPLAYINTR;

    /* Reading the core status clears it. Acknowledge the wrapper separately. */
    GFXSS->GFXSS_DC.MXDC.INTR = GFXSS_DC_MXDC_INTR_CORE_Msk;
    __DSB();
    return status;
}

static void display_isr(void)
{
    rt_interrupt_enter();
    if ((acknowledge_display_irq() & GFXSS_DC_DCNANO_GCREGDISPLAYINTR_GCREGDISPLAYINTR_DISP0_Msk) &&
        waiting_for_frame)
    {
        NVIC_DisableIRQ(GFXSS_DC_IRQ);
        waiting_for_frame = RT_FALSE;
        rt_sem_release(&scanout_done);
    }
    rt_interrupt_leave();
}

static void gpu_isr(void)
{
    rt_interrupt_enter();
    const rt_uint32_t flags = vg_lite_hal_peek(VG_LITE_INTR_STATUS);
    Cy_GFXSS_Clear_GPU_Interrupt(GFXSS, &gfx_context);
    __DSB();
    if (flags != 0u)
    {
        rt_event_send(&gpu_events, flags);
    }
    rt_interrupt_leave();
}

vg_lite_error_t __real_vg_lite_kernel(vg_lite_kernel_command_t command, void *data);

vg_lite_error_t __wrap_vg_lite_kernel(vg_lite_kernel_command_t command, void *data)
{
    if (command == VG_LITE_SUBMIT)
    {
        /* Complete command-buffer writes before the GPU fetch starts. */
        __DSB();
    }
    return __real_vg_lite_kernel(command, data);
}

/* The VG-Lite kernel calls this hook in render-thread context. The event also
 * preserves a completion that arrives before the thread starts waiting. */
int32_t __wrap_vg_lite_hal_wait_interrupt(uint32_t timeout, uint32_t mask, uint32_t *value)
{
    const uint32_t wait_ms = timeout < GPU_TIMEOUT_MS ? timeout : GPU_TIMEOUT_MS;
    const rt_tick_t ticks = rt_tick_from_millisecond(wait_ms);
    rt_uint32_t flags = 0;
    const rt_err_t result = rt_event_recv(&gpu_events, mask | GPU_AXI_BUS_ERROR,
                                         RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, ticks, &flags);
    *value = flags;
    return result == RT_EOK && (flags & GPU_AXI_BUS_ERROR) == 0u;
}

static rt_err_t init_graphics(void)
{
    cy_stc_gfx_config_t config = GFXSS_config;
    cy_stc_gfx_dc_config_t display = GFXSS_dc_config;
    cy_stc_gfx_layer_config_t layer = GFXSS_graphics_layer;
    cy_stc_mipidsi_config_t dsi = GFXSS_mipi_dsi_config;
    cy_stc_mipidsi_display_params_t timing = {
        .pixel_clock = 33984,
        .hdisplay = SPINE_DISPLAY_WIDTH,
        .hsync_width = 2,
        .hfp = 86,
        .hbp = 87,
        .vdisplay = SPINE_DISPLAY_HEIGHT,
        .vsync_width = 2,
        .vfp = 182,
        .vbp = 8,
    };
    cy_en_gfx_status_t error;

    layer.buffer_address = (gctADDRESS *)framebuffers[0];
    layer.width = SPINE_DISPLAY_WIDTH;
    layer.height = SPINE_DISPLAY_HEIGHT;
    display.gfx_layer_config = &layer;
    display.ovl0_layer_config = RT_NULL;
    display.ovl1_layer_config = RT_NULL;
    /* The Edgi-Talk panel needs the CFG1 RGB565 output bit mapping. */
    display.display_format = vivD16CFG1;
    display.display_width = SPINE_DISPLAY_WIDTH;
    display.display_height = SPINE_DISPLAY_HEIGHT;
    dsi.per_lane_mbps = 900;
    dsi.display_params = &timing;
    config.dc_cfg = &display;
    config.mipi_dsi_cfg = &dsi;

    error = Cy_GFXSS_Init(GFXSS, &config, &gfx_context);
    if (error != CY_GFX_SUCCESS)
    {
        LOG_E("GFXSS init failed: 0x%x", (unsigned int)error);
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t init_panel(void)
{
    static mtb_display_tl043wvv02_pin_config_t pins = {
        .reset_port = GPIO_PRT20,
        .reset_pin = 7U,
    };
    cy_en_mipidsi_status_t error;

    error = mtb_display_tl043wvv02_init_without_display_on(GFXSS_GFXSS_MIPIDSI, &pins);
    if (error != CY_MIPIDSI_SUCCESS)
    {
        LOG_E("LCD init failed: 0x%x", (unsigned int)error);
        return -RT_ERROR;
    }

    GFXSS->GFXSS_DC.DCNANO.GCREGFRAMEBUFFERSTRIDE = SCANOUT_STRIDE_BYTES;
    GFXSS->GFXSS_DC.DCNANO.GCREGDISPLAYINTRENABLE =
        GFXSS_DC_DCNANO_GCREGDISPLAYINTRENABLE_GCREGDISPLAYINTRENABLE_DISP0_Msk;
    GFXSS->GFXSS_DC.MXDC.INTR_MASK = GFXSS_DC_MXDC_INTR_MASK_CORE_MASK_Msk;
    if (Cy_GFXSS_Set_FrameBuffer(GFXSS, framebuffers[0], &gfx_context) != CY_GFX_SUCCESS)
    {
        LOG_E("Initial scanout bind failed");
        return -RT_ERROR;
    }

    error = mtb_display_tl043wvv02_display_on(GFXSS_GFXSS_MIPIDSI);
    if (error != CY_MIPIDSI_SUCCESS)
    {
        LOG_E("LCD display-on failed: 0x%x", (unsigned int)error);
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t init_gpu(void)
{
    vg_module_parameters_t memory = {
        .register_mem_base = (rt_uint32_t)GFXSS_GFXSS_GPU_GCNANO,
        .gpu_mem_base = {0U},
        .contiguous_mem_base = {gpu_heap},
        .contiguous_mem_size = {sizeof(gpu_heap)},
    };
    vg_lite_error_t error;

    vg_lite_init_mem(&memory);
    error = vg_lite_set_command_buffer_size(GPU_COMMAND_BYTES);
    if (error != VG_LITE_SUCCESS)
    {
        LOG_E("VG-Lite command buffer setup failed: %d", error);
        return -RT_ERROR;
    }
    error = vg_lite_init(GPU_TESS_WIDTH, GPU_TESS_HEIGHT);
    if (error != VG_LITE_SUCCESS)
    {
        LOG_E("VG-Lite init failed: %d", error);
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t spine_display_init(void)
{
    const cy_stc_sysint_t display_irq = {.intrSrc = GFXSS_DC_IRQ, .intrPriority = 3U};
    const cy_stc_sysint_t gpu_irq = {.intrSrc = GFXSS_GPU_IRQ, .intrPriority = 3U};
    rt_err_t error;

    if (display_ready)
    {
        return RT_EOK;
    }
    for (unsigned int index = 0; index < SCANOUT_COUNT; ++index)
    {
        targets[index] = (vg_lite_buffer_t){
            .width = SPINE_DISPLAY_WIDTH,
            .height = SPINE_DISPLAY_HEIGHT,
            .stride = SCANOUT_STRIDE_BYTES,
            .tiled = VG_LITE_LINEAR,
            /* VG-Lite's BGR565 stores red in the high bits expected by the DC. */
            .format = VG_LITE_BGR565,
            .memory = framebuffers[index],
            .address = (rt_uint32_t)(uintptr_t)framebuffers[index],
            .premultiplied = 1U,
            .image_mode = VG_LITE_NORMAL_IMAGE_MODE,
            .transparency_mode = VG_LITE_IMAGE_OPAQUE,
        };
    }

    rt_memset(framebuffers[0], 0, SCANOUT_BYTES);
    __DSB();
    error = rt_sem_init(&scanout_done, "spine.dc", 0, RT_IPC_FLAG_FIFO);
    if (error != RT_EOK)
    {
        return error;
    }
    error = rt_event_init(&gpu_events, "spine.gpu", RT_IPC_FLAG_FIFO);
    if (error != RT_EOK)
    {
        rt_sem_detach(&scanout_done);
        return error;
    }
    if (init_graphics() != RT_EOK)
    {
        rt_event_detach(&gpu_events);
        rt_sem_detach(&scanout_done);
        return -RT_ERROR;
    }
    if (Cy_SysInt_Init(&display_irq, display_isr) != CY_SYSINT_SUCCESS ||
        Cy_SysInt_Init(&gpu_irq, gpu_isr) != CY_SYSINT_SUCCESS)
    {
        LOG_E("GFXSS interrupt setup failed");
        goto fail;
    }

    Cy_GFXSS_Enable_GPU_Interrupt(GFXSS);
    NVIC_EnableIRQ(GFXSS_GPU_IRQ);
    if (init_panel() != RT_EOK || init_gpu() != RT_EOK)
    {
        goto fail;
    }

    display_ready = RT_TRUE;
    return RT_EOK;

fail:
    NVIC_DisableIRQ(GFXSS_GPU_IRQ);
    (void)Cy_GFXSS_DeInit(GFXSS, &gfx_context);
    rt_event_detach(&gpu_events);
    rt_sem_detach(&scanout_done);
    return -RT_ERROR;
}

static rt_err_t wait_for_flip(void)
{
    if (flip_pending)
    {
        rt_err_t error = rt_sem_take(&scanout_done, rt_tick_from_millisecond(SCANOUT_TIMEOUT_MS));

        if (error != RT_EOK)
        {
            return error;
        }
        front_buffer ^= 1U;
        flip_pending = RT_FALSE;
        if (!backlight_on)
        {
            spine_board_backlight_on();
            backlight_on = RT_TRUE;
        }
    }
    return RT_EOK;
}

vg_lite_buffer_t *spine_display_back_buffer(void)
{
    if (!display_ready || wait_for_flip() != RT_EOK)
    {
        return RT_NULL;
    }
    gpu_finished = RT_FALSE;
    return &targets[front_buffer ^ 1U];
}

rt_err_t spine_display_submit(void)
{
    vg_lite_error_t gpu_error;

    if (!display_ready || flip_pending)
    {
        return -RT_ERROR;
    }
    gpu_finished = RT_FALSE;
    gpu_error = vg_lite_flush();
    if (gpu_error != VG_LITE_SUCCESS)
    {
        LOG_E("VG-Lite frame submit failed: %d", gpu_error);
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t spine_display_wait_gpu(void)
{
    vg_lite_error_t gpu_error;

    if (!display_ready || flip_pending)
    {
        return -RT_ERROR;
    }
    gpu_finished = RT_FALSE;
    gpu_error = vg_lite_finish();
    if (gpu_error != VG_LITE_SUCCESS)
    {
        LOG_E("VG-Lite frame fence failed: %d", gpu_error);
        return -RT_ERROR;
    }
    __DSB();
    gpu_finished = RT_TRUE;
    return RT_EOK;
}

rt_err_t spine_display_flip(void)
{
    rt_base_t interrupt_level;
    cy_en_gfx_status_t status;

    if (!display_ready)
    {
        return -RT_ERROR;
    }
    if (flip_pending)
    {
        return wait_for_flip();
    }
    if (!gpu_finished)
    {
        return -RT_EBUSY;
    }

    interrupt_level = rt_hw_interrupt_disable();
    status = Cy_GFXSS_Set_FrameBuffer(GFXSS, framebuffers[front_buffer ^ 1U], &gfx_context);
    if (status == CY_GFX_SUCCESS)
    {
        /* Discard old frame signals. The next frame-done frees the old front buffer. */
        (void)acknowledge_display_irq();
        NVIC_ClearPendingIRQ(GFXSS_DC_IRQ);
        waiting_for_frame = RT_TRUE;
        gpu_finished = RT_FALSE;
        flip_pending = RT_TRUE;
        NVIC_EnableIRQ(GFXSS_DC_IRQ);
    }
    rt_hw_interrupt_enable(interrupt_level);

    return status == CY_GFX_SUCCESS ? wait_for_flip() : -RT_ERROR;
}

rt_err_t spine_touch_init(void)
{
    static rt_uint8_t reset_pin = ST7102_RST_PIN;
    struct rt_touch_config config = {
        .dev_name = "i2c1",
        .irq_pin = {.pin = ST7102_IRQ_PIN, .mode = PIN_MODE_INPUT_PULLUP},
        .user_data = &reset_pin,
    };
    rt_device_t device;
    rt_err_t error;

    if (touch_device != RT_NULL)
    {
        return RT_EOK;
    }
    device = rt_device_find("ST7102");
    if (device == RT_NULL)
    {
        error = rt_hw_ST7102_init("ST7102", &config);
        if (error != RT_EOK)
        {
            return error;
        }
        device = rt_device_find("ST7102");
        if (device == RT_NULL)
        {
            return -RT_ENOSYS;
        }
    }

    error = rt_device_open(device, RT_DEVICE_OFLAG_RDONLY);
    if (error == RT_EOK)
    {
        touch_device = device;
    }
    return error;
}

void spine_touch_read(struct spine_touch *touch)
{
    struct rt_touch_data points[ST7102_MAX_TOUCH] = {0};

    if (touch_device != RT_NULL && (last_touch.pressed || rt_pin_read(ST7102_IRQ_PIN) == PIN_LOW))
    {
        last_touch.pressed = RT_FALSE;
        if (rt_device_read(touch_device, 0, points, ST7102_MAX_TOUCH) > 0)
        {
            for (rt_size_t index = 0; index < ST7102_MAX_TOUCH; ++index)
            {
                if (points[index].event == RT_TOUCH_EVENT_DOWN || points[index].event == RT_TOUCH_EVENT_MOVE)
                {
                    last_touch.x = points[index].x_coordinate < SPINE_DISPLAY_WIDTH
                        ? points[index].x_coordinate : SPINE_DISPLAY_WIDTH - 1;
                    last_touch.y = points[index].y_coordinate < SPINE_DISPLAY_HEIGHT
                        ? points[index].y_coordinate : SPINE_DISPLAY_HEIGHT - 1;
                    last_touch.pressed = RT_TRUE;
                    break;
                }
            }
        }
    }
    *touch = last_touch;
}
