# SDLPal upstream snapshot

- Repository: https://github.com/sdlpal/sdlpal
- Integration source: `sdlpal-embedded` branch `extreme`
- Locked commit: `23177627e619731188591288215dc2a61d884ae7`
- Imported: 2026-08-04
- License: GPL-3.0; see `upstream/LICENSE`

The target builds an explicit source allowlist: the game core, compatibility
layer, headless SDL shim, and a PSoC-owned audio contract. ESP32 startup,
display, touch, storage, packed-resource, and memory-profile implementations
are not included. Three `embedded` headers are retained because the extreme
branch includes their declarations from otherwise portable core files; none of
their memory-level or packed-resource modes is enabled.

The project-private `audio/third_party` tree imports the following fixed-memory
RIX/OPL2 files from the same locked commit:

- `adplug/{binio.h,fprovide.h,opl.h,opltypes.h,player.cpp,player.h,rix.cpp,rix.h}`
- `adplug/mame/{fmopl.cpp.h,fmopl.h,mame.h}`
- `embedded/pal_mame_opl2_fixed_tables.inc`
- `embedded/pal_mame_opl2_static.{cpp,h}`

AdPlug files retain their LGPL-2.1-or-later notices. The MAME FMOPL core retains
its GPL-2.0-or-later notice, and the SDLPal wrapper remains GPL-3.0-or-later.
The imported `fmopl.cpp.h` and fixed-table include match their source Git blobs
exactly. Project-local adaptations only change relative include paths, omit
`common.h` in the fixed-memory RIX build to avoid unused desktop dependencies,
and place mutable OPL state in `.sdlpal_audio` Secondary SRAM.

Local changes to the snapshot are intentionally small:

1. `sdl_shim.c` omits unused static texture storage and supports target-owned
   dynamic surfaces without restoring the upstream multi-megabyte static pixel
   pools. Main and backup screens remain external fixed buffers; temporary UI
   and battle surfaces prefer the on-chip RT-Thread heap, then use the fixed
   GFX SRAM surface pool and HyperRAM fallback when SRAM is fragmented, and are
   freed through the allocator that supplied their pixels.
2. The shim declares two SDL2 texture helpers used only by the disabled touch
   overlay path; no renderer texture pool is allocated for this target.
3. PSoC-only engine changes are guarded by `PAL_PSOC_DIRECT_INDEXED`, including
   the `PAL_EngineMain` entry name, tick-based screenshot filename, and direct
   indexed-video hook. Builds without that macro retain upstream behavior.
4. The built-in Unicode and ASCII glyph tables are `const` on this target and
   runtime BDF/wor16 overrides are disabled. This keeps roughly 2.1 MiB of
   read-only font data in external Flash instead of copying it into DTCM.
5. Target-owned temporary surface pixels use the explicit hot-memory policy,
   and guarded lifecycle hooks emit SRAM, HyperRAM, GFX and stack diagnostics.
6. Normal engine allocations use platform-owned libc hooks that prefer the
   on-chip SRAM heap, then a fixed 128 KiB GFX resource pool, and use HyperRAM
   only as the final fallback. The allocation changes remain in platform hooks
   and the platform-specific shim; upstream battle and fight code is unchanged.
7. DOS audio is supplied by the project-private `audio/` group. It implements
   the upstream `AUDIO_*` ABI, MKF caching, VOC mixing and the RT-Thread
   `sound0` port without editing any file below `sdlpal/upstream`. Mutable audio
   state is linked into `.sdlpal_audio` Secondary SRAM, while `mus.mkf` and
   `voc.mkf` chunks use the bounded HyperRAM cache.
