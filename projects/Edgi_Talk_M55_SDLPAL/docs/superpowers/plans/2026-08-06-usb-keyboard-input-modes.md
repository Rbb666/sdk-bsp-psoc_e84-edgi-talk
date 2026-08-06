# SDLPal USB Keyboard Input Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect the enumerated USB Boot keyboard to SDLPal and add a compile-time touch/keyboard input choice with mode-specific full-screen aspect-preserving rendering.

**Architecture:** A new `pal_input_port` owns backend selection and exposes one control-mask API to the game thread. The USB worker maintains an atomic `PAL_CONTROL_*` snapshot while the existing bridge remains the sole SDL event producer. A pure viewport/scaler core drives both VG-Lite and CPU rendering, with touch controls compiled and drawn only in touch mode.

**Tech Stack:** RT-Thread 5, CherryUSB 1.6.0 HID Host, C11, SCons/Kconfig, GNU ld, Python `unittest`, MinGW host tests, PowerShell build matrix.

## Global Constraints

- The active project default is `BSP_SDLPAL_INPUT_USB_KEYBOARD`; menuconfig can select `BSP_SDLPAL_INPUT_TOUCH`.
- Input selection is compile-time only; there is no runtime switch or touch fallback after keyboard disconnect.
- `CONFIG_USBHOST_MAX_INTF_ALTSETTINGS` remains exactly `12` whenever the prebuilt DWC2 Host library is linked.
- Keyboard mappings remain arrows, Enter, Escape, PageUp and PageDown to the existing eight `PAL_CONTROL_*` values.
- Game pixels keep the exact 16:10 aspect ratio in all four rotations.
- Do not modify SDLPal upstream behavior or add non-Boot/NKRO parsing.
- Every production behavior change follows red-green-refactor.

---

### Task 1: Finish the DWC2 ABI Regression Guard

**Files:**
- Modify: `tools/check_elf.py`
- Modify: `tests/host/test_check_elf.py`
- Modify: `tests/host/test_project_contract.py`
- Modify: `board/linker_scripts/link.ld`
- Modify: `.config`
- Modify: `rtconfig.h`
- Modify: `README.md`
- Create: `docs/validation/usb-keyboard-phase1-board.txt`

**Interfaces:**
- Consumes: linker symbols `__usb_host_data_start__`, `__usb_host_data_end__`
- Produces: `USB_HOST_MIN_BYTES`, `USB_HOST_MAX_BYTES`, `usb_host=<bytes>` ELF report field

- [ ] **Step 1: Run the focused regression tests**

Run:

```powershell
python -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_check_elf projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract -v
```

Expected: all tests pass, including rejection of undersized, out-of-range and overlapping Host state.

- [ ] **Step 2: Run the real ELF check**

Run `tools/check_elf.py` for the current 90-degree image with the workspace `arm-none-eabi-nm.exe`.

Expected fields: `usb_host=32920` and `dtcm_headroom=19232`.

- [ ] **Step 3: Verify the prebuilt ABI offset**

Use `arm-none-eabi-objdump` on `USBH_IRQHandler` and `arm-none-eabi-nm` on `g_usbhost_bus`.

Expected: `g_usbhost_bus` size is `0x3e0c`; the interrupt reads offset `0x3de0`, which is inside that object.

- [ ] **Step 4: Commit**

```bash
git add libraries/Common/board/ports/usb/usb_config.h libraries/components/CherryUSB-1.6.0/Kconfig projects/Edgi_Talk_M55_SDLPAL
git commit -m "feat: add SDLPal USB keyboard host"
```

### Task 2: Maintain an Atomic Keyboard Control Snapshot

**Files:**
- Modify: `platform/pal_usb_keyboard_port.h`
- Modify: `platform/pal_usb_keyboard_port.c`
- Modify: `tests/host/test_usb_keyboard_port.c`

**Interfaces:**
- Consumes: `uint32_t pal_usb_keyboard_control(uint8_t usage)`
- Produces: `uint32_t pal_usb_keyboard_controls_get(void)`

- [ ] **Step 1: Write failing port tests**

Add tests that feed worker reports and assert:

```c
assert(pal_usb_keyboard_controls_get() == 0u);
feed_report(KEY_UP | KEY_ENTER);
assert(pal_usb_keyboard_controls_get() ==
       (PAL_CONTROL_UP | PAL_CONTROL_A));
feed_report(KEY_ENTER);
assert(pal_usb_keyboard_controls_get() == PAL_CONTROL_A);
disconnect_keyboard();
assert(pal_usb_keyboard_controls_get() == 0u);
```

Also assert an unmapped usage leaves the mask unchanged and a transfer error clears held controls.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
mingw32-make -C tests/host test_usb_keyboard_port.exe
tests/host/test_usb_keyboard_port.exe
```

Expected: compile or assertion failure because `pal_usb_keyboard_controls_get()` is absent.

- [ ] **Step 3: Implement the snapshot**

In the worker event callback, map the usage and update a `volatile uint32_t` under `rt_hw_interrupt_disable()`:

```c
if (event->pressed) {
    keyboard_control_mask |= control;
} else {
    keyboard_control_mask &= ~control;
}
```

Return a protected snapshot from `pal_usb_keyboard_controls_get()`. Route disconnect, generation changes and transfer errors through `pal_usb_keyboard_release_all()` so all held bits clear.

- [ ] **Step 4: Run port and decoder tests and verify GREEN**

Run `mingw32-make -C tests/host clean all`.

Expected: all C/C++ host tests pass.

- [ ] **Step 5: Commit**

```bash
git add projects/Edgi_Talk_M55_SDLPAL/platform/pal_usb_keyboard_port.* projects/Edgi_Talk_M55_SDLPAL/tests/host
git commit -m "feat: expose USB keyboard controls"
```

### Task 3: Add the Compile-Time Input Port and Kconfig Choice

**Files:**
- Create: `platform/pal_input_port.h`
- Create: `platform/pal_input_port.c`
- Create: `tests/host/test_input_port.c`
- Create: `tests/host/fakes_input/pal_touch_port.h`
- Create: `tests/host/fakes_input/pal_usb_keyboard_port.h`
- Modify: `platform/pal_engine_bridge.c`
- Modify: `applications/main.c`
- Modify: `platform/SConscript`
- Modify: `Kconfig`
- Modify: `.config`
- Modify: `rtconfig.h`
- Modify: `tests/host/Makefile`
- Modify: `tests/host/test_project_contract.py`

**Interfaces:**
- Produces: `bool pal_input_port_init(void)` and `uint32_t pal_input_port_poll(void)`
- Consumes: `pal_touch_port_*` in touch mode, `pal_usb_keyboard_host_start()` and `pal_usb_keyboard_controls_get()` in keyboard mode

- [ ] **Step 1: Write failing mode tests**

Compile `pal_input_port.c` twice with fake backends:

```c
#define BSP_SDLPAL_INPUT_TOUCH 1
assert(pal_input_port_init());
assert(pal_input_port_poll() == PAL_CONTROL_LEFT);
```

```c
#define BSP_SDLPAL_INPUT_USB_KEYBOARD 1
assert(pal_input_port_init());
assert(pal_input_port_poll() == (PAL_CONTROL_RIGHT | PAL_CONTROL_A));
```

The touch test also asserts `pal_display_controls_set()` receives the touch mask; the keyboard test asserts it is not called.

- [ ] **Step 2: Verify RED**

Run the two new host executables.

Expected: failure because `pal_input_port` does not exist.

- [ ] **Step 3: Implement `pal_input_port`**

Use mutually exclusive preprocessor branches and reject invalid builds:

```c
#if defined(BSP_SDLPAL_INPUT_TOUCH) == defined(BSP_SDLPAL_INPUT_USB_KEYBOARD)
#error "Select exactly one SDLPal input mode"
#endif
```

Touch mode polls points, transforms them with the current rotation and updates display controls. Keyboard mode starts Host and returns the atomic keyboard mask.

- [ ] **Step 4: Rewire startup and the engine bridge**

Replace direct touch/USB initialization in `main.c` with `pal_input_port_init()`. Replace direct touch polling in `PalEngineBridge_PollEvent()` with `pal_input_port_poll()` while retaining the existing event queue.

- [ ] **Step 5: Add the Kconfig choice and conditional source selection**

Define default keyboard and select CherryUSB DWC2/HID only from the keyboard choice. Filter backend source files in `platform/SConscript` so the unselected port is not linked.

- [ ] **Step 6: Run tests and verify GREEN**

Run host tests and Python contract tests.

Expected: both fake mode builds and all existing bridge/touch tests pass.

- [ ] **Step 7: Commit**

```bash
git add projects/Edgi_Talk_M55_SDLPAL/Kconfig projects/Edgi_Talk_M55_SDLPAL/.config projects/Edgi_Talk_M55_SDLPAL/rtconfig.h projects/Edgi_Talk_M55_SDLPAL/applications projects/Edgi_Talk_M55_SDLPAL/platform projects/Edgi_Talk_M55_SDLPAL/tests
git commit -m "feat: select SDLPal input backend"
```

### Task 4: Generalize Viewport and Indexed Scaling

**Files:**
- Modify: `platform/pal_display_core.h`
- Modify: `platform/pal_display_core.c`
- Modify: `tests/host/test_display_core.c`

**Interfaces:**
- Produces: `pal_display_viewport_t` and `bool pal_display_viewport_get(uint16_t width, uint16_t height, bool touch_controls, pal_display_viewport_t *viewport)`
- Produces: `size_t pal_display_convert_scaled_rows(..., uint16_t dst_width, uint16_t dst_height, ...)`

- [ ] **Step 1: Write failing viewport tests**

Assert exact rectangles:

```c
expect_viewport(480, 800, true, 0, 0, 480, 300);
expect_viewport(800, 480, true, 160, 0, 480, 300);
expect_viewport(480, 800, false, 0, 250, 480, 300);
expect_viewport(800, 480, false, 16, 0, 768, 480);
```

Assert every result satisfies `width * 5 == height * 8`.

- [ ] **Step 2: Write failing scaler boundary tests**

Use a 320x200 indexed test image whose corners have unique indices. Convert the first and last output rows for 480x300 and 768x480 and assert they select the matching source edges.

- [ ] **Step 3: Verify RED**

Run `test_display_core.exe` and confirm missing API failures.

- [ ] **Step 4: Implement viewport and scaling**

Touch returns the legacy fixed rectangle. Keyboard computes the largest centered 8:5 rectangle. The scaler uses:

```c
src_x = (uint32_t)dst_x * PAL_GAME_WIDTH / dst_width;
src_y = (uint32_t)dst_y * PAL_GAME_HEIGHT / dst_height;
```

- [ ] **Step 5: Verify GREEN and refactor legacy callers**

Run all display/core host tests. Keep `pal_display_convert_rows()` only as a compatibility wrapper if an existing test or caller still needs it.

- [ ] **Step 6: Commit**

```bash
git add projects/Edgi_Talk_M55_SDLPAL/platform/pal_display_core.* projects/Edgi_Talk_M55_SDLPAL/tests/host/test_display_core.c
git commit -m "feat: compute SDLPal game viewport"
```

### Task 5: Apply Mode-Specific Rendering

**Files:**
- Modify: `platform/pal_display_port.h`
- Modify: `platform/pal_display_port.c`
- Modify: `tests/host/test_display_port.c`
- Modify: `tests/host/fakes_display/pal_lcd_api.h`

**Interfaces:**
- Consumes: `pal_display_viewport_get()` and `pal_display_convert_scaled_rows()`
- Produces: mode-correct VG-Lite destination rectangle, CPU fallback and first-frame black bars

- [ ] **Step 1: Write failing display-port tests**

In keyboard landscape mode assert `lcd_blit_indexed8()` receives `(16, 0, 768, 480)`, no control-region rendering occurs, and black side bars are flushed on the first frame. In keyboard portrait mode assert `(0, 250, 480, 300)` with top/bottom clearing. Retain existing touch-mode expectations.

- [ ] **Step 2: Verify RED**

Run `test_display_port.exe` for touch and keyboard compile variants.

Expected: destination and bar-clear assertions fail against fixed 480x300 rendering.

- [ ] **Step 3: Implement shared viewport rendering**

Increase `PAL_DISPLAY_STRIP_WIDTH` to 800, route VG-Lite and CPU fallback through the computed viewport, and add a zero-filled strip flush helper for the outside bars.

Compile control drawing and dirty-control updates only for `BSP_SDLPAL_INPUT_TOUCH`. `pal_display_controls_set()` is a no-op in keyboard mode.

- [ ] **Step 4: Verify GREEN**

Run both display-port variants and all host tests.

- [ ] **Step 5: Commit**

```bash
git add projects/Edgi_Talk_M55_SDLPAL/platform/pal_display_port.* projects/Edgi_Talk_M55_SDLPAL/tests/host
git commit -m "feat: enlarge keyboard-mode game display"
```

### Task 6: Validate Both Modes Across All Rotations

**Files:**
- Modify: `tools/check_elf.py`
- Modify: `tools/build_matrix.ps1`
- Create: `tools/build_input_matrix.ps1`
- Modify: `tests/host/test_check_elf.py`
- Modify: `tests/host/test_project_contract.py`
- Modify: `README.md`

**Interfaces:**
- Produces: `check_elf.py --input-mode touch|keyboard`
- Produces: an eight-build mode/rotation matrix that restores `.config` and `rtconfig.h` even after failure

- [ ] **Step 1: Write failing checker tests**

Keyboard mode must require 28--64 KiB `.usb_host_data` and 2--4 KiB `.sdlpal_usb`. Touch mode must accept both as zero and reject nonzero Host sections.

- [ ] **Step 2: Verify RED**

Run `test_check_elf.py` and confirm the absent `--input-mode` behavior fails.

- [ ] **Step 3: Implement mode-aware ELF validation**

Add the CLI choice and print `input=<mode>` in PASS output. Preserve all framebuffer, GFX, ITCM, DTCM, stack and rotation checks.

- [ ] **Step 4: Add the input matrix wrapper**

For each mode, apply the exact Kconfig/`rtconfig.h` mode symbols, invoke the four-rotation matrix, and restore both files in `finally`. Keyboard builds include CherryUSB selections and value 12; touch builds remove them.

- [ ] **Step 5: Run all eight target builds**

Expected: 8 PASS lines, keyboard viewport memory within budget, touch images contain no Host static state, and source configs are restored to keyboard/90 degrees.

- [ ] **Step 6: Update README**

Document menuconfig symbols, mappings, mode-specific viewport dimensions, hot-plug behavior, RAM budgets and matrix commands.

- [ ] **Step 7: Commit**

```bash
git add projects/Edgi_Talk_M55_SDLPAL/tools projects/Edgi_Talk_M55_SDLPAL/tests projects/Edgi_Talk_M55_SDLPAL/README.md
git commit -m "test: validate SDLPal input modes"
```

### Task 7: Final Software and Board Verification

**Files:**
- Create: `docs/validation/usb-keyboard-game-input-board.txt`

**Interfaces:**
- Consumes: final keyboard/90-degree `rtthread.hex`
- Produces: reproducible software and board acceptance evidence

- [ ] **Step 1: Run all software verification**

Run host C/C++ tests, Python discovery, `git diff --check`, static stack analysis, the eight-build matrix, and the final keyboard/90-degree ELF check.

Expected: all commands pass; only documented pre-existing target warnings remain.

- [ ] **Step 2: Flash the final keyboard image**

Use the installed Infineon OpenOCD KitProg3 configuration and reset the board after programming.

- [ ] **Step 3: Capture board acceptance**

Verify arrows in menu/map, Enter/Escape, PageUp/PageDown, simultaneous keys, disconnect clear, reconnect recovery, `768x480` landscape viewport and absence of touch controls. Save observed serial excerpts and verdict in the validation file.

- [ ] **Step 4: Run touch regression image**

Flash one touch-mode image and verify the legacy control layout and highlights. Restore and rebuild the default keyboard/90-degree artifact afterward.

- [ ] **Step 5: Final commit**

```bash
git add projects/Edgi_Talk_M55_SDLPAL/docs/validation
git commit -m "test: record SDLPal keyboard board validation"
```
