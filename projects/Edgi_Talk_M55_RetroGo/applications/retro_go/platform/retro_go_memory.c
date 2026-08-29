#include "retro_go_memory.h"

#include "cy_pdl.h"
#ifdef TYPE
#undef TYPE
#endif
#include "cy_cmsis_utils.h"

#include <rthw.h>
#include <rtthread.h>

#define RETRO_GO_HYPERRAM_HEAP_START 0x64400000u
#define RETRO_GO_HYPERRAM_HEAP_END   0x64bfffffu
#define RETRO_GO_HYPERRAM_SHARED_END 0x64ffffffu
#define RETRO_GO_M55_SOCMEM_START     0x26060000u
#define RETRO_GO_M55_SOCMEM_END       0x261effffu
#define RETRO_GO_SHARED_SOCMEM_START  0x261f0000u
#define RETRO_GO_SHARED_SOCMEM_END    0x261fffffu
#define RETRO_GO_GBA_FRAMEBUFFER_BYTES (240u * 161u * sizeof(uint16_t))

#if defined(BSP_RETRO_GO_HYPERRAM_WRITEBACK_CACHE) && \
    (!defined(BSP_USING_HYPERAM_SIZE) || BSP_USING_HYPERAM_SIZE != 0x800000)
#error "Retro-Go HyperRAM WB MPU layout requires an 8 MiB private heap"
#endif

extern uint32_t SystemCoreClock;
extern uint8_t __HeapBase;
extern uint8_t __HeapLimit;
extern uint8_t __retro_go_gba_framebuffer_start__;
extern uint8_t __retro_go_gba_framebuffer_end__;

static void log_clock_tree(void)
{
    rt_kprintf("[retro-go] clocks: SystemCore=%u Hz HF0=%u Hz HF1=%u Hz "
               "HF4=%u Hz\n",
               (unsigned)SystemCoreClock,
               (unsigned)Cy_SysClk_ClkHfGetFrequency(0u),
               (unsigned)Cy_SysClk_ClkHfGetFrequency(1u),
               (unsigned)Cy_SysClk_ClkHfGetFrequency(4u));
}

void retro_go_memory_prepare(void)
{
    log_clock_tree();
    rt_kprintf("[retro-go] SOCMEM: M55=%08x-%08x heap=%p-%p (%u KiB), "
               "M33/M55 shared=%08x-%08x (64 KiB)\n",
               RETRO_GO_M55_SOCMEM_START, RETRO_GO_M55_SOCMEM_END,
               &__HeapBase, &__HeapLimit,
               (unsigned)(((uintptr_t)&__HeapLimit -
                           (uintptr_t)&__HeapBase) / 1024u),
               RETRO_GO_SHARED_SOCMEM_START, RETRO_GO_SHARED_SOCMEM_END);

#ifdef BSP_RETRO_GO_HYPERRAM_WRITEBACK_CACHE
    const uint32_t region_count =
        (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
    bool dcache_was_enabled = rt_hw_cpu_dcache_status() != 0;
    const cy_stc_mpu_config_t regions[] =
    {
        {
            .reg_num = 0u,
            .base_addr = RETRO_GO_HYPERRAM_HEAP_START,
            .end_addr = RETRO_GO_HYPERRAM_HEAP_END,
            .writable = true,
            .require_privileged = false,
            .shareable = false,
            .executable = false,
            .cacheable = ARM_MPU_ATTR_MEMORY_(1u, 1u, 1u, 1u),
            .is_device = false,
            .device_attrs = 0u,
        },
        {
            .reg_num = 1u,
            .base_addr = 0x240fd000u,
            .end_addr = 0x240fffffu,
            .writable = true,
            .require_privileged = false,
            .shareable = false,
            .executable = false,
            .cacheable = ARM_MPU_ATTR_NON_CACHEABLE,
            .is_device = false,
            .device_attrs = 0u,
        },
        {
            .reg_num = 2u,
            .base_addr = RETRO_GO_SHARED_SOCMEM_START,
            .end_addr = RETRO_GO_SHARED_SOCMEM_END,
            .writable = true,
            .require_privileged = false,
            .shareable = false,
            .executable = false,
            .cacheable = ARM_MPU_ATTR_NON_CACHEABLE,
            .is_device = false,
            .device_attrs = 0u,
        },
        {
            .reg_num = 3u,
            .base_addr = 0x26200000u,
            .end_addr = 0x264fffffu,
            .writable = true,
            .require_privileged = false,
            .shareable = false,
            .executable = false,
            .cacheable = ARM_MPU_ATTR_NON_CACHEABLE,
            .is_device = false,
            .device_attrs = 0u,
        },
        {
            .reg_num = 4u,
            .base_addr = RETRO_GO_HYPERRAM_HEAP_END + 1u,
            .end_addr = RETRO_GO_HYPERRAM_SHARED_END,
            .writable = true,
            .require_privileged = false,
            .shareable = false,
            .executable = false,
            .cacheable = ARM_MPU_ATTR_NON_CACHEABLE,
            .is_device = false,
            .device_attrs = 0u,
        },
#ifdef BSP_RETRO_GO_GBA_FRAMEBUFFER_CACHEABLE
        {
            .reg_num = 5u,
            .base_addr = (uintptr_t)&__retro_go_gba_framebuffer_start__,
            .end_addr = (uintptr_t)&__retro_go_gba_framebuffer_end__ - 1u,
            .writable = true,
            .require_privileged = false,
            .shareable = false,
            .executable = false,
            .cacheable = ARM_MPU_ATTR_MEMORY_(1u, 1u, 1u, 1u),
            .is_device = false,
            .device_attrs = 0u,
        },
#endif
    };

    const uint32_t required_regions =
#ifdef BSP_RETRO_GO_GBA_FRAMEBUFFER_CACHEABLE
        6u;
#else
        5u;
#endif

    if (region_count < required_regions)
    {
        rt_kprintf("[retro-go] MPU has only %u data regions; HyperRAM cache "
                   "override skipped\n", (unsigned)region_count);
        return;
    }
    if (dcache_was_enabled)
    {
        rt_hw_cpu_dcache_disable();
    }
    Cy_MPU_Init(regions, (uint8_t)(sizeof(regions) / sizeof(regions[0])));
    if (dcache_was_enabled)
    {
        rt_hw_cpu_dcache_enable();
    }
    MPU->RNR = 0u;
    __DSB();
    __ISB();
    rt_kprintf("[retro-go] MPU HyperRAM: R0=%08x/%08x MAIR0=%08x "
               "64400000-64bfffff WB/RA/WA, shared tail non-cacheable\n",
               (unsigned)MPU->RBAR, (unsigned)MPU->RLAR,
               (unsigned)MPU->MAIR[0]);
#ifdef BSP_RETRO_GO_GBA_FRAMEBUFFER_CACHEABLE
    MPU->RNR = 5u;
    __DSB();
    __ISB();
    rt_kprintf("[retro-go] MPU GBA framebuffer: R5=%08x/%08x MAIR1=%08x "
               "%p-%p WB/RA/WA (%u bytes)\n",
               (unsigned)MPU->RBAR, (unsigned)MPU->RLAR,
               (unsigned)MPU->MAIR[1],
               &__retro_go_gba_framebuffer_start__,
               &__retro_go_gba_framebuffer_end__,
               (unsigned)((uintptr_t)&__retro_go_gba_framebuffer_end__ -
                          (uintptr_t)&__retro_go_gba_framebuffer_start__));
    if ((uintptr_t)&__retro_go_gba_framebuffer_end__ -
            (uintptr_t)&__retro_go_gba_framebuffer_start__ !=
        RETRO_GO_GBA_FRAMEBUFFER_BYTES)
    {
        rt_kprintf("[retro-go] GBA framebuffer MPU size mismatch\n");
    }
    MPU->RNR = 0u;
#endif
#endif
}
