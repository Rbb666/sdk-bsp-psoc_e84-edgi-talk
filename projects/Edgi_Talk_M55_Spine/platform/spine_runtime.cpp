/* SPDX-License-Identifier: Apache-2.0 */
#include "spine_runtime.h"

#include "spine_gpu.h"
#include "assets/spine_demo_asset.h"

#include <rtthread.h>
#include <spine/spine.h>

#include <cmath>
#include <new>
#include <string.h>

namespace
{
#ifndef SPINE_RUNTIME_POOL_KIB
#define SPINE_RUNTIME_POOL_KIB 128
#endif
constexpr size_t runtime_pool_size = SPINE_RUNTIME_POOL_KIB * 1024;
constexpr float viewport_fraction = 0.78f;
#ifdef SPINE_FRAME_BUFFERS_SOCMEM
constexpr bool frame_buffers_use_socmem = true;
#else
constexpr bool frame_buffers_use_socmem = false;
#endif

__attribute__((section(".spine_dtcm"), aligned(8)))
uint8_t runtime_pool[runtime_pool_size];

spine_gpu_texture_t atlas_texture;
rt_smem_t runtime_heap;
bool allocate_in_socmem;

class AllocationScope
{
public:
    explicit AllocationScope(bool use_socmem) : previous(allocate_in_socmem)
    {
        allocate_in_socmem = use_socmem;
    }

    ~AllocationScope() { allocate_in_socmem = previous; }

private:
    const bool previous;
};

bool in_dtcm(const void *block)
{
    return reinterpret_cast<uintptr_t>(block) - reinterpret_cast<uintptr_t>(runtime_pool) < sizeof(runtime_pool);
}

class PlatformExtension final : public spine::SpineExtension
{
    void *_alloc(size_t size, const char *, int) override
    {
        return _realloc(nullptr, size, nullptr, 0);
    }

    void *_calloc(size_t size, const char *, int) override
    {
        void *block = _alloc(size, nullptr, 0);
        if (block != nullptr)
            memset(block, 0, size);
        return block;
    }

    void *_realloc(void *block, size_t size, const char *, int) override
    {
        if (size == 0)
        {
            _free(block, nullptr, 0);
            return nullptr;
        }

        /* Existing allocations stay in the heap that owns them. */
        const bool use_dtcm = block != nullptr ? in_dtcm(block) : !allocate_in_socmem;
        void *resized = use_dtcm ? rt_smem_realloc(runtime_heap, block, size) : rt_realloc(block, size);
        if (resized == nullptr)
            rt_kprintf("Spine: %s heap exhausted requesting %u bytes\n",
                       use_dtcm ? "DTCM" : "SRAM", (unsigned)size);

        /* The upstream containers require successful allocations. */
        RT_ASSERT(resized != nullptr);
        return resized;
    }

    void _free(void *block, const char *, int) override
    {
        if (in_dtcm(block))
            rt_smem_free(block);
        else
            rt_free(block);
    }

    char *_readFile(const spine::String &, int *length) override
    {
        if (length != nullptr)
            *length = 0;
        return nullptr;
    }
};

alignas(PlatformExtension) unsigned char extension_storage[sizeof(PlatformExtension)];

class EmbeddedTextureLoader final : public spine::TextureLoader
{
public:
    bool loaded() const { return loaded_; }

private:
    bool loaded_ = false;

    void load(spine::AtlasPage &page, const spine::String &) override
    {
        if (page.name == spine_demo_model.page && page.pma &&
            page.width == static_cast<int>(atlas_texture.width) &&
            page.height == static_cast<int>(atlas_texture.height))
        {
            page.texture = &atlas_texture;
            loaded_ = true;
        }
    }

    void unload(void *) override {}
};

struct Runtime
{
    EmbeddedTextureLoader *loader;
    spine::Atlas *atlas;
    spine::SkeletonData *data;
    spine::Skeleton *skeleton;
    spine::AnimationStateData *state_data;
    spine::AnimationState *animation;
    spine::SkeletonRenderer *renderer;
    float center_x;
    float center_y;
    bool facing_left;
};

__attribute__((section(".spine_dtcm"))) Runtime runtime;

size_t blob_size(const spine_demo_blob &blob)
{
    return reinterpret_cast<uintptr_t>(blob.end) - reinterpret_cast<uintptr_t>(blob.begin);
}

int prepare_atlas()
{
    const auto &asset = spine_demo_model;
    uint8_t *pixels = asset.texture_pixels;
    const uint8_t *input = asset.texture_rle.begin;
    size_t available = blob_size(asset.texture_rle);
    size_t remaining = static_cast<size_t>(asset.width) * asset.height * 4;

    while (available != 0)
    {
        const unsigned token = *input++;
        --available;
        const size_t bytes = ((token & 0x7f) + 1) * 4;
        const bool repeat = (token & 0x80) != 0;
        const size_t source_bytes = repeat ? 4 : bytes;
        if (source_bytes > available || bytes > remaining)
            return -RT_EINVAL;

        if (repeat)
        {
            for (size_t offset = 0; offset < bytes; offset += 4)
                memcpy(pixels + offset, input, 4);
        }
        else
        {
            memcpy(pixels, input, bytes);
        }
        pixels += bytes;
        remaining -= bytes;
        input += source_bytes;
        available -= source_bytes;
    }
    if (remaining != 0)
        return -RT_EINVAL;

    atlas_texture = {asset.texture_pixels, asset.width, asset.height, asset.width * 4};
    return spine_gpu_prepare_texture(&atlas_texture);
}

int load_rig()
{
    const auto &asset = spine_demo_model;
    runtime.loader = new EmbeddedTextureLoader();
    runtime.atlas = new spine::Atlas(reinterpret_cast<const char *>(asset.atlas.begin),
                                    blob_size(asset.atlas), "", runtime.loader);
    auto &pages = runtime.atlas->getPages();
    if (pages.size() != 1 || !runtime.loader->loaded())
    {
        rt_kprintf("Spine: embedded atlas page is invalid\n");
        return -RT_ERROR;
    }

    if (asset.binary)
    {
        spine::SkeletonBinary reader(*runtime.atlas);
        runtime.data = reader.readSkeletonData(asset.skeleton.begin, blob_size(asset.skeleton));
        if (runtime.data == nullptr)
            rt_kprintf("Spine: binary skeleton load failed: %s\n", reader.getError().buffer());
    }
    else
    {
        spine::SkeletonJson reader(*runtime.atlas);
        runtime.data = reader.readSkeletonData(reinterpret_cast<const char *>(asset.skeleton.begin));
        if (runtime.data == nullptr)
            rt_kprintf("Spine: JSON skeleton load failed: %s\n", reader.getError().buffer());
    }
    return runtime.data != nullptr ? RT_EOK : -RT_ERROR;
}

int start_animation(unsigned width, unsigned height)
{
    runtime.skeleton = new spine::Skeleton(*runtime.data);
    if (spine_demo_model.skin != nullptr)
    {
        auto *skin = runtime.data->findSkin(spine_demo_model.skin);
        if (skin == nullptr)
            return -RT_ENOENT;
        runtime.skeleton->setSkin(skin);
        runtime.skeleton->setupPoseSlots();
    }

    runtime.state_data = new spine::AnimationStateData(*runtime.data);
    runtime.state_data->setDefaultMix(0.13f);
    runtime.animation = new spine::AnimationState(*runtime.state_data);
    {
        const AllocationScope heap(frame_buffers_use_socmem);
        runtime.renderer = new spine::SkeletonRenderer();
    }

    const auto &asset = spine_demo_model;
    if (runtime.data->findAnimation(asset.action) == nullptr ||
        (asset.dash != nullptr && runtime.data->findAnimation(asset.dash) == nullptr) ||
        (asset.hop != nullptr && runtime.data->findAnimation(asset.hop) == nullptr) ||
        spine_runtime_play(spine_demo_model.idle, true) != RT_EOK)
        return -RT_ENOENT;

    /* Spine's top-down coordinates keep the model upright on the LCD. */
    spine::Bone::setYDown(true);
    spine_runtime_update(0.0f);
    float left, top, pose_width, pose_height;
    runtime.skeleton->getBounds(left, top, pose_width, pose_height);
    if (!(pose_width > 0.0f && pose_height > 0.0f && std::isfinite(pose_width) && std::isfinite(pose_height) &&
          std::isfinite(left) && std::isfinite(top)))
        return -RT_EINVAL;

    const float sx = width * viewport_fraction / pose_width;
    const float sy = height * viewport_fraction / pose_height;
    const float scale = sx < sy ? sx : sy;
    runtime.skeleton->setScaleX(scale);
    runtime.skeleton->setScaleY(scale);
    /* Keep the initial idle pose as the positioning reference for every clip. */
    runtime.center_x = (left + pose_width * 0.5f) * scale;
    runtime.center_y = (top + pose_height * 0.5f) * scale;
    spine_runtime_move(width * 0.5f, height * 0.5f);
    spine_runtime_update(0.0f);
    rt_kprintf("Spine: loaded %s (%u bones, %u animations)\n", spine_demo_model.name,
               (unsigned)runtime.data->getBones().size(), (unsigned)runtime.data->getAnimations().size());
    return RT_EOK;
}
} // namespace

spine::SpineExtension *spine::getDefaultExtension()
{
    return new (extension_storage) PlatformExtension();
}

int spine_runtime_init(unsigned width, unsigned height)
{
    if (width == 0 || height == 0)
        return -RT_EINVAL;
    if (runtime_heap != nullptr)
        return -RT_EBUSY;

    int result = prepare_atlas();
    if (result != RT_EOK)
        return result;

    runtime_heap = rt_smem_init("spine", runtime_pool, sizeof(runtime_pool));
    if (runtime_heap == nullptr)
        return -RT_ENOMEM;

    /* Load the larger binary rig into the shared on-chip heap. */
    {
        const AllocationScope heap(spine_demo_model.binary);
        result = load_rig();
    }
    if (result == RT_EOK)
        result = start_animation(width, height);
    if (result != RT_EOK)
        spine_runtime_deinit();

    return result;
}

void spine_runtime_deinit(void)
{
    if (runtime_heap == nullptr)
        return;

    delete runtime.renderer;
    delete runtime.animation;
    delete runtime.state_data;
    delete runtime.skeleton;
    delete runtime.data;
    delete runtime.atlas;
    delete runtime.loader;
    runtime = {};

    rt_smem_detach(runtime_heap);
    runtime_heap = nullptr;
}

void spine_runtime_move(float x, float y)
{
    if (runtime.skeleton != nullptr)
    {
        const float center_x = runtime.facing_left ? -runtime.center_x : runtime.center_x;
        runtime.skeleton->setPosition(x - center_x, y - runtime.center_y);
    }
}

void spine_runtime_face_left(bool left)
{
    if (runtime.skeleton != nullptr && left != runtime.facing_left)
    {
        runtime.skeleton->setScaleX(-runtime.skeleton->getScaleX());
        runtime.facing_left = left;
    }
}

int spine_runtime_play(const char *name, bool loop)
{
    if (runtime.animation == nullptr || name == nullptr)
        return -RT_EINVAL;

    spine::Animation *animation = runtime.data->findAnimation(name);
    if (animation == nullptr)
        return -RT_ENOENT;

    runtime.animation->setAnimation(0, *animation, loop);
    if (!loop)
        runtime.animation->addAnimation(0, spine_demo_model.idle, true, 0.0f);
    return RT_EOK;
}

int spine_runtime_action(void)
{
    return spine_runtime_play(spine_demo_model.action, false);
}

int spine_runtime_idle(void)
{
    return spine_runtime_play(spine_demo_model.idle, true);
}

int spine_runtime_run(void)
{
    const char *run = spine_demo_model.run;
    return run != nullptr ? spine_runtime_play(run, true) : spine_runtime_idle();
}

int spine_runtime_dash(void)
{
    const char *dash = spine_demo_model.dash;
    return dash != nullptr ? spine_runtime_play(dash, false) : -RT_ENOENT;
}

int spine_runtime_hop(void)
{
    const char *hop = spine_demo_model.hop;
    return hop != nullptr ? spine_runtime_play(hop, false) : -RT_ENOENT;
}

const char *spine_runtime_model_name(void)
{
    return spine_demo_model.name;
}

__attribute__((section(".spine_itcm")))
void spine_runtime_update(float seconds)
{
    if (runtime.animation == nullptr)
        return;

    runtime.skeleton->update(seconds);
    runtime.animation->update(seconds);
    {
        /* Action timelines lazily allocate mesh deformations. */
        const AllocationScope heap(frame_buffers_use_socmem);
        runtime.animation->apply(*runtime.skeleton);
    }
    runtime.skeleton->updateWorldTransform(spine::Physics_Update);
}

__attribute__((section(".spine_itcm")))
int spine_runtime_draw(void)
{
    if (runtime.renderer == nullptr)
        return -RT_EINVAL;

    const spine::RenderCommand *commands;
    {
        /* C012's 4.3 renderer batches complete slot meshes before GPU submission. */
        const AllocationScope heap(frame_buffers_use_socmem);
        commands = runtime.renderer->render(*runtime.skeleton);
    }
    for (const spine::RenderCommand *command = commands; command != nullptr; command = command->next)
    {
        if (command->numIndices == 0)
            continue;

        const uint32_t argb = command->colors[0];
        const uint32_t abgr = (argb & 0xff00ff00u) | ((argb & 0xffu) << 16) | ((argb >> 16) & 0xffu);
        const int result = spine_gpu_draw(static_cast<const spine_gpu_texture_t *>(command->texture),
                                          command->positions, command->uvs, command->numVertices,
                                          command->indices, command->numIndices, abgr,
                                          static_cast<spine_gpu_blend_t>(command->blendMode));
        if (result != RT_EOK)
            return result;
    }

    return RT_EOK;
}

unsigned spine_runtime_heap_peak(void)
{
    return runtime_heap != nullptr ? runtime_heap->max : 0;
}
