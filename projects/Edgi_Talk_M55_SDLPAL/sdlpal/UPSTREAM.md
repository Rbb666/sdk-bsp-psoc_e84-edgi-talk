# SDLPal upstream snapshot

- Repository: https://github.com/sdlpal/sdlpal
- Integration source: `sdlpal-embedded` branch `extreme`
- Locked commit: `23177627e619731188591288215dc2a61d884ae7`
- Imported: 2026-08-04
- License: GPL-3.0; see `upstream/LICENSE`

The target builds an explicit source allowlist: the game core, compatibility
layer, headless SDL shim, and the no-audio contract. ESP32 startup, display,
touch, storage, audio, packed-resource, and memory-profile implementations are
not included. Three `embedded` headers are retained because the extreme branch
includes their declarations from otherwise portable core files; none of their
memory-level or packed-resource modes is enabled.

Local changes to the snapshot are intentionally small:

1. `sdl_shim.c` omits unused static texture storage and supports target-owned
   dynamic surfaces without restoring the upstream multi-megabyte static pixel
   pools. Main and backup screens remain external fixed buffers; temporary UI
   and battle surfaces use the on-chip RT-Thread heap and are freed normally.
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
