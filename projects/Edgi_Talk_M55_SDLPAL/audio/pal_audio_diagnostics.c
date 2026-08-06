#include "pal_audio_diagnostics.h"

#include <finsh.h>
#include <rtthread.h>

static int pal_audio(int argc, char **argv)
{
    pal_audio_diagnostics_t audio;

    (void)argc;
    (void)argv;
    pal_audio_diagnostics_get(&audio);
    rt_kprintf("\n[PAL AUDIO]\n");
    rt_kprintf(
        "  state opened=%u music=%u sound=%u track=%d rix=%u\n",
        audio.opened, audio.music_enabled, audio.sound_enabled,
        audio.current_music, audio.rix_playing);
    rt_kprintf(
        "  blocks rendered=%lu written=%lu short=%lu silence=%lu underruns=%lu\n",
        (unsigned long)audio.rendered_blocks,
        (unsigned long)audio.written_blocks,
        (unsigned long)audio.short_writes,
        (unsigned long)audio.silence_recoveries,
        (unsigned long)audio.hardware_underruns);
    rt_kprintf(
        "  timing render_last_us=%lu render_max_us=%lu write_last_us=%lu write_max_us=%lu\n",
        (unsigned long)audio.render_last_us,
        (unsigned long)audio.render_max_us,
        (unsigned long)audio.write_last_us,
        (unsigned long)audio.write_max_us);
    rt_kprintf(
        "  driver tx=%lu rx=%lu complete=%lu irq=%lu sem=%lu mq_fail=%lu\n",
        (unsigned long)audio.driver_tx_messages,
        (unsigned long)audio.driver_rx_messages,
        (unsigned long)audio.driver_completion_requests,
        (unsigned long)audio.driver_fifo_irqs,
        (unsigned long)audio.driver_sem_releases,
        (unsigned long)audio.driver_mq_send_failures);
    rt_kprintf(
        "  voices active=%lu peak=%lu replaced=%lu rejected=%lu\n",
        (unsigned long)audio.active_voices,
        (unsigned long)audio.peak_voices,
        (unsigned long)audio.replaced_voices,
        (unsigned long)audio.rejected_voices);
    rt_kprintf(
        "  queue sound_drops=%lu music_coalesces=%lu stale=%lu release_overflows=%lu\n",
        (unsigned long)audio.sound_drops,
        (unsigned long)audio.music_coalesces,
        (unsigned long)audio.stale_music_commands,
        (unsigned long)audio.release_overflows);
    rt_kprintf(
        "  cache current=%lu peak=%lu hit=%lu miss=%lu evict=%lu fail=%lu\n",
        (unsigned long)audio.cache_current_bytes,
        (unsigned long)audio.cache_peak_bytes,
        (unsigned long)audio.cache_hits,
        (unsigned long)audio.cache_misses,
        (unsigned long)audio.cache_evictions,
        (unsigned long)audio.cache_failures);
    rt_kprintf(
        "  rix ticks=%lu loops=%lu failed=%lu\n",
        (unsigned long)audio.rendered_rix_ticks,
        (unsigned long)audio.completed_rix_loops,
        (unsigned long)audio.failed_rix_tracks);
    rt_kprintf("  audio stack used_peak=%lu total=%lu\n",
               (unsigned long)audio.audio_stack_used_bytes,
               (unsigned long)audio.audio_stack_total_bytes);
    return 0;
}
MSH_CMD_EXPORT(pal_audio, Show SDLPal DOS audio state and performance);
