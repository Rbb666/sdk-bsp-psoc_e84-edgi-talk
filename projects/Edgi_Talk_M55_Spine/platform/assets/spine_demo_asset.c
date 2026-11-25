/* SPDX-License-Identifier: Apache-2.0 */
#include "spine_demo_asset.h"

#include "spine_gpu.h"

#include "spine_demo_data.inc"

enum { ATLAS_WIDTH = 128, ATLAS_HEIGHT = 128 };

__attribute__((section(".spine_gpu"), aligned(SPINE_GPU_TEXTURE_ALIGNMENT)))
static uint8_t atlas_pixels[ATLAS_WIDTH * ATLAS_HEIGHT * 4];

const struct spine_demo_asset spine_demo_model =
{
    .name = "Orbit",
    .page = "orbit.rgba",
    .idle = "idle",
    .action = "action",
    .atlas = {(const uint8_t *)spine_demo_atlas,
              (const uint8_t *)spine_demo_atlas + sizeof(spine_demo_atlas) - 1},
    .skeleton = {(const uint8_t *)spine_demo_skeleton,
                 (const uint8_t *)spine_demo_skeleton + sizeof(spine_demo_skeleton)},
    .texture_rle = {packed_pixels, packed_pixels + sizeof(packed_pixels)},
    .texture_pixels = atlas_pixels,
    .width = ATLAS_WIDTH,
    .height = ATLAS_HEIGHT,
};
