# SDLPal RT-Thread SDL Port Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove ESP32-S3 production paths and dependencies from `Edgi_Talk_M55_SDLPAL` while preserving the required SDL shim as an explicit RT-Thread port and keeping the project buildable with SCons.

**Architecture:** Keep SDL-compatible surfaces, palettes, events, and string helpers in the existing lightweight shim, but relocate it under `sdlpal/port/rtthread`. Bind SDL time APIs directly to RT-Thread, leave display and input behind the existing PSoC engine bridge, and reject inactive foreign-platform memory profiles explicitly instead of retaining missing includes.

**Tech Stack:** C99, RT-Thread, SCons, Python 3, PowerShell, MinGW GCC host tests, Arm GNU Toolchain.

## Global Constraints

- Preserve current SDLPal display, input, save, and audio behavior.
- Do not add or link the desktop/full SDL library.
- `RT_TICK_PER_SECOND` remains `1000` for the current PSoC project configuration.
- Do not enable `MEM_LEVEL1`, `MEM_LEVEL2`, `PAL_PAGED_EVENT_STATE`, or `PAL_EXTREME_TWO_SCREENS`.
- Do not revert, stage, or commit pre-existing user changes, including the deleted historical host tests and the modified `sdlpal/upstream/video.c`.
- Production source, build scripts, and resources under `projects/Edgi_Talk_M55_SDLPAL` must not retain ESP32, ESP32-S3, Espressif, `ESP_PLATFORM`, or `esp_attr.h` references.
- Every terminal command is invoked through `rtk` as required by `C:\Users\rb\.codex\RTK.md`.

---

### Task 1: Add a failing RT-Thread SDL time contract test

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/tools/host-tests/sdl_rtthread_time/fakes/rtthread.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tools/host-tests/sdl_rtthread_time/test_sdl_rtthread_time.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tools/run_sdl_rtthread_time_test.ps1`
- Test: `projects/Edgi_Talk_M55_SDLPAL/tools/host-tests/sdl_rtthread_time/test_sdl_rtthread_time.c`

**Interfaces:**
- Consumes: current `SDL_GetTicks()`, `SDL_Delay()`, `SDL_GetPerformanceCounter()`, and `SDL_GetPerformanceFrequency()` declarations from `SDL.h`.
- Produces: a repeatable host contract test that records calls to `rt_tick_get_millisecond()`, `rt_tick_get()`, and `rt_thread_mdelay()`.

- [ ] **Step 1: Create the fake RT-Thread time header**

```c
#ifndef PAL_TEST_RTTHREAD_H
#define PAL_TEST_RTTHREAD_H

#include <stdint.h>

typedef int32_t rt_int32_t;
typedef uint32_t rt_tick_t;
typedef int rt_err_t;

#define RT_TICK_PER_SECOND 1000u
#define RT_TICK_MAX UINT32_MAX

rt_tick_t rt_tick_get(void);
rt_tick_t rt_tick_get_millisecond(void);
rt_err_t rt_thread_mdelay(rt_int32_t milliseconds);

#endif
```

- [ ] **Step 2: Create the time contract test**

```c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "SDL.h"
#include "rtthread.h"

static rt_tick_t fake_tick;
static rt_tick_t fake_milliseconds;
static rt_int32_t delay_calls[4];
static unsigned delay_call_count;

rt_tick_t rt_tick_get(void)
{
    return fake_tick;
}

rt_tick_t rt_tick_get_millisecond(void)
{
    return fake_milliseconds;
}

rt_err_t rt_thread_mdelay(rt_int32_t milliseconds)
{
    assert(delay_call_count < 4u);
    delay_calls[delay_call_count++] = milliseconds;
    return 0;
}

void PalEngineBridge_RenderPresent(const void *pixels, int pitch, int w, int h)
{
    (void)pixels;
    (void)pitch;
    (void)w;
    (void)h;
}

int PalEngineBridge_PollEvent(SDL_Event *event)
{
    (void)event;
    return 0;
}

int main(void)
{
    fake_milliseconds = 0x12345678u;
    fake_tick = 0x00abcdefu;

    assert(SDL_GetTicks() == 0x12345678u);
    assert(SDL_GetPerformanceCounter() == 0x00abcdefu);
    assert(SDL_GetPerformanceFrequency() == RT_TICK_PER_SECOND);

    SDL_Delay(0u);
    assert(delay_call_count == 0u);

    SDL_Delay(25u);
    assert(delay_call_count == 1u);
    assert(delay_calls[0] == 25);

    delay_call_count = 0u;
    SDL_Delay(UINT32_MAX);
    assert(delay_call_count == 3u);
    assert(delay_calls[0] == 2147483646);
    assert(delay_calls[1] == 2147483646);
    assert(delay_calls[2] == 3);

    puts("sdl_rtthread_time: PASS");
    return 0;
}
```

- [ ] **Step 3: Create the host test runner against the current shim path**

```powershell
param(
    [string]$Compiler = "D:\softwoare\tools_dept\mingw64\bin\gcc.exe"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$testRoot = Join-Path $PSScriptRoot "host-tests\sdl_rtthread_time"
$shimRoot = Join-Path $projectRoot "sdlpal\upstream\esp32s3\native_engine_shim"
$output = Join-Path ([IO.Path]::GetTempPath()) ("sdlpal-rtthread-time-{0}.exe" -f $PID)

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "Host compiler not found: $Compiler"
}

$compilerArgs = @(
    "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
    ("-I{0}" -f (Join-Path $testRoot "fakes")),
    ("-I{0}" -f (Join-Path $shimRoot "include")),
    "-DPAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS=1",
    "-DPAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY=1",
    "-DPAL_SDL_SHIM_DYNAMIC_SURFACES=1",
    (Join-Path $testRoot "test_sdl_rtthread_time.c"),
    (Join-Path $shimRoot "sdl_shim.c"),
    "-o", $output
)

try {
    & $Compiler @compilerArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $output
    exit $LASTEXITCODE
}
finally {
    if (Test-Path -LiteralPath $output) {
        Remove-Item -LiteralPath $output -Force
    }
}
```

- [ ] **Step 4: Run the test and verify RED**

Run from `projects/Edgi_Talk_M55_SDLPAL`:

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_sdl_rtthread_time_test.ps1
```

Expected: link failure naming unresolved `PalEngineBridge_GetTicks` and `PalEngineBridge_Delay`. The failure proves the current shim still routes time through the engine bridge.

- [ ] **Step 5: Commit only the failing contract test**

```powershell
rtk git add -- projects/Edgi_Talk_M55_SDLPAL/tools/host-tests/sdl_rtthread_time/fakes/rtthread.h projects/Edgi_Talk_M55_SDLPAL/tools/host-tests/sdl_rtthread_time/test_sdl_rtthread_time.c projects/Edgi_Talk_M55_SDLPAL/tools/run_sdl_rtthread_time_test.ps1
rtk git commit -m "test: define RT-Thread SDL time contract"
```

### Task 2: Bind SDL time directly to RT-Thread

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/sdl_shim.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_bridge.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_bridge.c`
- Test: `projects/Edgi_Talk_M55_SDLPAL/tools/host-tests/sdl_rtthread_time/test_sdl_rtthread_time.c`

**Interfaces:**
- Consumes: `rt_tick_get_millisecond()`, `rt_tick_get()`, `rt_thread_mdelay()`, `RT_TICK_PER_SECOND`, and `RT_TICK_MAX` from RT-Thread.
- Produces: SDL time functions implemented directly on the RTOS; engine bridge retains only display and event interfaces.

- [ ] **Step 1: Include RT-Thread and define a safe delay chunk**

Immediately after `#include "SDL.h"`, add:

```c
#include <rtthread.h>

#include <ctype.h>
#include <limits.h>

#define PAL_SDL_DELAY_MAX_TICKS ((rt_tick_t)(RT_TICK_MAX / 2u - 1u))
#define PAL_SDL_DELAY_MAX_MS_BY_TICK \
    (((uint64_t)PAL_SDL_DELAY_MAX_TICKS * 1000u) / RT_TICK_PER_SECOND)
#define PAL_SDL_DELAY_CHUNK_MS \
    ((Uint32)(PAL_SDL_DELAY_MAX_MS_BY_TICK < (uint64_t)INT_MAX \
                  ? PAL_SDL_DELAY_MAX_MS_BY_TICK \
                  : (uint64_t)INT_MAX))
```

Remove the old standalone `#include <ctype.h>` so the header is included once.

- [ ] **Step 2: Remove the time declarations from the engine hook block**

The hook declarations in `sdl_shim.c` become:

```c
#if PAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS
void PalEngineBridge_RenderPresent(const void *pixels, int pitch, int w, int h);
int PalEngineBridge_PollEvent(SDL_Event *event);
#else
void PalEngineBridge_RenderPresent(const void *pixels, int pitch, int w, int h) __attribute__((weak));
int PalEngineBridge_PollEvent(SDL_Event *event) __attribute__((weak));
#endif
```

Also remove the unused `shim_ticks` fallback state.

- [ ] **Step 3: Replace the four SDL time functions**

```c
Uint32 SDL_GetTicks(void)
{
    return (Uint32)rt_tick_get_millisecond();
}

void SDL_Delay(Uint32 milliseconds)
{
    while (milliseconds > PAL_SDL_DELAY_CHUNK_MS) {
        (void)rt_thread_mdelay((rt_int32_t)PAL_SDL_DELAY_CHUNK_MS);
        milliseconds -= PAL_SDL_DELAY_CHUNK_MS;
    }
    if (milliseconds != 0u) {
        (void)rt_thread_mdelay((rt_int32_t)milliseconds);
    }
}

Uint64 SDL_GetPerformanceCounter(void)
{
    return (Uint64)rt_tick_get();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return (Uint64)RT_TICK_PER_SECOND;
}
```

- [ ] **Step 4: Remove duplicate bridge declarations and definitions**

Delete these declarations from `platform/pal_engine_bridge.h`:

```c
Uint32 PalEngineBridge_GetTicks(void);
void PalEngineBridge_Delay(Uint32 milliseconds);
```

Delete the corresponding functions from `platform/pal_engine_bridge.c`:

```c
Uint32 PalEngineBridge_GetTicks(void)
{
    return (Uint32)rt_tick_get_millisecond();
}

void PalEngineBridge_Delay(Uint32 milliseconds)
{
    if (milliseconds != 0u) {
        rt_thread_mdelay((rt_int32_t)(milliseconds > (Uint32)INT_MAX
                                          ? INT_MAX
                                          : milliseconds));
    }
}
```

- [ ] **Step 5: Run the contract test and verify GREEN**

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_sdl_rtthread_time_test.ps1
```

Expected: `sdl_rtthread_time: PASS` and exit code `0`.

- [ ] **Step 6: Confirm the removed bridge symbols have no callers**

```powershell
rtk rg -n "PalEngineBridge_(GetTicks|Delay)" sdlpal platform audio applications
```

Expected: no matches and `rg` exit code `1`.

- [ ] **Step 7: Commit only the RT-Thread time implementation**

```powershell
rtk git add -- projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/sdl_shim.c projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_bridge.h projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_bridge.c
rtk git commit -m "refactor: bind SDL time to RT-Thread"
```

### Task 3: Move the shim into the RT-Thread port

**Files:**
- Move: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/sdl_shim.c` to `projects/Edgi_Talk_M55_SDLPAL/sdlpal/port/rtthread/sdl_shim.c`
- Move: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/include/*` to `projects/Edgi_Talk_M55_SDLPAL/sdlpal/port/rtthread/include/*`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/SConscript`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/SConscript`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/SConscript`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tools/run_sdl_rtthread_time_test.ps1`
- Test: `projects/Edgi_Talk_M55_SDLPAL/tools/host-tests/sdl_rtthread_time/test_sdl_rtthread_time.c`

**Interfaces:**
- Consumes: the RT-Thread-backed shim produced by Task 2.
- Produces: `sdlpal/port/rtthread/sdl_shim.c` and `sdlpal/port/rtthread/include/SDL.h` as the single SDL compatibility implementation and include root.

- [ ] **Step 1: Move the tracked shim and headers**

```powershell
rtk powershell -NoProfile -Command "New-Item -ItemType Directory -Force projects\Edgi_Talk_M55_SDLPAL\sdlpal\port\rtthread | Out-Null"
rtk git mv projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/sdl_shim.c projects/Edgi_Talk_M55_SDLPAL/sdlpal/port/rtthread/sdl_shim.c
rtk git mv projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/include projects/Edgi_Talk_M55_SDLPAL/sdlpal/port/rtthread/include
```

- [ ] **Step 2: Run the host test and verify the old runner path fails**

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_sdl_rtthread_time_test.ps1
```

Expected: compiler input/path failure naming the removed historical shim path.

- [ ] **Step 3: Update the SDLPal build group**

In `sdlpal/SConscript`, define the port root and use it for source/include paths:

```python
cwd = GetCurrentDir()
upstream = cwd + '/upstream'
rtthread_port = cwd + '/port/rtthread'
```

```python
src += [
    upstream + '/sdl_compat/sdl_compat.c',
    rtthread_port + '/sdl_shim.c',
]
```

```python
path = [
    upstream,
    upstream + '/sdl_compat',
    rtthread_port + '/include',
    cwd + '/../platform',
]
```

- [ ] **Step 4: Update platform and audio include roots**

Replace the historical shim include entry in both `platform/SConscript` and `audio/SConscript` with:

```python
cwd + '/../sdlpal/port/rtthread/include',
```

- [ ] **Step 5: Update the host runner to the new port root**

Replace its `$shimRoot` assignment with:

```powershell
$shimRoot = Join-Path $projectRoot "sdlpal\port\rtthread"
```

- [ ] **Step 6: Remove only the two generated files left in the old directory**

First resolve and verify both files are inside the project, then remove them:

```powershell
rtk powershell -NoProfile -Command "$root=(Resolve-Path 'projects\Edgi_Talk_M55_SDLPAL').Path; $targets=@('projects\Edgi_Talk_M55_SDLPAL\sdlpal\upstream\esp32s3\native_engine_shim\sdl_shim.o','projects\Edgi_Talk_M55_SDLPAL\sdlpal\upstream\esp32s3\native_engine_shim\sdl_shim.su'); foreach($target in $targets){if(Test-Path -LiteralPath $target){$resolved=(Resolve-Path -LiteralPath $target).Path; if(-not $resolved.StartsWith($root + [IO.Path]::DirectorySeparatorChar)){throw \"Refusing to remove outside project: $resolved\"}; Remove-Item -LiteralPath $resolved -Force}}"
```

- [ ] **Step 7: Run the host contract after migration**

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_sdl_rtthread_time_test.ps1
```

Expected: `sdl_rtthread_time: PASS` and exit code `0`.

- [ ] **Step 8: Commit only the path migration**

```powershell
rtk git add -- projects/Edgi_Talk_M55_SDLPAL/sdlpal/port/rtthread projects/Edgi_Talk_M55_SDLPAL/sdlpal/SConscript projects/Edgi_Talk_M55_SDLPAL/platform/SConscript projects/Edgi_Talk_M55_SDLPAL/audio/SConscript projects/Edgi_Talk_M55_SDLPAL/tools/run_sdl_rtthread_time_test.ps1
rtk git commit -m "refactor: move SDL shim to RT-Thread port"
```

### Task 4: Remove inactive foreign-platform source dependencies

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/tools/check_platform_clean.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/defines.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/global.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/res.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/palcommon.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/rngplay.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/uigame.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/UPSTREAM.md`
- Test: `projects/Edgi_Talk_M55_SDLPAL/tools/check_platform_clean.py`

**Interfaces:**
- Consumes: the production source/build tree after Task 3.
- Produces: a persistent production-tree scanner and explicit compile-time rejection of unsupported paged/two-screen profiles.

- [ ] **Step 1: Create the production platform-reference checker**

```python
#!/usr/bin/env python3
"""Reject foreign-platform identifiers in SDLPal production files."""

from __future__ import annotations

import pathlib
import sys


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_ROOTS = ("sdlpal", "platform", "audio", "applications")
ROOT_FILES = ("SConstruct", "SConscript", "Kconfig", "README.md")
SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".inc", ".py", ".md"}
FORBIDDEN = (
    "esp" + "32",
    "esp" + "ressif",
    "esp_" + "platform",
    "esp_" + "attr.h",
)


def production_files() -> list[pathlib.Path]:
    files = [PROJECT_ROOT / name for name in ROOT_FILES]
    for root_name in SOURCE_ROOTS:
        root = PROJECT_ROOT / root_name
        files.extend(
            path
            for path in root.rglob("*")
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
        )
    return sorted(set(files))


def main() -> int:
    matches: list[str] = []
    for path in production_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_number, line in enumerate(text.splitlines(), start=1):
            folded = line.casefold()
            if any(token in folded for token in FORBIDDEN):
                relative = path.relative_to(PROJECT_ROOT)
                matches.append(f"{relative}:{line_number}:{line.strip()}")
    if matches:
        print("Foreign-platform references found:", file=sys.stderr)
        print("\n".join(matches), file=sys.stderr)
        return 1
    print("platform_clean: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Run the checker and verify RED**

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe tools\check_platform_clean.py
```

Expected: exit code `1` with matches from `global.c`, `res.c`, `palcommon.c`, `rngplay.c`, `uigame.c`, and `UPSTREAM.md`.

- [ ] **Step 3: Reject unsupported profiles centrally**

In `sdlpal/upstream/defines.h`, remove the retired board-specific macro block and the old derived-state block, then use:

```c
/*
 * Resource ownership is selected only by MEM_LEVEL1 or MEM_LEVEL2. Keep
 * board wiring, presentation, and storage topology as orthogonal features.
 */
#if defined(MEM_LEVEL1) || defined(PAL_STORAGE_SD_ONLY)
#define PAL_PAGED_EVENT_STATE 1
#endif

#if defined(PAL_PAGED_EVENT_STATE) || defined(PAL_EXTREME_TWO_SCREENS)
#error "Paged and two-screen extreme profiles are unsupported by the RT-Thread port"
#endif
```

Remove the board-specific sentence from the following comment so it describes only the bounded pager behavior.

- [ ] **Step 4: Remove ESP-only attributes while retaining GNU sections**

Delete these include blocks from both `global.c` and `res.c`:

```c
#if defined(ESP_PLATFORM) && defined(MEM_LEVEL2)
#include <esp_attr.h>
#endif
```

In `global.c`, replace the `PAL_GLOBAL_PSRAM` selection with:

```c
#if defined(__GNUC__) && defined(MEM_LEVEL1)
#define PAL_GLOBAL_PSRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#elif defined(__GNUC__)
#define PAL_GLOBAL_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_GLOBAL_PSRAM
#endif
```

In `res.c`, replace the `PAL_RES_PSRAM` selection with:

```c
#if defined(__GNUC__) && defined(MEM_LEVEL1)
#define PAL_RES_PSRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#elif defined(__GNUC__)
#define PAL_RES_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_RES_PSRAM
#endif
```

- [ ] **Step 5: Remove missing foreign-platform includes**

Delete the guarded include of `pal_engine_extreme_save.inc` from `global.c`; delete the guarded provider/memory include pairs from `palcommon.c` and `rngplay.c`; delete the guarded memory include from `uigame.c`. The centralized `#error` in `defines.h` now rejects those unsupported profiles before their guarded code can be selected.

- [ ] **Step 6: Update the upstream integration note**

Replace the first integration paragraph in `sdlpal/UPSTREAM.md` with:

```markdown
The target builds an explicit source allowlist: the game core, compatibility
layer, an RT-Thread headless SDL port, and a PSoC-owned audio contract.
Startup, display, touch, storage, packed-resource, and platform-specific
memory-profile implementations from other targets are not included. Three
`embedded` headers are retained because the source branch includes their
declarations from otherwise portable core files; none of their memory-level
or packed-resource modes is enabled.
```

Change local-difference item 1 to identify `sdlpal/port/rtthread/sdl_shim.c` as the project-maintained RT-Thread shim.

- [ ] **Step 7: Run the checker and contract test GREEN**

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe tools\check_platform_clean.py
rtk powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_sdl_rtthread_time_test.ps1
```

Expected: `platform_clean: PASS`, `sdl_rtthread_time: PASS`, and exit code `0` from both commands.

- [ ] **Step 8: Commit only the production cleanup and checker**

```powershell
rtk git add -- projects/Edgi_Talk_M55_SDLPAL/tools/check_platform_clean.py projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/defines.h projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/global.c projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/res.c projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/palcommon.c projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/rngplay.c projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/uigame.c projects/Edgi_Talk_M55_SDLPAL/sdlpal/UPSTREAM.md
rtk git commit -m "refactor: remove ESP32 SDLPal dependencies"
```

### Task 5: Verify the complete PSoC build and linked contract

**Files:**
- Verify: `projects/Edgi_Talk_M55_SDLPAL/rt-thread.elf`
- Verify: `projects/Edgi_Talk_M55_SDLPAL/rtthread.map`
- Verify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/port/rtthread/sdl_shim.su`

**Interfaces:**
- Consumes: all implementation tasks.
- Produces: fresh host-test, SCons, ELF-layout, stack-usage, symbol, and source-scan evidence.

- [ ] **Step 1: Run both focused checks from the project directory**

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe tools\check_platform_clean.py
rtk powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_sdl_rtthread_time_test.ps1
```

Expected: both print `PASS` and exit `0`.

- [ ] **Step 2: Run a fresh full SCons build**

```powershell
rtk powershell -NoProfile -Command "$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -j16"
```

Expected: exit code `0`, a rebuilt `rt-thread.elf`, `rtthread.map`, and `rtthread.hex`, with no source path under the removed directory.

- [ ] **Step 3: Validate ELF layout and feature contract**

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe tools\check_elf.py --elf rt-thread.elf --map rtthread.map --nm D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin\arm-none-eabi-nm.exe --rotation 90 --input-mode keyboard
```

Expected: checker success and exit code `0`.

- [ ] **Step 4: Validate the static stack limit**

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe tools\check_stack_usage.py --root sdlpal --limit 12288
```

Expected: exit code `0`; every SDLPal static frame remains at or below `12288` bytes.

- [ ] **Step 5: Verify linked SDL and RT-Thread symbols**

```powershell
rtk powershell -NoProfile -Command "& 'D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin\arm-none-eabi-nm.exe' --defined-only rt-thread.elf | Select-String ' SDL_GetTicks$| SDL_Delay$| SDL_GetPerformanceCounter$| SDL_GetPerformanceFrequency$| rt_tick_get$| rt_tick_get_millisecond$| rt_thread_mdelay$'"
rtk rg -n "PalEngineBridge_(GetTicks|Delay)|sdlpal[/\\]upstream[/\\]esp32s3" rtthread.map sdlpal platform audio applications
```

Expected: the four SDL functions and RT-Thread APIs are present in the ELF; the second command has no matches and exits `1`.

- [ ] **Step 6: Check whitespace and isolate task-owned changes**

```powershell
rtk git diff --check
rtk git status --short
```

Expected: no whitespace errors. Pre-existing user deletions and the existing `video.c` modification remain untouched; only task-owned files appear in this task's commits.
