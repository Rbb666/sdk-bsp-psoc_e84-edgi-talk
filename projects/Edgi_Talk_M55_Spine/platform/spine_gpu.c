/* SPDX-License-Identifier: Apache-2.0 */
#include "spine_gpu.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "board.h"
#include "vg_lite.h"
#include "vg_lite_context.h"

#define SPINE_ITCM __attribute__((section(".spine_itcm")))
#define MAX_BATCH_TRIANGLES 16U
#define TRIANGLE_PATH_WORDS 10U
#define COMPATIBILITY_WORDS 4U
#define COMMAND_HEADROOM_BYTES (8U * 1024U)
#define MAX_SCREEN_ERROR 0.04f
#define MAX_TEXEL_ERROR 0.25f
#define MIN_TRIANGLE_AREA 1.0e-6f

struct point
{
    float x;
    float y;
};

struct vertex
{
    struct point screen;
    struct point texel;
};

struct bounds
{
    struct point min;
    struct point max;
};

struct coverage
{
    struct point points[3];
    struct bounds bounds;
    uint16_t indices[3];
};

union path_word
{
    uint32_t opcode;
    float coordinate;
};

struct draw_batch
{
    vg_lite_matrix_t pattern;
    struct bounds bounds;
    struct coverage coverage[MAX_BATCH_TRIANGLES];
    uint32_t compatible[COMPATIBILITY_WORDS];
    /* Each triangle uses three opcode/coordinate triples and a CLOSE. */
    union path_word commands[MAX_BATCH_TRIANGLES * TRIANGLE_PATH_WORDS + 1U];
    unsigned word_count;
    unsigned triangle_count;
    float inverse_det;
    bool screen_positive;
    bool texel_positive;
};

static vg_lite_buffer_t *render_target;
extern const unsigned char __spine_gfx_start__[];
extern const unsigned char __spine_gfx_end__[];

static const vg_lite_blend_t gpu_blends[] =
{
    [SPINE_GPU_BLEND_NORMAL] = VG_LITE_BLEND_SRC_OVER,
    [SPINE_GPU_BLEND_ADDITIVE] = VG_LITE_BLEND_ADDITIVE,
    [SPINE_GPU_BLEND_MULTIPLY] = VG_LITE_BLEND_MULTIPLY,
    [SPINE_GPU_BLEND_SCREEN] = VG_LITE_BLEND_SCREEN,
};

static int in_graphics_ram(uintptr_t address, size_t bytes)
{
    const uintptr_t start = (uintptr_t)__spine_gfx_start__;
    const uintptr_t end = (uintptr_t)__spine_gfx_end__;
    return address >= start && address < end && bytes <= end - address;
}

static int texture_valid(const spine_gpu_texture_t *texture)
{
    return texture && texture->pixels && texture->width && texture->height &&
           texture->width <= UINT16_MAX && texture->height <= UINT16_MAX &&
           texture->stride_bytes >= texture->width * 4U &&
           texture->stride_bytes <= (unsigned)INT32_MAX / texture->height &&
           (((uintptr_t)texture->pixels | texture->stride_bytes) &
            (SPINE_GPU_TEXTURE_ALIGNMENT - 1U)) == 0U &&
           in_graphics_ram((uintptr_t)texture->pixels, texture->stride_bytes * texture->height);
}

int spine_gpu_prepare_texture(const spine_gpu_texture_t *texture)
{
    if (!texture_valid(texture))
        return -VG_LITE_INVALID_ARGUMENT;

    __DSB();
    return 0;
}

int spine_gpu_begin(vg_lite_buffer_t *target, uint32_t background_abgr)
{
    vg_lite_error_t error;

    render_target = NULL;
    if (!target || target->width <= 0 || target->height <= 0 || target->stride <= 0 ||
        target->stride > INT32_MAX / target->height)
        return -VG_LITE_INVALID_ARGUMENT;

    const size_t bytes = (size_t)target->stride * target->height;
    if (!in_graphics_ram(target->address, bytes))
        return -VG_LITE_INVALID_ARGUMENT;

    /* Enable the full target scissor so VG-Lite uses each path's bounds
     * for tessellation instead of tessellating the entire screen. */
    error = vg_lite_set_scissor(0, 0, target->width, target->height);
    if (error == VG_LITE_SUCCESS)
        error = vg_lite_clear(target, NULL, background_abgr);
    if (error == VG_LITE_SUCCESS)
        render_target = target;
    return -(int)error;
}

static uint32_t premultiply_tint(uint32_t abgr)
{
    const uint32_t alpha = abgr >> 24;
    const uint32_t red = ((abgr & 0xffU) * alpha + 127U) / 255U;
    const uint32_t green = (((abgr >> 8) & 0xffU) * alpha + 127U) / 255U;
    const uint32_t blue = (((abgr >> 16) & 0xffU) * alpha + 127U) / 255U;
    return (alpha << 24) | (blue << 16) | (green << 8) | red;
}

static void SPINE_ITCM load_vertex(struct vertex *vertex, const float *xy, const float *uv,
                                  unsigned index, const spine_gpu_texture_t *texture)
{
    const unsigned offset = index * 2U;
    vertex->screen = (struct point){xy[offset], xy[offset + 1U]};
    vertex->texel = (struct point){uv[offset] * texture->width,
                                   uv[offset + 1U] * texture->height};
}

static inline float winding(struct point a, struct point b, struct point c)
{
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
}

static void SPINE_ITCM affine_matrix(const struct vertex vertices[4], float texel_area,
                                     vg_lite_matrix_t *matrix)
{
    const struct point screen = vertices[0].screen;
    const struct point texel = vertices[0].texel;
    const float dx1 = vertices[1].screen.x - screen.x;
    const float dy1 = vertices[1].screen.y - screen.y;
    const float dx2 = vertices[2].screen.x - screen.x;
    const float dy2 = vertices[2].screen.y - screen.y;
    const float du1 = vertices[1].texel.x - texel.x;
    const float dv1 = vertices[1].texel.y - texel.y;
    const float du2 = vertices[2].texel.x - texel.x;
    const float dv2 = vertices[2].texel.y - texel.y;
    const float inverse_det = 1.0f / texel_area;
    const float a = (dx1 * dv2 - dx2 * dv1) * inverse_det;
    const float b = (du1 * dx2 - du2 * dx1) * inverse_det;
    const float c = (dy1 * dv2 - dy2 * dv1) * inverse_det;
    const float d = (du1 * dy2 - du2 * dy1) * inverse_det;

    vg_lite_identity(matrix);
    matrix->m[0][0] = a;
    matrix->m[0][1] = b;
    matrix->m[0][2] = screen.x - a * texel.x - b * texel.y;
    matrix->m[1][0] = c;
    matrix->m[1][1] = d;
    matrix->m[1][2] = screen.y - c * texel.x - d * texel.y;
}

static bool SPINE_ITCM affine_quad(const struct vertex vertices[4])
{
    /* The two triangles share their diagonal. Affine region attachments
     * are parallelograms in both coordinate spaces, including rotated UVs. */
    if (!(fabsf(vertices[0].screen.x + vertices[2].screen.x -
                vertices[1].screen.x - vertices[3].screen.x) < 0.01f &&
          fabsf(vertices[0].screen.y + vertices[2].screen.y -
                vertices[1].screen.y - vertices[3].screen.y) < 0.01f &&
          fabsf(vertices[0].texel.x + vertices[2].texel.x -
                vertices[1].texel.x - vertices[3].texel.x) < 0.01f &&
          fabsf(vertices[0].texel.y + vertices[2].texel.y -
                vertices[1].texel.y - vertices[3].texel.y) < 0.01f))
        return false;

    const float first_screen = winding(vertices[0].screen, vertices[1].screen, vertices[2].screen);
    const float second_screen = winding(vertices[2].screen, vertices[3].screen, vertices[0].screen);
    const float first_texel = winding(vertices[0].texel, vertices[1].texel, vertices[2].texel);
    const float second_texel = winding(vertices[2].texel, vertices[3].texel, vertices[0].texel);

    return fabsf(first_screen) >= MIN_TRIANGLE_AREA && fabsf(second_screen) >= MIN_TRIANGLE_AREA &&
           fabsf(first_texel) >= MIN_TRIANGLE_AREA && fabsf(second_texel) >= MIN_TRIANGLE_AREA &&
           (first_screen > 0.0f) == (second_screen > 0.0f) &&
           (first_texel > 0.0f) == (second_texel > 0.0f);
}

static inline struct bounds union_bounds(struct bounds a, struct bounds b)
{
    return (struct bounds){
        .min = {fminf(a.min.x, b.min.x), fminf(a.min.y, b.min.y)},
        .max = {fmaxf(a.max.x, b.max.x), fmaxf(a.max.y, b.max.y)},
    };
}

static bool SPINE_ITCM polygon_bounds(const struct vertex vertices[4], unsigned count,
                                     struct bounds *bounds)
{
    *bounds = (struct bounds){vertices[0].screen, vertices[0].screen};
    for (unsigned i = 0; i < count; ++i)
    {
        const struct point point = vertices[i].screen;
        if (!isfinite(point.x) || !isfinite(point.y))
            return false;
        *bounds = union_bounds(*bounds, (struct bounds){point, point});
    }
    return true;
}

static bool SPINE_ITCM separated_by_edge(const struct point a[3], const struct point b[3],
                                        bool positive)
{
    for (unsigned i = 0; i < 3U; ++i)
    {
        const struct point start = a[i];
        const struct point end = a[(i + 1U) % 3U];
        const double dx = (double)end.x - start.x;
        const double dy = (double)end.y - start.y;
        bool outside = true;

        /* Double products keep point/edge contact from becoming an overlap. */
        for (unsigned j = 0; j < 3U; ++j)
        {
            const double side = dx * ((double)b[j].y - start.y) -
                                dy * ((double)b[j].x - start.x);
            if (positive ? side > 0.0 : side < 0.0)
            {
                outside = false;
                break;
            }
        }
        if (outside)
            return true;
    }
    return false;
}

static inline bool find_reversed_edge(const struct coverage *a, const struct coverage *b,
                                      unsigned *edge_a, unsigned *edge_b)
{
    for (unsigned i = 0; i < 3U; ++i)
    {
        const unsigned next = (i + 1U) % 3U;
        for (unsigned j = 0; j < 3U; ++j)
        {
            if (a->indices[next] == b->indices[j] && a->indices[i] == b->indices[(j + 1U) % 3U])
            {
                *edge_a = i;
                *edge_b = j;
                return true;
            }
        }
    }
    return false;
}

static bool SPINE_ITCM shared_edge_separates(const struct coverage *a, const struct coverage *b)
{
    unsigned edge_a, edge_b;
    if (!find_reversed_edge(a, b, &edge_a, &edge_b))
        return false;

    /* Verify strict opposite sides to keep the shortcut safe for narrow triangles. */
    const struct point start = a->points[edge_a];
    const struct point end = a->points[(edge_a + 1U) % 3U];
    const struct point other_a = a->points[(edge_a + 2U) % 3U];
    const struct point other_b = b->points[(edge_b + 2U) % 3U];
    const double dx = (double)end.x - start.x;
    const double dy = (double)end.y - start.y;
    const double a_side = dx * ((double)other_a.y - start.y) -
                          dy * ((double)other_a.x - start.x);
    const double b_side = dx * ((double)other_b.y - start.y) -
                          dy * ((double)other_b.x - start.x);
    return (a_side < 0.0 && b_side > 0.0) || (a_side > 0.0 && b_side < 0.0);
}

static bool SPINE_ITCM triangles_overlap(const struct coverage *a, const struct coverage *b,
                                         bool positive)
{
    if (a->bounds.max.x <= b->bounds.min.x || b->bounds.max.x <= a->bounds.min.x ||
        a->bounds.max.y <= b->bounds.min.y || b->bounds.max.y <= a->bounds.min.y)
        return false;

    if (shared_edge_separates(a, b))
        return false;

    return !separated_by_edge(a->points, b->points, positive) &&
           !separated_by_edge(b->points, a->points, positive);
}

static inline float bounds_area(struct bounds bounds)
{
    return (bounds.max.x - bounds.min.x) * (bounds.max.y - bounds.min.y);
}

static bool SPINE_ITCM pattern_matches(struct draw_batch *batch, const struct vertex vertices[4],
                                       const uint16_t indices[3])
{
    const float (*m)[3] = batch->pattern.m;
    for (unsigned i = 0; i < 3U; ++i)
    {
        const unsigned word = indices[i] / 32U;
        const uint32_t bit = UINT32_C(1) << (indices[i] % 32U);
        const bool cacheable = word < COMPATIBILITY_WORDS;
        if (cacheable && (batch->compatible[word] & bit))
            continue;

        const struct point uv = vertices[i].texel;
        const float dx = m[0][0] * uv.x + m[0][1] * uv.y + m[0][2] - vertices[i].screen.x;
        const float dy = m[1][0] * uv.x + m[1][1] * uv.y + m[1][2] - vertices[i].screen.y;
        if (!(dx * dx + dy * dy <= MAX_SCREEN_ERROR * MAX_SCREEN_ERROR))
            return false;

        /* Also bound sampling error when the pattern is strongly minified. */
        const float du = (m[1][1] * dx - m[0][1] * dy) * batch->inverse_det;
        const float dv = (m[0][0] * dy - m[1][0] * dx) * batch->inverse_det;
        if (!(du * du + dv * dv <= MAX_TEXEL_ERROR * MAX_TEXEL_ERROR))
            return false;
        if (cacheable)
            batch->compatible[word] |= bit;
    }
    return true;
}

static bool SPINE_ITCM can_batch(struct draw_batch *batch, const struct vertex vertices[4],
                                const struct coverage *candidate,
                                struct bounds combined, float screen_area, float texel_area)
{
    const bool screen_positive = screen_area > 0.0f;
    if (batch->triangle_count == MAX_BATCH_TRIANGLES || screen_positive != batch->screen_positive ||
        (texel_area > 0.0f) != batch->texel_positive)
        return false;

    if (!pattern_matches(batch, vertices, candidate->indices))
        return false;

    /* The driver tessellates the combined bounding rectangle. */
    if (bounds_area(combined) > 2.0f * (bounds_area(batch->bounds) + bounds_area(candidate->bounds)))
        return false;

    for (unsigned i = 0; i < batch->triangle_count; ++i)
    {
        if (triangles_overlap(&batch->coverage[i], candidate, screen_positive))
            return false;
    }
    return true;
}

static void SPINE_ITCM append_polygon(struct draw_batch *batch, const struct vertex vertices[4],
                                     unsigned count)
{
    union path_word *commands = batch->commands + batch->word_count;
    for (unsigned i = 0; i < count; ++i)
    {
        commands[3U * i].opcode = i == 0 ? VLC_OP_MOVE : VLC_OP_LINE;
        commands[3U * i + 1U].coordinate = vertices[i].screen.x;
        commands[3U * i + 2U].coordinate = vertices[i].screen.y;
    }
    commands[3U * count].opcode = VLC_OP_CLOSE;
    batch->word_count += 3U * count + 1U;
}

static vg_lite_error_t SPINE_ITCM submit_batch(struct draw_batch *batch, vg_lite_buffer_t *source,
                                              vg_lite_quality_t quality, vg_lite_blend_t blend,
                                              uint32_t tint)
{
    if (batch->word_count == 0U)
        return VG_LITE_SUCCESS;

    batch->commands[batch->word_count++].opcode = VLC_OP_END;

    /* Cover the fringe before the driver's integer tessellation bounds clip it. */
    const float left = fmaxf(0.0f, batch->bounds.min.x - 1.0f);
    const float top = fmaxf(0.0f, batch->bounds.min.y - 1.0f);
    const float right = fminf((float)render_target->width, batch->bounds.max.x + 1.0f);
    const float bottom = fminf((float)render_target->height, batch->bounds.max.y + 1.0f);
    vg_lite_path_t path;
    vg_lite_error_t error = vg_lite_init_path(&path, VG_LITE_FP32, quality,
                                             batch->word_count * sizeof(batch->commands[0]),
                                             batch->commands, left, top, right, bottom);
    batch->word_count = 0U;
    batch->triangle_count = 0U;
    if (error != VG_LITE_SUCCESS)
        return error;

    /* The driver copies these non-uploaded paths into its command buffer. */
    error = vg_lite_draw_pattern(render_target, &path, VG_LITE_FILL_NON_ZERO, NULL,
                                 source, &batch->pattern, blend, VG_LITE_PATTERN_PAD, 0,
                                 tint, VG_LITE_FILTER_BI_LINEAR);
    if (error == VG_LITE_SUCCESS &&
        CMDBUF_SIZE(s_context) > COMMAND_HEADROOM_BYTES &&
        CMDBUF_OFFSET(s_context) >= CMDBUF_SIZE(s_context) - COMMAND_HEADROOM_BYTES)
    {
        /* Submit at a path boundary before the driver's automatic rollover wait. */
        error = vg_lite_flush();
    }
    return error;
}

int SPINE_ITCM spine_gpu_draw(const spine_gpu_texture_t *texture, const float *xy,
                             const float *uv, unsigned vertex_count, const uint16_t *indices,
                             unsigned index_count, uint32_t tint_abgr, spine_gpu_blend_t blend)
{
    if (!render_target || !texture_valid(texture) || !xy || !uv || !indices ||
        (index_count % 3U) || (unsigned)blend >= sizeof(gpu_blends) / sizeof(gpu_blends[0]))
        return -VG_LITE_INVALID_ARGUMENT;

    if ((tint_abgr >> 24) == 0U)
        return 0;

    vg_lite_buffer_t source =
    {
        .width = (int32_t)texture->width,
        .height = (int32_t)texture->height,
        .stride = (int32_t)texture->stride_bytes,
        .tiled = VG_LITE_LINEAR,
        .format = VG_LITE_RGBA8888,
        .memory = (void *)texture->pixels,
        .address = (uint32_t)(uintptr_t)texture->pixels,
        .image_mode = tint_abgr == UINT32_MAX ? VG_LITE_NORMAL_IMAGE_MODE : VG_LITE_MULTIPLY_IMAGE_MODE,
        .paintType = VG_LITE_PAINT_COLOR,
        .premultiplied = 1,
    };
    const uint32_t tint = premultiply_tint(tint_abgr);
    const vg_lite_blend_t gpu_blend = gpu_blends[blend];
    struct draw_batch batch;
    batch.word_count = 0U;
    batch.triangle_count = 0U;

    while (index_count)
    {
        const uint16_t *primitive_indices = indices;
        struct vertex vertices[4];
        unsigned count = 3;

        for (unsigned i = 0; i < count; ++i)
        {
            if (indices[i] >= vertex_count)
                return -VG_LITE_INVALID_ARGUMENT;
            load_vertex(&vertices[i], xy, uv, indices[i], texture);
        }

        /* Spine region triangulation is [0,1,2, 2,3,0]. Fuse such an affine
         * pair into one path, preserving order and skipping the shared edge. */
        if (index_count >= 6U && indices[2] == indices[3] && indices[0] == indices[5] &&
            indices[4] < vertex_count)
        {
            load_vertex(&vertices[3], xy, uv, indices[4], texture);
            if (affine_quad(vertices))
                count = 4;
        }

        const unsigned consumed = count == 4 ? 6U : 3U;
        indices += consumed;
        index_count -= consumed;

        struct bounds bounds;
        if (!polygon_bounds(vertices, count, &bounds))
            return -VG_LITE_INVALID_ARGUMENT;
        if (bounds.max.x <= 0.0f || bounds.max.y <= 0.0f ||
            bounds.min.x >= render_target->width || bounds.min.y >= render_target->height)
            continue;

        const float screen_area = winding(vertices[0].screen, vertices[1].screen, vertices[2].screen);
        const float texel_area = winding(vertices[0].texel, vertices[1].texel, vertices[2].texel);
        if (!isfinite(screen_area) || !isfinite(texel_area) ||
            fabsf(screen_area) < MIN_TRIANGLE_AREA || fabsf(texel_area) < MIN_TRIANGLE_AREA)
            continue;

        const bool batchable = blend == SPINE_GPU_BLEND_NORMAL && count == 3U;
        const struct coverage coverage =
        {
            .points = {vertices[0].screen, vertices[1].screen, vertices[2].screen},
            .bounds = bounds,
            .indices = {primitive_indices[0], primitive_indices[1], primitive_indices[2]},
        };
        if (batchable && batch.triangle_count != 0U)
        {
            const struct bounds combined = union_bounds(batch.bounds, bounds);
            if (can_batch(&batch, vertices, &coverage, combined, screen_area, texel_area))
            {
                batch.bounds = combined;
                batch.coverage[batch.triangle_count++] = coverage;
                append_polygon(&batch, vertices, count);
                continue;
            }
        }

        vg_lite_error_t error = submit_batch(&batch, &source, VG_LITE_LOW, gpu_blend, tint);
        if (error != VG_LITE_SUCCESS)
            return -(int)error;

        batch.bounds = bounds;
        affine_matrix(vertices, texel_area, &batch.pattern);
        append_polygon(&batch, vertices, count);
        if (batchable)
        {
            batch.coverage[batch.triangle_count++] = coverage;
            batch.screen_positive = screen_area > 0.0f;
            batch.texel_positive = texel_area > 0.0f;
            batch.inverse_det = texel_area / screen_area;
            /* Cache only vertices checked against this unchanged pattern. */
            for (unsigned i = 0; i < COMPATIBILITY_WORDS; ++i)
                batch.compatible[i] = 0U;
        }
        else
        {
            const vg_lite_quality_t quality = count == 4U ? VG_LITE_HIGH : VG_LITE_LOW;
            error = submit_batch(&batch, &source, quality, gpu_blend, tint);
            if (error != VG_LITE_SUCCESS)
                return -(int)error;
        }
    }

    return -(int)submit_batch(&batch, &source, VG_LITE_LOW, gpu_blend, tint);
}
