# SDLPal PSoC Edge Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a no-LVGL SDLPal target for the PSoC Edge M55 board that loads raw PAL resources from `/sdcard/pal`, renders on the physical portrait LCD, and supports color-block multitouch controls while keeping the per-frame path in on-chip SRAM.

**Architecture:** Create `projects/Edgi_Talk_M55_SDLPAL` from the board/build configuration of `Edgi_Talk_M55_LVGL`, but remove every LVGL source and configuration dependency. Vendor the pinned SDLPal embedded core and its minimal SDL shim, add a PSoC-specific direct indexed-video hook, and keep platform code behind small display, touch, storage, memory, and timing interfaces.

**Tech Stack:** C99, RT-Thread, SCons, ARM GNU Toolchain, PSoC Edge GFXSS/VG-Lite, ST7102 touch, SDIO/DFS/Elm FAT, MinGW GCC host tests.

## Global Constraints

- The target project must not compile or link LVGL.
- The default display configuration is physical portrait `480x800`, rotation `0`.
- `BSP_LCD_ROTATION_DEGREES` is the only rotation setting; `90/270` use VG-Lite and `180` uses GFXSS layer rotation.
- Game data comes from raw files under `/sdcard/pal`; `pal_full.pak` and target pack providers are forbidden.
- Saves are stored under `/sdcard/pal/save`.
- Audio source, audio devices, and audio threads are excluded from this phase.
- Two aligned 64 KiB indexed framebuffer slots belong in DTCM.
- Generic `malloc` must not fall back to HyperRAM; HyperRAM is used only by explicit cold-data allocation.
- All shell commands in this repository are prefixed with `rtk` per `C:\Users\RTT\.codex\RTK.md`.
- SDLPal source is pinned to `D:\workspace_rb\OpenSouce\sdlpal-embedded`, commit `2317762` (`origin/extreme`).

---

## File Map

The implementation creates or changes these ownership units:

- `projects/Edgi_Talk_M55_SDLPAL/{SConstruct,SConscript,Kconfig,.config,rtconfig.h,rtconfig.py}`: target build and RT-Thread configuration.
- `projects/Edgi_Talk_M55_SDLPAL/board/`: copied board startup plus linker ownership for indexed framebuffers.
- `projects/Edgi_Talk_M55_SDLPAL/applications/main.c`: boot sequencing and SDLPal thread entry only.
- `projects/Edgi_Talk_M55_SDLPAL/platform/pal_display_core.[ch]`: pure RGB565 conversion, 3:2 scaling, and control-block drawing.
- `projects/Edgi_Talk_M55_SDLPAL/platform/pal_touch_core.[ch]`: pure rotation, hit testing, and multi-contact key-mask logic.
- `projects/Edgi_Talk_M55_SDLPAL/platform/pal_display_port.[ch]`: LCD area flush, frame submission, status pages, and timing metrics.
- `projects/Edgi_Talk_M55_SDLPAL/platform/pal_touch_port.[ch]`: ST7102 RT-Thread device adapter and SDL event generation.
- `projects/Edgi_Talk_M55_SDLPAL/platform/pal_storage.[ch]`: mount wait, resource validation, and save-directory creation.
- `projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.[ch]`: fixed DTCM buffers, explicit HyperRAM allocation, and `pal_mem` metrics.
- `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_bridge.c`: indexed present, event, clock, delay, and engine startup hooks.
- `projects/Edgi_Talk_M55_SDLPAL/platform/pal_config.h`: SDLPal feature and path configuration.
- `projects/Edgi_Talk_M55_SDLPAL/platform/pal_backend_stubs.c`: AVI and non-selected player stubs required by the no-audio link.
- `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/`: pinned GPLv3 SDLPal source and minimal SDL shim.
- `projects/Edgi_Talk_M55_SDLPAL/sdlpal/SConscript`: exact source inventory and compile definitions.
- `projects/Edgi_Talk_M55_SDLPAL/tests/host/`: executable pure-C and project-contract tests.
- `projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py`: LVGL/audio symbol and RAM-section checks.

---

### Task 1: Create the no-LVGL target skeleton

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/.config`
- Create: `projects/Edgi_Talk_M55_SDLPAL/Kconfig`
- Create: `projects/Edgi_Talk_M55_SDLPAL/SConstruct`
- Create: `projects/Edgi_Talk_M55_SDLPAL/SConscript`
- Create: `projects/Edgi_Talk_M55_SDLPAL/rtconfig.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/rtconfig.py`
- Create: `projects/Edgi_Talk_M55_SDLPAL/board/SConscript`
- Create: `projects/Edgi_Talk_M55_SDLPAL/board/board.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/board/board.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/board/linker_scripts/link.ld`
- Create: `projects/Edgi_Talk_M55_SDLPAL/applications/SConscript`
- Create: `projects/Edgi_Talk_M55_SDLPAL/applications/main.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`

**Interfaces:**
- Consumes: existing `Edgi_Talk_M55_LVGL` board startup and `Edgi_Talk_M55_Driver_All` DFS/SDIO configuration.
- Produces: a buildable RT-Thread application with LCD, touch, HyperRAM, SDIO1, DFS/Elm FAT and POSIX FS enabled, but no LVGL.

- [ ] **Step 1: Write the failing project contract test**

```python
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_target_contract():
    sconstruct = (ROOT / "SConstruct").read_text(encoding="utf-8")
    config = (ROOT / ".config").read_text(encoding="utf-8")
    assert "lvgl_9.2.0/SConscript" not in sconstruct
    assert "CONFIG_USING_LVGL=y" not in config
    assert "CONFIG_BSP_USING_LVGL=y" not in config
    for setting in (
        "CONFIG_BSP_USING_LCD=y",
        "CONFIG_BSP_USING_HYPERAM=y",
        "CONFIG_BSP_USING_FILESYSTEM=y",
        "CONFIG_BSP_USING_SDCARD=y",
        "CONFIG_BSP_USING_SDIO1=y",
        "CONFIG_RT_USING_DFS_ELMFAT=y",
        "CONFIG_BSP_LCD_ROTATION_DEGREES=0",
    ):
        assert setting in config
```

- [ ] **Step 2: Run the contract test and verify it fails**

Run:

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe -m pytest projects\Edgi_Talk_M55_SDLPAL\tests\host\test_project_contract.py -q
```

Expected: FAIL because `projects/Edgi_Talk_M55_SDLPAL` does not exist.

- [ ] **Step 3: Create the minimal project**

Mechanically copy the board, linker, SCons and toolchain files from `Edgi_Talk_M55_LVGL`. Create this application entry:

```c
#include <rtthread.h>

int main(void)
{
    rt_kprintf("SDLPal PSoC Edge target bootstrap\n");
    for (;;)
    {
        rt_thread_mdelay(1000);
    }
}
```

Remove both LVGL `SConscript` calls from `SConstruct`. Do not copy `virtual3d_emoji_demo`, LVGL demo sources, or LVGL packages. Merge the filesystem settings selected by `BSP_USING_FILESYSTEM` and `BSP_USING_SDCARD` into `.config`, retain LCD rotation `0`, and disable `RT_USING_MEMHEAP_AUTO_BINDING`.

- [ ] **Step 4: Regenerate and verify configuration**

Run:

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe -m pytest projects\Edgi_Talk_M55_SDLPAL\tests\host\test_project_contract.py -q
```

Expected: `1 passed`.

- [ ] **Step 5: Build the skeleton**

Run from `projects/Edgi_Talk_M55_SDLPAL`:

```powershell
rtk powershell -NoProfile -Command "`$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -j16"
```

Expected: exit code 0 and `rt-thread.elf` generated.

- [ ] **Step 6: Prove LVGL is absent**

Run:

```powershell
rtk D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin\arm-none-eabi-nm.exe rt-thread.elf
```

Expected: no symbol beginning with `lv_` and no `lvgl_thread_init`.

- [ ] **Step 7: Commit**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL
rtk git commit -m "feat: add no-LVGL SDLPal target skeleton"
```

---

### Task 2: Implement and test the portrait RGB565 renderer

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_display_core.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_display_core.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_display_core.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Interfaces:**
- Consumes: `320x200` indexed pixels and 256 RGB colors.
- Produces: `pal_display_palette_set()`, `pal_display_convert_rows()`, and `pal_display_draw_controls()`.

- [ ] **Step 1: Define the public renderer contract and failing tests**

```c
#define PAL_GAME_WIDTH 320u
#define PAL_GAME_HEIGHT 200u
#define PAL_PORTRAIT_WIDTH 480u
#define PAL_GAME_VIEW_HEIGHT 300u

typedef struct pal_rgb {
    uint8_t r, g, b;
} pal_rgb_t;

typedef struct pal_display_palette {
    uint16_t rgb565[256];
} pal_display_palette_t;

void pal_display_palette_set(pal_display_palette_t *palette,
                             const pal_rgb_t colors[256]);
size_t pal_display_convert_rows(const uint8_t *indexed, size_t src_pitch,
                                uint16_t first_dst_y, uint16_t row_count,
                                const pal_display_palette_t *palette,
                                uint16_t *dst, size_t dst_pitch_pixels);
void pal_display_draw_controls(uint16_t *dst, size_t pitch,
                               uint16_t width, uint16_t height,
                               uint32_t pressed_mask);
```

The tests assert `red -> 0xf800`, `green -> 0x07e0`, `blue -> 0x001f`; destination x values `0,1,2` map to source x `0,0,1`; destination y values `0,1,2` map to source y `0,0,1`; and a pressed A block differs from the released A block.

- [ ] **Step 2: Run the display test and verify it fails**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_display_core
```

Expected: compilation fails because `pal_display_core` is not implemented.

- [ ] **Step 3: Implement RGB565 lookup and exact 3:2 nearest-neighbor scaling**

Use these integer mappings in `pal_display_convert_rows()`:

```c
src_y = ((uint32_t)dst_y * 2u) / 3u;
src_x = ((uint32_t)dst_x * 2u) / 3u;
dst[row * dst_pitch_pixels + dst_x] =
    palette->rgb565[indexed[src_y * src_pitch + src_x]];
```

Draw controls with fixed RGB565 colors after the indexed conversion. Use rectangular blocks for four directions, A, B, PgUp and PgDn; brighten a block when its bit is present in `pressed_mask`.

- [ ] **Step 4: Run host tests**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_display_core
```

Expected: executable exits 0 and prints `display_core: PASS`.

- [ ] **Step 5: Commit**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL/platform projects/Edgi_Talk_M55_SDLPAL/tests/host
rtk git commit -m "feat: add indexed portrait renderer"
```

---

### Task 3: Implement and test rotation-aware multitouch controls

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_touch_core.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_touch_core.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_touch_core.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Interfaces:**
- Consumes: up to five physical `480x800` touch contacts and `BSP_LCD_ROTATION_DEGREES`.
- Produces: `pal_touch_transform()` and `pal_touch_controls()` returning `PAL_CONTROL_*` bits.

- [ ] **Step 1: Write failing coordinate and multi-contact tests**

```c
typedef struct pal_touch_point {
    uint16_t x;
    uint16_t y;
    uint8_t id;
    uint8_t active;
} pal_touch_point_t;

typedef enum pal_control {
    PAL_CONTROL_UP    = 1u << 0,
    PAL_CONTROL_DOWN  = 1u << 1,
    PAL_CONTROL_LEFT  = 1u << 2,
    PAL_CONTROL_RIGHT = 1u << 3,
    PAL_CONTROL_A     = 1u << 4,
    PAL_CONTROL_B     = 1u << 5,
    PAL_CONTROL_PGUP  = 1u << 6,
    PAL_CONTROL_PGDN  = 1u << 7
} pal_control_t;
```

Tests cover all four rotations, inactive contacts, block-edge inclusivity, sliding from left to up, and two simultaneous contacts producing `PAL_CONTROL_RIGHT | PAL_CONTROL_A`.

- [ ] **Step 2: Run and verify failure**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_touch_core
```

Expected: compilation fails because touch functions do not exist.

- [ ] **Step 3: Implement one coordinate transform and one layout table**

Use physical bounds before rotation and return logical bounds after rotation:

```c
switch (rotation) {
case 0:   logical_x = x; logical_y = y; break;
case 90:  logical_x = 799u - y; logical_y = x; break;
case 180: logical_x = 479u - x; logical_y = 799u - y; break;
case 270: logical_x = y; logical_y = 479u - x; break;
default: return false;
}
```

Apply optional swap/invert calibration before rotation. OR the hit result of every active point so independent fingers can hold direction and A/B together.

- [ ] **Step 4: Run host tests**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_touch_core
```

Expected: executable exits 0 and prints `touch_core: PASS`.

- [ ] **Step 5: Commit**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL/platform projects/Edgi_Talk_M55_SDLPAL/tests/host
rtk git commit -m "feat: add multitouch control mapping"
```

---

### Task 4: Add LCD, ST7102, and boot-status ports

**Files:**
- Create: `libraries/HAL_Drivers/drv_lcd.h`
- Modify: `libraries/HAL_Drivers/drv_lcd.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_display_port.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_display_port.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_touch_port.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_touch_port.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_status.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_status.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/SConscript`

**Interfaces:**
- Consumes: display/touch core functions and existing LCD/ST7102 drivers.
- Produces: `pal_display_present_indexed()`, `pal_touch_port_poll()`, and `pal_status_show()`.

- [ ] **Step 1: Add compile-time API tests**

Create a host compile test that includes all new headers and calls:

```c
bool pal_display_present_indexed(const uint8_t *pixels, size_t pitch,
                                 const pal_rgb_t palette[256]);
bool pal_touch_port_poll(pal_touch_point_t *points, size_t capacity,
                         size_t *count);
void pal_status_show(uint16_t background, const char *code,
                     const char *detail);
```

Run `rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_port_headers`; expect compilation failure.

- [ ] **Step 2: Expose the LCD API in a shared header**

Declare the existing functions without changing behavior:

```c
void lcd_flush_rgb565(const void *pixels, uint32_t width, uint32_t height);
void lcd_flush_rgb565_area(const void *pixels, uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height,
                           uint32_t src_stride, rt_bool_t present);
rt_err_t lcd_wait_frame_done(uint32_t timeout_ms);
```

- [ ] **Step 3: Implement strip conversion and present**

Allocate one 16-line `480x16` RGB565 buffer in `.cy_gpu_buf`. Convert destination rows `0..299` in strips, flush each with `present=false`, redraw the control region only when its state changes, and present on the last area call. Record current, maximum, and count for conversion/present microseconds.

- [ ] **Step 4: Implement ST7102 batch input**

Call `rt_hw_ST7102_port()` once, find and open device `ST7102`, read an array of five `struct rt_touch_data`, and expose only `RT_TOUCH_EVENT_DOWN` and `RT_TOUCH_EVENT_MOVE` as active contacts. A read error yields zero active contacts and releases prior keys.

- [ ] **Step 5: Implement the no-LVGL status screen**

Use a built-in 5x7 ASCII glyph set for `A-Z`, `0-9`, slash, dot, dash and underscore. Render a full-screen RGB565 background plus code/detail strings through `lcd_flush_rgb565_area()`; this path must work before SDLPal starts.

- [ ] **Step 6: Run tests and target build**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_port_headers
rtk powershell -NoProfile -Command "Set-Location 'projects\Edgi_Talk_M55_SDLPAL'; `$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -j16"
```

Expected: host header test and target build both exit 0.

- [ ] **Step 7: Commit**

```powershell
rtk git add libraries/HAL_Drivers projects/Edgi_Talk_M55_SDLPAL
rtk git commit -m "feat: add direct LCD and touch ports"
```

---

### Task 5: Add SD resource and save-path validation

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_storage.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_storage.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_storage.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Interfaces:**
- Consumes: POSIX `stat()`/`mkdir()` on RT-Thread DFS.
- Produces: `pal_storage_validate()` and `pal_storage_prepare_save_dir()`.

- [ ] **Step 1: Write failing validator tests**

```c
typedef bool (*pal_storage_probe_fn)(const char *path, void *context);

typedef struct pal_storage_result {
    bool ready;
    char missing_name[16];
} pal_storage_result_t;

pal_storage_result_t pal_storage_validate(const char *root,
                                          pal_storage_probe_fn probe,
                                          void *context);
```

The fake probe first reports every required file present, then reports only `map.mkf` missing. Assert `ready=true` for the first case and `missing_name == "map.mkf"` for the second.

- [ ] **Step 2: Verify failure**

Run `rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_storage`.

Expected: compilation fails because storage functions are absent.

- [ ] **Step 3: Implement exact required-file validation**

Use this fixed array:

```c
static const char *const required_files[] = {
    "abc.mkf", "ball.mkf", "data.mkf", "f.mkf", "fbp.mkf",
    "fire.mkf", "gop.mkf", "map.mkf", "mgo.mkf", "pat.mkf",
    "rgm.mkf", "rng.mkf", "sss.mkf", "word.dat", "m.msg"
};
```

Treat `desc.dat` as optional. `pal_storage_prepare_save_dir()` accepts an existing directory or creates it with mode `0777`; a non-directory at that path is an error.

- [ ] **Step 4: Run tests and commit**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_storage
rtk git add projects/Edgi_Talk_M55_SDLPAL/platform projects/Edgi_Talk_M55_SDLPAL/tests/host
rtk git commit -m "feat: validate raw PAL resources"
```

Expected: `storage: PASS`, then a successful commit.

---

### Task 6: Vendor the pinned SDLPal core and minimal SDL shim

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/UPSTREAM.md`
- Create: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/SConscript`
- Create: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/LICENSE`
- Create: selected files under `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_config.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_backend_stubs.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_sdl_shim.c`

**Interfaces:**
- Consumes: pinned SDLPal `2317762` and platform hooks declared in Tasks 2-5.
- Produces: `PAL_EngineMain()`, SDL surfaces/events/palette operations, and a no-audio SDLPal link.

- [ ] **Step 1: Write a failing SDL shim test**

Create two 8-bit external surfaces, assign a palette, blit a `2x2` rectangle, push key-down/key-up events, and assert both destination pixels and `SDL_GetKeyboardState()` transitions. Run `rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_sdl_shim`; expect missing headers/sources.

- [ ] **Step 2: Import the pinned source mechanically**

Import these core C files and their same-directory headers from `2317762`:

```text
battle.c ending.c fight.c font.c game.c global.c input.c itemmenu.c
magicmenu.c main.c map.c overlay.c palcfg.c palcommon.c paldebug.c
palette.c play.c res.c rngplay.c scene.c script.c text.c ui.c
uibattle.c uigame.c util.c video.c yj1.c
```

Also import all root headers required by those files, `sdl_compat/sdl_compat.[ch]`, `esp32s3/native_engine_shim/include/{SDL.h,SDL_endian.h,SDL_events.h,SDL_video.h}`, `esp32s3/native_engine_shim/sdl_shim.c`, `unix/contract_noaudio.c`, and `LICENSE`.

Record repository URL, branch, commit, import date, imported inventory and local patch list in `UPSTREAM.md`.

- [ ] **Step 3: Define the PSoC SDLPal configuration**

`pal_config.h` sets:

```c
#define PAL_PREFIX "/sdcard/pal/"
#define PAL_SAVE_PREFIX "/sdcard/pal/save/"
#define PAL_CONFIG_PREFIX PAL_PREFIX
#define PAL_PLATFORM "Infineon PSoC Edge M55"
#define PAL_DEFAULT_WINDOW_WIDTH 320
#define PAL_DEFAULT_WINDOW_HEIGHT 200
#define PAL_DEFAULT_TEXTURE_WIDTH 320
#define PAL_DEFAULT_TEXTURE_HEIGHT 200
#define PAL_HAS_JOYSTICKS 0
#define PAL_HAS_TOUCH 0
#define PAL_HAS_NATIVEMIDI 0
#define PAL_HAS_MP3 0
#define PAL_HAS_OGG 0
#define PAL_HAS_OPUS 0
#define PAL_HAS_GLSL 0
#define PAL_HAS_CONFIG_PAGE 0
#define PAL_VIDEO_INIT_FLAGS SDL_WINDOW_SHOWN
#define PAL_SDL_INIT_FLAGS (SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE)
#define PAL_FATAL_OUTPUT(s) pal_engine_fatal(s)
```

- [ ] **Step 4: Build only the raw-resource, no-audio source set**

Compile definitions are:

```text
USE_SDL3=0
PAL_HEADLESS_SDL_SHIM=1
PAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS=1
PAL_PSOC_DIRECT_INDEXED=1
PAL_CONTRACT_NO_AUDIO=1
PAL_NO_LAUNCH_UI=1
PAL_HAS_JOYSTICKS=0
PAL_HAS_TOUCH=0
```

Do not define `PAL_NO_RUNTIME_DECOMPRESS`, `PAL_NO_RUNTIME_HEAP`, `MEM_LEVEL1`, `MEM_LEVEL2`, `PAL_CONTRACT_TARGET_PACK_PROVIDER`, or `PAL_EXTREME_TWO_SCREENS`.

Compile upstream `main.c` with `main=PAL_EngineMain`. Supply no-op AVI functions in `pal_backend_stubs.c`; return `NULL` for unselected music/player constructors if a referenced symbol remains after excluding audio backends.

- [ ] **Step 5: Run shim tests and resolve the target link by declared stubs only**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_sdl_shim
rtk powershell -NoProfile -Command "Set-Location 'projects\Edgi_Talk_M55_SDLPAL'; `$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -j16"
```

Expected: shim test prints `sdl_shim: PASS`; target link has no unresolved symbol and no audio object.

- [ ] **Step 6: Commit**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL/sdlpal projects/Edgi_Talk_M55_SDLPAL/platform projects/Edgi_Talk_M55_SDLPAL/tests/host
rtk git commit -m "feat: vendor SDLPal core and minimal SDL shim"
```

---

### Task 7: Connect direct indexed video, input events, and fixed DTCM buffers

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/video.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_bridge.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/board/linker_scripts/link.ld`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_engine_bridge.c`

**Interfaces:**
- Consumes: SDLPal indexed surface/palette, display present, touch mask, and RT-Thread clock.
- Produces: `PalEngineBridge_RenderPresentIndexed()`, `PalEngineBridge_PollEvent()`, `PalEngineBridge_GetTicks()`, `PalEngineBridge_Delay()`, and two fixed framebuffer arrays.

- [ ] **Step 1: Write the failing bridge test**

Feed a synthetic indexed frame and palette to `PalEngineBridge_RenderPresentIndexed()` using a fake display callback; assert one present with `320x200`, pitch 320. Change the touch mask from zero to right+A and back; assert ordered SDL key-down and key-up events for `SDLK_RIGHT` and `SDLK_RETURN`.

- [ ] **Step 2: Verify failure**

Run `rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_engine_bridge`.

Expected: bridge symbols are missing.

- [ ] **Step 3: Add a PSoC-only direct indexed branch to upstream video**

Under `PAL_PSOC_DIRECT_INDEXED`, `VIDEO_Startup()` creates `gpScreen` and `gpScreenBak` from the two external `320x200` arrays, allocates one 256-color palette, and returns without creating SDL Window/Renderer/Texture or an ARGB surface. `VIDEO_UpdateScreen()` calls:

```c
PalEngineBridge_RenderPresentIndexed(gpScreen->pixels,
                                     gpScreen->pitch,
                                     gpScreen->w,
                                     gpScreen->h,
                                     gpPalette->colors);
```

Palette, fade, backup and restore operations continue to use the indexed surfaces. Window resize/fullscreen calls become no-ops only under this target macro.

- [ ] **Step 4: Place both framebuffer slots in DTCM**

```c
#define PAL_INDEXED_SLOT_BYTES (64u * 1024u)
__attribute__((section(".pal_framebuffer"), aligned(64)))
uint8_t pal_framebuffer_primary[PAL_INDEXED_SLOT_BYTES];
__attribute__((section(".pal_framebuffer"), aligned(64)))
uint8_t pal_framebuffer_backup[PAL_INDEXED_SLOT_BYTES];
```

Add a `NOLOAD` `.pal_framebuffer` output section in `m55_data_INTERNAL`, expose start/end symbols, and assert its size is exactly 128 KiB and its end stays below `__StackLimit`.

- [ ] **Step 5: Implement edge-triggered multi-key SDL events**

Track `previous_mask`, `current_mask`, and an eight-entry pending event queue. Emit releases before presses when a finger slides between controls. Map A/B/PgUp/PgDn to Return/Escape/PageUp/PageDown. `PalEngineBridge_GetTicks()` uses `rt_tick_get_millisecond()` and delay uses at least one RT tick for any non-zero delay.

- [ ] **Step 6: Run bridge tests and target build**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_engine_bridge
rtk powershell -NoProfile -Command "Set-Location 'projects\Edgi_Talk_M55_SDLPAL'; `$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -j16"
```

Expected: bridge test passes; map contains `.pal_framebuffer` at `0x20000000..0x2001ffff` or another aligned DTCM range below the stack.

- [ ] **Step 7: Commit**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL
rtk git commit -m "feat: connect SDLPal indexed video and touch input"
```

---

### Task 8: Boot SDLPal from raw SD resources

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/applications/main.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/applications/SConscript`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_bridge.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_boot_state.c`

**Interfaces:**
- Consumes: storage validator, status display, touch/display ports, and `PAL_EngineMain()`.
- Produces: deterministic boot states and one SDLPal game thread.

- [ ] **Step 1: Write a failing boot-state test**

Model `WAIT_SD`, `CHECK_RESOURCES`, `INIT_IO`, `RUN_ENGINE`, and `FATAL`. Assert missing mount remains in `WAIT_SD`, missing `map.mkf` enters `FATAL` with `E02`, and valid storage advances to `RUN_ENGINE` exactly once.

- [ ] **Step 2: Verify failure**

Run `rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_boot_state`.

Expected: boot-state API is absent.

- [ ] **Step 3: Implement boot sequencing**

Wait for `/sdcard` with a one-second retry and red `E01 SD MOUNT` status. Validate resources and show `E02` plus the missing filename on failure. Create `/sdcard/pal/save`, initialize LCD/touch ports, then create one `sdlpal` thread with a 24 KiB stack that calls `PAL_EngineMain(1, argv)`.

Enable the LCD backlight only after the first complete status or game frame. The main thread keeps the board heartbeat LED and does not run game logic.

- [ ] **Step 4: Run tests and target build**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_boot_state
rtk powershell -NoProfile -Command "Set-Location 'projects\Edgi_Talk_M55_SDLPAL'; `$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -j16"
```

Expected: boot test passes and target links with `PAL_EngineMain` plus no audio thread.

- [ ] **Step 5: Commit**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL
rtk git commit -m "feat: boot SDLPal from raw SD resources"
```

---

### Task 9: Enforce SRAM ownership and add memory diagnostics

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/.config`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/rtconfig.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_memory_policy.c`

**Interfaces:**
- Consumes: RT-Thread primary heap and `drv_hyperam_get_memheap()`.
- Produces: explicit `pal_cold_alloc/free()` and `pal_mem` diagnostics.

- [ ] **Step 1: Write a failing allocator-policy test**

Use fake internal and HyperRAM allocators. Assert `pal_hot_alloc()` calls only internal allocation and returns failure without fallback; assert `pal_cold_alloc()` calls only HyperRAM; assert counters capture current and peak bytes by tag.

- [ ] **Step 2: Verify failure**

Run `rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_memory_policy`.

Expected: memory policy functions are absent.

- [ ] **Step 3: Disable transparent HyperRAM fallback**

Ensure both `.config` and generated `rtconfig.h` do not define `RT_USING_MEMHEAP_AUTO_BINDING`. Leave `RT_USING_MEMHEAP_AS_HEAP` enabled for the primary Secondary SRAM heap. `pal_cold_alloc()` calls `rt_memheap_alloc(drv_hyperam_get_memheap(), size)` directly and records the requested tag and size.

- [ ] **Step 4: Add `pal_mem` FinSH output**

Print DTCM framebuffer start/end/size, primary heap total/used/peak, HyperRAM total/used/peak/largest, GFX section start/end, display frame count/max microseconds, and SDLPal thread stack high-water estimate. Print the same summary at engine entry, title-ready, map-ready, battle-entry, and fatal exit hooks where those lifecycle points are available.

- [ ] **Step 5: Run tests, build, and inspect size**

```powershell
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host test_memory_policy
rtk powershell -NoProfile -Command "Set-Location 'projects\Edgi_Talk_M55_SDLPAL'; `$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -j16"
rtk D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin\arm-none-eabi-size.exe -A projects\Edgi_Talk_M55_SDLPAL\rt-thread.elf
```

Expected: tests/build exit 0; `.pal_framebuffer` is 131,072 bytes; no LVGL buffers exist; default portrait GFX fixed use is approximately one 819,200-byte LCD buffer plus the small conversion buffer and driver metadata.

- [ ] **Step 6: Commit**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL
rtk git commit -m "feat: enforce SDLPal memory ownership"
```

---

### Task 10: Add build-matrix checks and perform board acceptance

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tools/build_matrix.ps1`
- Create: `projects/Edgi_Talk_M55_SDLPAL/README.md`
- Modify: `sdk-bsp-psoc_e84-edgi-talk.yaml`
- Modify: `README.md`
- Modify: `README_zh.md`

**Interfaces:**
- Consumes: final ELF/map and four rotation configurations.
- Produces: repeatable static validation plus documented SD-card and board procedure.

- [ ] **Step 1: Write the ELF checker before using it**

The checker runs `arm-none-eabi-nm` and parses the map. It exits non-zero when it finds any symbol prefixed `lv_`, `lvgl_thread_init`, an audio thread/device symbol, a missing `.pal_framebuffer`, a framebuffer size other than 131,072 bytes, DTCM overflow, or GFX overflow.

- [ ] **Step 2: Build all rotation configurations**

`build_matrix.ps1` creates four temporary config copies, selects exactly one of rotation `0/90/180/270`, builds each, runs `check_elf.py`, and preserves a named size report. It restores the default portrait configuration after the matrix.

Run:

```powershell
rtk powershell -NoProfile -File projects\Edgi_Talk_M55_SDLPAL\tools\build_matrix.ps1
```

Expected: four successful builds; `0` has no VG-Lite rotation scanout buffer; `90/270` contain render plus scanout buffers; all remain within 3 MiB GFX SRAM.

- [ ] **Step 3: Document resource preparation and commands**

README lists the 15 required files, `/sdcard/pal/save`, default portrait controls, rotation configuration, build command, `pal_mem`, error codes, and explicit statement that audio is absent.

- [ ] **Step 4: Perform development-board acceptance**

Run these cases and record serial logs plus `pal_mem` output:

1. Boot without SD: red `E01`, then insert SD and observe retry.
2. Remove `map.mkf`: red `E02 map.mkf`.
3. Restore data: reach title and start a new game.
4. Verify palette fades, map scroll, direction, A/B, PgUp/PgDn and direction+A two-finger input.
5. Save, reset, and load the save.
6. Play through scene transitions and one battle for at least 30 minutes.
7. Confirm touch response below 100 ms and display conversion/present p95 below 33 ms.
8. Confirm no monotonic heap growth and report internal SRAM, GFX SRAM, HyperRAM and thread stack peaks.

- [ ] **Step 5: Run final verification**

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe -m pytest projects\Edgi_Talk_M55_SDLPAL\tests\host\test_project_contract.py -q
rtk make -C projects\Edgi_Talk_M55_SDLPAL\tests\host all
rtk powershell -NoProfile -File projects\Edgi_Talk_M55_SDLPAL\tools\build_matrix.ps1
rtk git diff --check
rtk git status --short
```

Expected: all tests and builds pass, `git diff --check` is clean, and only intended project/docs changes are present before commit.

- [ ] **Step 6: Commit**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL sdk-bsp-psoc_e84-edgi-talk.yaml README.md README_zh.md
rtk git commit -m "docs: add SDLPal build and board validation"
```

---

## Execution Checkpoints

- Checkpoint A after Task 1: no-LVGL target skeleton builds.
- Checkpoint B after Task 5: pure renderer, touch and storage tests pass; LCD/status build is integrated.
- Checkpoint C after Task 8: SDLPal raw-resource image links and has deterministic boot behavior.
- Checkpoint D after Task 10: board acceptance and memory report are complete.

Audio design and implementation begin only after Checkpoint D passes.
