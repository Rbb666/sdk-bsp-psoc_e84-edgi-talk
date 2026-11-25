/* SPDX-License-Identifier: Apache-2.0 */
#include "assets/spine_demo_asset.h"
#include "spine_gpu.h"

extern const uint8_t c012_atlas[], c012_atlas_end[];
extern const uint8_t c012_skeleton[], c012_skeleton_end[];
extern const uint8_t c012_texture_rle[], c012_texture_rle_end[];

enum { ATLAS_SIZE = 560 };

__attribute__((section(".spine_gpu"), aligned(SPINE_GPU_TEXTURE_ALIGNMENT)))
static uint8_t atlas_pixels[ATLAS_SIZE * ATLAS_SIZE * 4];

const struct spine_demo_asset spine_demo_model =
{
    .name = "NIKKE c012",
    .page = "c012_00.png",
    .skin = "00",
    .idle = "idle",
    .action = "action",
    .dash = "dash",
    .hop = "hop",
    .atlas = {c012_atlas, c012_atlas_end},
    .skeleton = {c012_skeleton, c012_skeleton_end},
    .texture_rle = {c012_texture_rle, c012_texture_rle_end},
    .texture_pixels = atlas_pixels,
    .width = ATLAS_SIZE,
    .height = ATLAS_SIZE,
    .binary = true,
};
