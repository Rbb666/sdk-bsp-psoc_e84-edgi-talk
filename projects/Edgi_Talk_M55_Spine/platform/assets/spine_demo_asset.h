/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SPINE_DEMO_ASSET_H
#define SPINE_DEMO_ASSET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spine_demo_blob
{
    const uint8_t *begin;
    const uint8_t *end;
};

struct spine_demo_asset
{
    const char *name;
    const char *page;
    const char *idle;
    const char *action;
    const char *run;
    const char *dash;
    const char *hop;
    const char *skin;
    struct spine_demo_blob atlas;
    struct spine_demo_blob skeleton;
    struct spine_demo_blob texture_rle;
    uint8_t *texture_pixels;
    unsigned width;
    unsigned height;
    bool binary;
};

extern const struct spine_demo_asset spine_demo_model;

#ifdef __cplusplus
}
#endif

#endif
