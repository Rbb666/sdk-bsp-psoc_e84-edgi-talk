#ifndef PAL_AUDIO_PORT_H
#define PAL_AUDIO_PORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pal_audio_render_fn)(void *context, int16_t *samples,
                                    size_t sample_count);

typedef struct pal_audio_port_metrics
{
    uint32_t rendered_blocks;
    uint32_t written_blocks;
    uint32_t short_writes;
    uint32_t silence_recoveries;
    uint32_t render_last_us;
    uint32_t render_max_us;
    uint32_t write_last_us;
    uint32_t write_max_us;
    uint32_t hardware_underruns;
    uint32_t driver_tx_messages;
    uint32_t driver_rx_messages;
    uint32_t driver_fifo_irqs;
    uint32_t driver_sem_releases;
    uint32_t driver_completion_requests;
    uint32_t driver_mq_send_failures;
    uint32_t audio_stack_used_bytes;
    uint32_t audio_stack_total_bytes;
} pal_audio_port_metrics_t;

int pal_audio_port_start(pal_audio_render_fn render, void *context);
int pal_audio_port_stop(uint32_t timeout_ms);
int pal_audio_port_is_running(void);
void pal_audio_port_metrics_get(pal_audio_port_metrics_t *metrics);

#ifdef __cplusplus
}
#endif

#endif
