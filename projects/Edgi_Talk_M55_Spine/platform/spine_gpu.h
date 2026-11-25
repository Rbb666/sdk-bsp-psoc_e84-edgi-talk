/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SPINE_GPU_H
#define SPINE_GPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vg_lite_buffer;

#define SPINE_GPU_TEXTURE_ALIGNMENT 64U

typedef struct
{
    const void *pixels;
    unsigned width;
    unsigned height;
    unsigned stride_bytes;
} spine_gpu_texture_t;

typedef enum
{
    SPINE_GPU_BLEND_NORMAL,
    SPINE_GPU_BLEND_ADDITIVE,
    SPINE_GPU_BLEND_MULTIPLY,
    SPINE_GPU_BLEND_SCREEN
} spine_gpu_blend_t;

/* Prepare alpha-premultiplied RGBA8888 bytes in GPU-visible memory.
 * The pixel address and byte stride must both be 64-byte aligned. */
int spine_gpu_prepare_texture(const spine_gpu_texture_t *texture);

/* Queue drawing on the display's back buffer. Its present call fences the GPU.
 * Calls must be serialized by the display's render thread. */
int spine_gpu_begin(struct vg_lite_buffer *target, uint32_t background_abgr);
int spine_gpu_draw(const spine_gpu_texture_t *texture, const float *xy, const float *uv,
                   unsigned vertex_count, const uint16_t *indices, unsigned index_count,
                   uint32_t tint_abgr, spine_gpu_blend_t blend);

#ifdef __cplusplus
}
#endif

#endif
