#ifndef PAL_AUDIO_PORT_H
#define PAL_AUDIO_PORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pal_audio_render_fn)(void *context, int16_t *samples,
                                    size_t sample_count);

int pal_audio_port_start(pal_audio_render_fn render, void *context);
int pal_audio_port_stop(uint32_t timeout_ms);
int pal_audio_port_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
