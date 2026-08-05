# SDLPal Main Workspace Migration Design

## Goal

Migrate the validated `Edgi_Talk_M55_SDLPAL` project from `.w/s/projects`
into the repository's main `projects` directory without changing the behavior
of existing projects. Preserve `.w/s` as a rollback source.

## Scope

The migration includes the current SDLPal project sources, project-local
tests, and project-local delivery tools. It excludes generated objects,
firmware images, map files, reports, host test executables, worktree-only
design notes, and unrelated repository documentation changes.

The SDK manifest gains one `Edgi_Talk_M55_SDLPAL` project entry so the new
project can be discovered and exported like existing examples. Root README
files are not changed.

## Project Copy Contract

The destination is `projects/Edgi_Talk_M55_SDLPAL`. The source of truth for
the copy is the current `.w/s/projects/Edgi_Talk_M55_SDLPAL` working tree,
including its tracked modifications and non-ignored untracked platform and
test sources.

Only source-controlled and non-ignored source files are copied. The project
must not contain `build`, `reports`, `.elf`, `.hex`, `.map`, `.o`, `.su`, or
host `.exe` artifacts after migration.

The imported `sdlpal/upstream` tree is copied byte-for-byte from the source
worktree. No new platform changes are made inside upstream sources.

## Project-Local Configuration

The project defines the unique build macro `BSP_USING_SDLPAL` in its own
configuration and adds it to the SCons compiler environment so it is visible
to common and vendor sources even when those sources do not include
`rtconfig.h` directly.

`BSP_LCD_VGLITE_INDEXED` is defined by the SDLPal project Kconfig and depends
on `BSP_USING_SDLPAL`. The shared `libraries/M55_Config/Kconfig` remains
unchanged.

The project Kconfig uses `platform` as the `PLATFORM_DIR` default instead of
an absolute workstation path.

LCD functions consumed by the platform port are declared in a project-local
header under `platform`. The migration does not add
`libraries/HAL_Drivers/drv_lcd.h`.

## Shared-Code Isolation

Only three shared implementation files may change:

- `libraries/HAL_Drivers/drv_lcd.c`
- `libraries/Common/board/ports/display_panels/drv_touch.c`
- `libraries/components/mtb-device-support-pse8xxgp/pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/vg_lite_hal.c`

Every SDLPal-specific behavior in these files is guarded by
`BSP_USING_SDLPAL`. When the macro is absent, preprocessing must retain the
original main-branch behavior.

The LCD guard covers the indexed staging buffer and blit function, SDLPal
RGB565 channel order, and SDLPal-only VG-Lite initialization failure state.
Existing LVGL and rotation behavior remains on its original path.

The ST7102 guard covers caller-capacity limiting and valid-slot filtering.
The original parser remains the fallback for all other projects. Host-only
touch hooks require both `BSP_USING_SDLPAL` and `ST7102_HOST_TEST`.

The VG-Lite HAL guard prevents the SDLPal bare-metal path from freeing the
static device object and clears its pointer for SDLPal shutdown. Other
projects retain the original free path.

## Manifest Integration

`sdk-bsp-psoc_e84-edgi-talk.yaml` receives only the new project entry already
validated in the isolated branch. Existing project entries and ordering are
otherwise unchanged.

## Contract Tests

Project contract tests are updated to assert:

- `BSP_USING_SDLPAL` is project-owned and compiler-visible.
- `BSP_LCD_VGLITE_INDEXED` is project-local.
- the shared M55 Kconfig has no SDLPal indexed option.
- no shared `drv_lcd.h` is required.
- all three shared implementations contain the SDLPal guard and preserve an
  explicit non-SDLPal path where behavior differs.
- root README files are outside the migration contract.

Existing host tests continue to cover display conversion, touch parsing,
save I/O, memory policy, stack budget, and ELF layout.

## Verification

The migrated project must pass:

1. All host C tests.
2. All Python contract and ELF tests.
3. Static stack-usage validation with a 12 KiB function-frame limit.
4. A full 90-degree target build and final ELF layout validation.
5. The 0/90/180/270 build matrix.
6. A clean build of `projects/Edgi_Talk_M55_LVGL` after the shared changes.
7. `git diff --check` and an artifact scan of the migrated project.

The final 90-degree SDLPal ELF must keep its selected hot code in ITCM and
preserve at least 64 KiB of unused ITCM. The source worktree `.w/s` remains
registered and is not deleted.

## Failure Handling

If the migrated SDLPal build differs from the isolated build, compare the
project source manifests and generated ELF contracts before changing code.
If the LVGL project fails, treat the shared-code isolation as incorrect and
restore the non-SDLPal path rather than adding another global workaround.
