#include "retro_go_gpsp_prelude.h"
#include "retro_go_platform.h"

#ifdef BSP_RETRO_GO_GBA_FAST_RAM_WRITE_INLINE
#error "Select either the inline or linker-wrapper gpSP RAM-write path"
#endif

/* GNU ld --wrap redirects the portable interpreter's external references
 * here without changing the upstream gpSP snapshot. Calls originating inside
 * gba_memory.c remain bound to the original implementation. */
extern cpu_alert_type function_cc __real_write_memory8(u32 address, u8 value);
extern cpu_alert_type function_cc __real_write_memory16(u32 address, u16 value);
extern cpu_alert_type function_cc __real_write_memory32(u32 address, u32 value);

RETRO_GO_ITCM cpu_alert_type function_cc
__wrap_write_memory8(u32 address, u8 value)
{
    const u32 region = address >> 24;

    if (region == 0x02u)
    {
        address8(ewram, address & 0x3ffffu) = eswap8(value);
        return CPU_ALERT_NONE;
    }
    if (region == 0x03u)
    {
        address8(iwram, (address & 0x7fffu) + 0x8000u) = value;
        return CPU_ALERT_NONE;
    }
    return __real_write_memory8(address, value);
}

RETRO_GO_ITCM cpu_alert_type function_cc
__wrap_write_memory16(u32 address, u16 value)
{
    const u32 region = address >> 24;

    if (region == 0x02u)
    {
        address16(ewram, address & 0x3ffffu) = eswap16(value);
        return CPU_ALERT_NONE;
    }
    if (region == 0x03u)
    {
        address16(iwram, (address & 0x7fffu) + 0x8000u) = eswap16(value);
        return CPU_ALERT_NONE;
    }
    return __real_write_memory16(address, value);
}

RETRO_GO_ITCM cpu_alert_type function_cc
__wrap_write_memory32(u32 address, u32 value)
{
    const u32 region = address >> 24;

    if (region == 0x02u)
    {
        address32(ewram, address & 0x3ffffu) = eswap32(value);
        return CPU_ALERT_NONE;
    }
    if (region == 0x03u)
    {
        address32(iwram, (address & 0x7fffu) + 0x8000u) = eswap32(value);
        return CPU_ALERT_NONE;
    }
    return __real_write_memory32(address, value);
}
