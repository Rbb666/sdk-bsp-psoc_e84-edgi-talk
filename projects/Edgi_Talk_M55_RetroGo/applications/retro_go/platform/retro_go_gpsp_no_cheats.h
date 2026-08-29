#ifndef RETRO_GO_GPSP_NO_CHEATS_H
#define RETRO_GO_GPSP_NO_CHEATS_H

/* The standalone launcher has no cheat-code loader. Include gpSP's public
 * declarations with their original C linkage, then turn only cpu.cc's two
 * per-instruction hook checks into compile-time no-ops. The upstream files
 * and the vblank-side cheat_clear() lifecycle remain untouched. */
#ifdef __cplusplus
extern "C" {
#endif
#include "common.h"
#ifdef __cplusplus
}
#endif

#define cheat_master_hook (0xffffffffu)
#define process_cheats() ((void)0)

#ifdef BSP_RETRO_GO_GBA_FAST_RAM_WRITE_INLINE
static __attribute__((always_inline)) inline cpu_alert_type
retro_go_gpsp_write_memory8(u32 address, u8 value)
{
    if ((address >> 24) == 0x02u)
    {
        address8(ewram, address & 0x3ffffu) = value;
        return CPU_ALERT_NONE;
    }
    if ((address >> 24) == 0x03u)
    {
        address8(iwram, (address & 0x7fffu) + 0x8000u) = value;
        return CPU_ALERT_NONE;
    }
    return write_memory8(address, value);
}

static __attribute__((always_inline)) inline cpu_alert_type
retro_go_gpsp_write_memory16(u32 address, u16 value)
{
    if ((address >> 24) == 0x02u)
    {
        address16(ewram, address & 0x3ffffu) = eswap16(value);
        return CPU_ALERT_NONE;
    }
    if ((address >> 24) == 0x03u)
    {
        address16(iwram, (address & 0x7fffu) + 0x8000u) = eswap16(value);
        return CPU_ALERT_NONE;
    }
    return write_memory16(address, value);
}

static __attribute__((always_inline)) inline cpu_alert_type
retro_go_gpsp_write_memory32(u32 address, u32 value)
{
    if ((address >> 24) == 0x02u)
    {
        address32(ewram, address & 0x3ffffu) = eswap32(value);
        return CPU_ALERT_NONE;
    }
    if ((address >> 24) == 0x03u)
    {
        address32(iwram, (address & 0x7fffu) + 0x8000u) = eswap32(value);
        return CPU_ALERT_NONE;
    }
    return write_memory32(address, value);
}

#define write_memory8 retro_go_gpsp_write_memory8
#define write_memory16 retro_go_gpsp_write_memory16
#define write_memory32 retro_go_gpsp_write_memory32
#endif

#endif
