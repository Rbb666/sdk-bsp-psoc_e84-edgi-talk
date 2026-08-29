#ifndef RETRO_GO_LAUNCHER_ASSETS_H
#define RETRO_GO_LAUNCHER_ASSETS_H

#include <stdint.h>

typedef struct retro_go_launcher_asset
{
    uint16_t width;
    uint16_t height;
    uint8_t bits_per_pixel;
    uint16_t palette_size;
    const uint16_t *palette;
    const uint8_t *alpha;
    const uint8_t *data;
} retro_go_launcher_asset_t;

extern const retro_go_launcher_asset_t retro_go_asset_background_gb;
extern const retro_go_launcher_asset_t retro_go_asset_background_gbc;
extern const retro_go_launcher_asset_t retro_go_asset_background_gba;
extern const retro_go_launcher_asset_t retro_go_asset_logo_gb;
extern const retro_go_launcher_asset_t retro_go_asset_logo_gbc;
extern const retro_go_launcher_asset_t retro_go_asset_banner_gb;
extern const retro_go_launcher_asset_t retro_go_asset_banner_gbc;

#endif
