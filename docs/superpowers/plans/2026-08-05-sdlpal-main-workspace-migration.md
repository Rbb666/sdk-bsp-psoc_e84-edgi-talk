# SDLPal Main Workspace Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the validated SDLPal project into the main `projects` directory while preserving the behavior of every existing project.

**Architecture:** Copy only the current project source set from `.w/s`, then make all shared LCD, ST7102, and VG-Lite behavior conditional on the project-owned `BSP_USING_SDLPAL` compiler macro. Keep configuration and API declarations project-local, register only the new project in the SDK manifest, and verify both SDLPal and the existing LVGL project.

**Tech Stack:** RT-Thread 5.0.2, SCons, GCC Arm Embedded, Python `unittest`, host GCC/Make, GNU ld, PowerShell.

## Global Constraints

- Destination: `projects/Edgi_Talk_M55_SDLPAL` in the main worktree.
- Source: current `.w/s/projects/Edgi_Talk_M55_SDLPAL` working-tree content.
- Preserve `.w/s`; do not delete or unregister it.
- Do not copy generated firmware, maps, reports, objects, stack reports, or host executables.
- Do not add new modifications under `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream`.
- Shared behavior changes must require `BSP_USING_SDLPAL`.
- Do not modify `libraries/M55_Config/Kconfig`, shared `drv_lcd.h`, root README files, or unrelated project files.
- Final default display rotation remains 90 degrees.

---

### Task 1: Establish the Existing-Project Baseline

**Files:**
- Read: `projects/Edgi_Talk_M55_LVGL/**`
- Create outside repository: `%TEMP%/sdlpal-migration-lvgl-size-before.txt`

**Interfaces:**
- Consumes: clean main worktree at commit `e1c6447` or its descendant.
- Produces: a successful LVGL build and its `text/data/bss` baseline.

- [ ] **Step 1: Verify the destination is absent and main is otherwise clean**

Run:

```powershell
rtk git status --short
rtk powershell -NoProfile -Command "Test-Path -LiteralPath 'projects/Edgi_Talk_M55_SDLPAL'"
```

Expected: only this plan may be untracked; destination prints `False`.

- [ ] **Step 2: Build the existing LVGL project before shared changes**

Run from `projects/Edgi_Talk_M55_LVGL`:

```powershell
rtk powershell -NoProfile -Command "$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -j8"
```

Expected: exit 0 and an `rt-thread.elf` target.

- [ ] **Step 3: Record the LVGL size baseline outside the repository**

Run:

```powershell
rtk powershell -NoProfile -Command "& 'D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin\arm-none-eabi-size.exe' 'rt-thread.elf' | Set-Content -LiteralPath ([IO.Path]::Combine([IO.Path]::GetTempPath(), 'sdlpal-migration-lvgl-size-before.txt'))"
```

Expected: the temporary file contains one `text data bss dec hex` row.

### Task 2: Copy the Current Project Source Set

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/**`
- Source only: `.w/s/projects/Edgi_Talk_M55_SDLPAL/**`

**Interfaces:**
- Consumes: the isolated project working tree, including non-ignored untracked platform/test files.
- Produces: a source-only destination tree with the same relevant file hashes.

- [ ] **Step 1: Reconfirm the exact absolute copy endpoints**

Run:

```powershell
rtk powershell -NoProfile -Command "Resolve-Path '.w\s\projects\Edgi_Talk_M55_SDLPAL'; Test-Path -LiteralPath 'projects\Edgi_Talk_M55_SDLPAL'"
```

Expected: source resolves inside the repository and destination is `False`.

- [ ] **Step 2: Mechanically copy the source tree without junctions or generated artifacts**

Run from the repository root:

```powershell
rtk robocopy ".w\s\projects\Edgi_Talk_M55_SDLPAL" "projects\Edgi_Talk_M55_SDLPAL" /E /XJ /XD build reports Debug "documentation\html" libraries libs rt-thread .vscode DebugConfig RTE settings /XF *.elf *.hex *.map *.o *.obj *.su *.d *.dep *.exe *.pdb *.idb *.ilk *.old *.out *.bak *.lib *.dblite JLinkLog.txt JLinkSettings.ini
```

Expected: robocopy status 1 (files copied) and no access outside the two verified endpoints.

- [ ] **Step 3: Verify the destination manifest and upstream hashes**

Run source/destination `rg --files --hidden` listings with the same exclusions, then compare them. Run `Get-FileHash -Algorithm SHA256` over both `sdlpal/upstream` trees and compare sorted path/hash pairs.

Expected: source manifests and upstream hashes match; no generated artifact pattern is present.

- [ ] **Step 4: Commit the source-only project import**

```powershell
rtk git add projects\Edgi_Talk_M55_SDLPAL
rtk git commit -m "feat: migrate SDLPal project into main workspace"
```

Expected: the commit contains only `projects/Edgi_Talk_M55_SDLPAL`.

### Task 3: Make Configuration and LCD API Project-Owned

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/Kconfig`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/.config`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/rtconfig.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/SConstruct`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_lcd_api.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_display_port.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`
- Delete from destination if copied: `projects/Edgi_Talk_M55_SDLPAL/tests/host/fakes_display/drv_lcd.h`

**Interfaces:**
- Produces: global compiler define `BSP_USING_SDLPAL`, project-local indexed Kconfig symbol, and LCD function declarations.
- Consumes: LCD functions implemented by the guarded shared driver in Task 4.

- [ ] **Step 1: Add failing project-isolation contract assertions**

Add assertions equivalent to:

```python
self.assertIn("config BSP_USING_SDLPAL", project_kconfig)
self.assertIn("config BSP_LCD_VGLITE_INDEXED", project_kconfig)
self.assertNotIn("config BSP_LCD_VGLITE_INDEXED", shared_kconfig)
self.assertIn("env.Append(CPPDEFINES=['BSP_USING_SDLPAL'])", sconstruct)
self.assertIn("#define BSP_USING_SDLPAL", rtconfig)
self.assertTrue((ROOT / "platform" / "pal_lcd_api.h").is_file())
self.assertFalse((BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_lcd.h").exists())
```

Change LCD header reads from the shared path to `ROOT / "platform" / "pal_lcd_api.h"`.

- [ ] **Step 2: Run the contract test and verify RED**

```powershell
rtk python -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract.ProjectContractTest.test_target_contract
```

Expected: FAIL because `BSP_USING_SDLPAL` and `pal_lcd_api.h` do not exist yet.

- [ ] **Step 3: Add project-owned Kconfig symbols**

Add before sourcing shared Kconfig:

```kconfig
config BSP_USING_SDLPAL
    bool
    default y

config BSP_LCD_VGLITE_INDEXED
    bool "Enable SDLPal VG-Lite indexed framebuffer blit"
    depends on BSP_USING_SDLPAL
    default y
```

Set `PLATFORM_DIR` to:

```kconfig
default "platform"
```

Add `CONFIG_BSP_USING_SDLPAL=y` to `.config` and `#define BSP_USING_SDLPAL` to `rtconfig.h`.

- [ ] **Step 4: Make the unique macro compiler-visible to every source**

Immediately after constructing `env` in `SConstruct`, add:

```python
env.Append(CPPDEFINES=['BSP_USING_SDLPAL'])
```

This is required because vendor VG-Lite sources do not include `rtconfig.h`.

- [ ] **Step 5: Add the project-local LCD API**

Create `platform/pal_lcd_api.h`:

```c
#ifndef PAL_LCD_API_H
#define PAL_LCD_API_H

#include <stdint.h>
#include <rtthread.h>

void lcd_flush_rgb565(const void *pixels, uint32_t width, uint32_t height);
void lcd_flush_rgb565_area(const void *pixels, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height,
                           uint32_t src_stride, rt_bool_t present);
rt_bool_t lcd_blit_indexed8(const void *pixels,
                            uint32_t width, uint32_t height,
                            uint32_t src_stride, const uint32_t *clut,
                            uint32_t x, uint32_t y,
                            uint32_t dst_width, uint32_t dst_height,
                            rt_bool_t present);
rt_err_t lcd_wait_frame_done(uint32_t timeout_ms);

#endif
```

Include `pal_lcd_api.h` from `pal_display_port.c`. Update the host Makefile to depend on this header and use `fakes_display/rtthread.h`; remove the copied fake `drv_lcd.h`.

- [ ] **Step 6: Run the focused contract test**

Expected: it now progresses to failures for the still-unmigrated guarded shared implementations.

### Task 4: Guard All Necessary Shared Implementations

**Files:**
- Modify: `libraries/HAL_Drivers/drv_lcd.c`
- Modify: `libraries/Common/board/ports/display_panels/drv_touch.c`
- Modify: `libraries/components/mtb-device-support-pse8xxgp/pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/vg_lite_hal.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Interfaces:**
- Consumes: global `BSP_USING_SDLPAL` define from Task 3.
- Produces: indexed LCD path, corrected SDLPal RGB565 format, bounded ST7102 parser, and safe SDLPal VG-Lite shutdown with original non-SDLPal behavior preserved.

- [ ] **Step 1: Add failing macro-isolation assertions**

Add this dedicated test:

```python
def test_shared_changes_are_sdlpal_guarded(self):
    lcd = (
        BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_lcd.c"
    ).read_text(encoding="utf-8")
    touch = (
        BSP_ROOT
        / "libraries"
        / "Common"
        / "board"
        / "ports"
        / "display_panels"
        / "drv_touch.c"
    ).read_text(encoding="utf-8")
    vg_hal = (
        BSP_ROOT
        / "libraries"
        / "components"
        / "mtb-device-support-pse8xxgp"
        / "pdl"
        / "drivers"
        / "third_party"
        / "COMPONENT_GFXSS"
        / "vsi"
        / "gcnano"
        / "vg_lite_hal.c"
    ).read_text(encoding="utf-8")

    self.assertIn("defined(BSP_USING_SDLPAL)", lcd)
    self.assertIn("defined(BSP_LCD_VGLITE_INDEXED)", lcd)
    self.assertIn("#ifdef BSP_USING_SDLPAL", lcd)
    self.assertIn("buffer->format = VG_LITE_RGB565;", lcd)

    self.assertIn("defined(BSP_USING_SDLPAL)", touch)
    self.assertIn("#else", touch)
    self.assertIn("rt_size_t max_points = ST7102_MAX_TOUCH;", touch)
    self.assertIn("defined(ST7102_HOST_TEST)", touch)

    self.assertIn("defined(BSP_USING_SDLPAL) && _BAREMETAL", vg_hal)
    self.assertIn("vg_lite_hal_free(device);", vg_hal)
```

Run it before copying implementations. Expected: FAIL on main shared files.

- [ ] **Step 2: Port and guard the LCD changes**

Port the isolated-worktree indexed implementation, but use this activation:

```c
#if defined(BSP_USING_LVGL) || LCD_ROTATION_BACKEND_VGLITE || \
    (defined(BSP_USING_SDLPAL) && defined(BSP_LCD_VGLITE_INDEXED))
#define LCD_VGLITE_REQUIRED 1
#else
#define LCD_VGLITE_REQUIRED 0
#endif
```

Guard indexed dimensions, `.cy_gpu_buf.sdlpal_indexed`, and
`lcd_blit_indexed8` with:

```c
#if defined(BSP_USING_SDLPAL) && defined(BSP_LCD_VGLITE_INDEXED)
/* SDLPal indexed implementation */
#endif
```

Preserve the original RGB565 format when the project macro is absent:

```c
#ifdef BSP_USING_SDLPAL
buffer->format = VG_LITE_BGR565;
#else
buffer->format = VG_LITE_RGB565;
#endif
```

Guard `vglite_failed`, parameter zeroing, and failure latching with
`BSP_USING_SDLPAL`; retain the original initializer path otherwise.

- [ ] **Step 3: Port and guard the ST7102 parser changes**

Under `BSP_USING_SDLPAL`, use caller `read_num` as writable capacity, cap it
to `ST7102_MAX_TOUCH`, parse only the minimum readable/writable count, require
`ST7102_POINT_VALID_MASK`, and release only writable slots. Under `#else`,
retain the exact original `max_points` parser and release loop.

Expose host hooks only with:

```c
#if defined(BSP_USING_SDLPAL) && defined(ST7102_HOST_TEST)
/* ST7102_host_test_read_point and ST7102_host_test_reset */
#endif
```

Compile the host driver test with both `-DBSP_USING_SDLPAL` and
`-DST7102_HOST_TEST`.

- [ ] **Step 4: Port and guard the VG-Lite HAL shutdown fix**

Use:

```c
#if defined(BSP_USING_SDLPAL) && _BAREMETAL
        device = NULL;
#else
        vg_lite_hal_free(device);
#if defined(BSP_USING_SDLPAL)
        device = NULL;
#endif
#endif
```

This makes the no-macro branch textually and behaviorally identical to main.

- [ ] **Step 5: Run shared-isolation and host tests**

```powershell
rtk python -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host clean all
```

Expected: all contract tests and host C tests pass.

- [ ] **Step 6: Commit the isolated platform integration**

```powershell
rtk git add projects\Edgi_Talk_M55_SDLPAL libraries\HAL_Drivers\drv_lcd.c libraries\Common\board\ports\display_panels\drv_touch.c libraries\components\mtb-device-support-pse8xxgp\pdl\drivers\third_party\COMPONENT_GFXSS\vsi\gcnano\vg_lite_hal.c
rtk git commit -m "feat: isolate SDLPal shared driver integration"
```

Expected: no shared Kconfig, shared header, root README, or unrelated project file is staged.

### Task 5: Register Only the New SDK Project

**Files:**
- Modify: `sdk-bsp-psoc_e84-edgi-talk.yaml`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`

**Interfaces:**
- Produces: one discoverable `Edgi_Talk_M55_SDLPAL` example entry.

- [ ] **Step 1: Run the existing manifest contract and verify RED**

```powershell
rtk python -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract.ProjectContractTest.test_delivery_tools_and_documentation
```

Expected: FAIL because the main manifest does not yet contain the SDLPal entry.

- [ ] **Step 2: Add the isolated branch's SDLPal manifest entry**

Insert one `project_name: Edgi_Talk_M55_SDLPAL` entry before the existing
`Edgi_Talk_M55_LVGL` entry. Include the project directory, `projects/libs`,
shared `libraries`, `rt-thread`, and repository `tools`, plus the existing GNU
10.2.1 and OpenOCD 2.0.0 package declarations. Do not edit other entries.

- [ ] **Step 3: Run the manifest contract and YAML-oriented project tests**

Expected: PASS, and `git diff` shows one manifest block only.

- [ ] **Step 4: Commit manifest registration**

```powershell
rtk git add sdk-bsp-psoc_e84-edgi-talk.yaml projects\Edgi_Talk_M55_SDLPAL\tests\host\test_project_contract.py
rtk git commit -m "chore: register SDLPal example project"
```

### Task 6: Verify SDLPal and Existing-Project Isolation

**Files:**
- Generate ignored: `projects/Edgi_Talk_M55_SDLPAL/build/**`
- Generate ignored: `projects/Edgi_Talk_M55_SDLPAL/rt-thread.elf`
- Generate ignored: `projects/Edgi_Talk_M55_SDLPAL/rtthread.hex`
- Generate ignored: `projects/Edgi_Talk_M55_SDLPAL/reports/**`
- Generate ignored: `projects/Edgi_Talk_M55_LVGL/build/**`

**Interfaces:**
- Consumes: complete migrated project and guarded shared code.
- Produces: verified 90-degree firmware, four rotation reports, and evidence that LVGL remains unchanged.

- [ ] **Step 1: Run complete host and Python suites**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host clean all
rtk python -m unittest discover -s projects\Edgi_Talk_M55_SDLPAL\tests\host -p test_*.py
```

Expected: all host targets pass and 32 Python tests pass, adjusted only if the isolation test increases the count.

- [ ] **Step 2: Build SDLPal and validate stack/ELF**

Build with the approved GCC path, then run:

```powershell
rtk python tools\check_stack_usage.py --root sdlpal --limit 12288
rtk python tools\check_elf.py --elf rt-thread.elf --map rtthread.map --nm "D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin\arm-none-eabi-nm.exe" --rotation 90
```

Expected: max function frame <= 12,288 bytes; hot SDLPal symbols in ITCM; at least 65,536 bytes ITCM reserved.

- [ ] **Step 3: Run the four-rotation matrix and restore 90 degrees**

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File projects\Edgi_Talk_M55_SDLPAL\tools\build_matrix.ps1
```

Expected: PASS reports for 0, 90, 180, and 270; `.config` and `rtconfig.h` restored to 90 degrees.

- [ ] **Step 4: Rebuild the final 90-degree SDLPal firmware**

Run normal SCons from the SDLPal project and rerun `check_elf.py --rotation 90`.

Expected: final `rtthread.hex` matches the restored default configuration.

- [ ] **Step 5: Rebuild LVGL and compare the baseline**

Run SCons from `projects/Edgi_Talk_M55_LVGL`, record `arm-none-eabi-size`, and compare it with `%TEMP%/sdlpal-migration-lvgl-size-before.txt`.

Expected: build exit 0 and identical `text`, `data`, and `bss` sizes. If sizes differ, stop and inspect preprocessed shared branches before proceeding.

- [ ] **Step 6: Run final hygiene checks**

```powershell
rtk git diff --check
rtk git status --short
rtk git diff e1c6447 --name-only
```

Verify no generated artifact is tracked, no root README or shared Kconfig/header changed, no unrelated project changed, and `.w/s` still appears in `git worktree list`.

- [ ] **Step 7: Review the implementation commits**

Confirm the implementation satisfies the approved design and report the exact firmware path, ITCM usage, tests, LVGL comparison, and retained worktree path.
