# SDLPal DOS Audio Integration Implementation Plan

> **For Codex:** Execute this plan task by task with test-driven development. Do not modify `sdlpal/upstream` or `vg_lite_hal.c`; keep every shared driver change under `BSP_USING_SDLPAL`.

**Goal:** Play DOS RIX music and up to four mixed DOS VOC effects through the existing PSoC Edge `sound0` device at 16 kHz, while keeping hot state in on-chip SRAM and raw audio resources in a bounded HyperRAM cache.

**Architecture:** A project-private `audio` build group provides the upstream `AUDIO_*` contract. Pure C modules implement the bounded cache, command queues, VOC parser/resampler, and mixer. A private C++ module adapts the locked fixed-memory RIX/MAME OPL2 code. OPL renders internally at 22.05 kHz and is converted to 16 kHz with fixed-point interpolation. An RT-Thread platform port owns the static audio thread and writes 256-sample PCM16 blocks to `sound0`.

**Toolchain:** RT-Thread/SCons, ARM GCC C/C++, host GCC/G++, Python unittest, existing RT-Thread Audio API.

---

## Task 1: Lock the project and shared-code contracts

**Files:**

- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Step 1: Add failing contract tests**

Require all of the following:

- project config enables `RT_USING_AUDIO`, `BSP_USING_AUDIO`, and `BSP_USING_AUDIO_PLAY`;
- SDLPal engine build no longer compiles `unix/contract_noaudio.c` and no longer defines `PAL_CONTRACT_NO_AUDIO`;
- the project-private `audio/SConscript` is discovered and defines `PAL_NO_RUNTIME_HEAP` plus `USE_RIX_EXTRA_INIT=0`;
- `drv_i2s.c` and `drv_i2s.h` keep SDLPal-specific FIFO/frame values under `BSP_USING_SDLPAL`;
- `sdlpal/upstream` remains at the locked provenance commit and contains no audio edits;
- `vg_lite_hal.c` remains free of SDLPal changes.

**Step 2: Run the new contract test and confirm failure**

Run: `rtk python -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract -v`

Expected: FAIL because audio is still disabled and `contract_noaudio.c` is still selected.

**Step 3: Add host-test targets to the Makefile**

Declare future `test_audio_cache`, `test_voc_mixer`, `test_audio_queue`, and `test_rix_output` targets without adding implementation shortcuts.

**Step 4: Commit the red tests**

Commit: `test: define SDLPal audio integration contract`

## Task 2: Implement the bounded HyperRAM resource cache

**Files:**

- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_cache.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_cache.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_audio_cache.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Step 1: Write failing cache tests**

Cover cache hit/miss, immutable handles, retain/release, zero-reference LRU eviction, refusal to evict active entries, duplicate-key replacement protection, loader failure, allocator failure, and the exact 1 MiB cap.

Use injected load/allocate/free callbacks so host tests do not depend on RT-Thread or files.

**Step 2: Run and confirm failure**

Run: `rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host test_audio_cache`

Expected: FAIL because the cache API does not exist.

**Step 3: Implement the minimum cache**

Use fixed metadata slots, 64-bit monotonically increasing LRU stamps, checked size arithmetic, immutable data pointers, and reference counts. Perform allocation/free only in the caller's context. Expose current/peak bytes, hits, misses, evictions, and failures.

**Step 4: Run the focused test**

Expected: PASS.

**Step 5: Commit**

Commit: `feat: add bounded SDLPal audio resource cache`

## Task 3: Implement DOS VOC parsing, resampling, and four-voice mixing

**Files:**

- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_voc.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_voc.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_mixer.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_mixer.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_voc_mixer.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Step 1: Write failing parser/mixer tests**

Build small in-memory VOC fixtures for type-1 sound data, type-2 continuation, type-3 silence, extended metadata, unknown blocks, unsupported codec, invalid time constants, block overflow, and truncation.

Verify unsigned 8-bit PCM conversion, fixed-point interpolation at representative DOS rates, exact end-of-voice behavior, one-to-four voices, oldest-voice replacement, independent music/SFX volume, and PCM16 saturation.

**Step 2: Run and confirm failure**

Run: `rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host test_voc_mixer`

Expected: FAIL because the parser and mixer do not exist.

**Step 3: Implement bounds-checked VOC views**

Keep VOC data immutable in the cache. Store only block cursor, source rate, source phase, and playback state per voice. Do not decode a complete PCM copy.

**Step 4: Implement fixed-point mixing**

Mix into 32-bit accumulators, apply Q15 gains, and saturate once. No floating point, allocation, or file access in render functions.

**Step 5: Run the focused test and commit**

Commit: `feat: add fixed-memory DOS VOC mixer`

## Task 4: Implement fixed command queues and audio runtime state

**Files:**

- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_queue.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_queue.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_runtime.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_runtime.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_audio_queue.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Step 1: Write failing queue tests**

Verify FIFO SFX commands, full-queue drops, complete desired-state music snapshots, music coalescing, generation wrap handling, stale-generation rejection, enable/disable, volume changes, and cache-handle release on rejected commands.

**Step 2: Run and confirm failure**

Run: `rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host test_audio_queue`

**Step 3: Implement fixed SPSC queues**

Use fixed arrays and single-producer/single-consumer indices. Platform critical-section hooks protect producer publication without introducing RT-Thread into host-testable code.

**Step 4: Run and commit**

Commit: `feat: add SDLPal audio command runtime`

## Task 5: Import and adapt the locked RIX/OPL2 implementation

**Files:**

- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/third_party/adplug/opltypes.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/third_party/adplug/opl.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/third_party/adplug/pal_rix_decoder.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/third_party/adplug/pal_rix_decoder.cpp`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/third_party/mame/mame.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/third_party/mame/fmopl.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/third_party/mame/fmopl.cpp.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/third_party/pal_mame_opl2_fixed_tables.inc`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_mame_opl2_static.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_mame_opl2_static.cpp`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_rix_output.cpp`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/UPSTREAM.md`

**Step 1: Add a failing RIX/OPL smoke test**

Verify malformed RIX rejection, a minimal valid track, reset determinism, non-zero PCM after register activity, `PalMameOpl2_TableBytes() == 25706`, and stable reported state size.

**Step 2: Run and confirm failure**

Run: `rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host test_rix_output`

**Step 3: Import exact locked sources**

Source: `D:\workspace_rb\OpenSouce\sdlpal-embedded`, `origin/extreme`, commit `23177627e619731188591288215dc2a61d884ae7`.

Retain all license headers. Adapt the RIX class to buffer-only loading and remove filesystem/runtime-heap dependencies. Keep the verified 22.05 kHz fixed OPL tables unchanged. Place mutable OPL state in the project audio SRAM section, not DTCM.

**Step 4: Run the smoke test and commit**

Commit: `feat: import fixed-memory RIX OPL2 backend`

## Task 6: Implement 22.05 kHz RIX to 16 kHz music rendering

**Files:**

- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_rix_music.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_rix_music.cpp`
- Extend: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_rix_output.cpp`

**Step 1: Add failing timing tests**

Verify exact 70 Hz sequencer advancement over one second, 22,050-to-16,000 fixed-point phase continuity across arbitrary output block boundaries, loop/non-loop completion, track switching, disable pause semantics, music volume, and symmetric fade out/in.

**Step 2: Run and confirm failure**

**Step 3: Implement the renderer**

Render complete 315-sample OPL ticks into a small on-chip source buffer. Resample continuously into caller-provided 16 kHz blocks with integer phase arithmetic. Preserve pending-switch and fade semantics without allocation.

**Step 4: Run and commit**

Commit: `feat: render RIX music at SDLPal output rate`

## Task 7: Add the RT-Thread `sound0` port and static SRAM section

**Files:**

- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/SConscript`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/board/linker_scripts/link.ld`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_check_elf.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`

**Step 1: Add failing linker/port contracts**

Require a `.sdlpal_audio (NOLOAD)` section in Secondary SRAM, exported boundaries, a 48 KiB upper assertion, an 8 KiB static stack, a 256-sample block, and static thread initialization with no `rt_thread_create`.

**Step 2: Run and confirm failure**

**Step 3: Implement the device port**

Find/open `sound0`, configure 16 kHz/16-bit mono input, initialize a static RT-Thread object and stack, render 256 PCM16 samples per iteration, recover short writes with silence, and stop with a bounded acknowledgement.

All audio-owned static mutable storage uses `.sdlpal_audio`; no audio state is placed in DTCM or GFX memory.

**Step 4: Run contract/ELF tests and commit**

Commit: `feat: add static RT-Thread SDLPal audio port`

## Task 8: Implement the upstream AUDIO contract and MKF integration

**Files:**

- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_contract.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_resources.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_resources.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/SConscript`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/SConscript`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_backend_stubs.c`

**Step 1: Extend failing contract tests**

Require exactly one `gAudioDevice`, every declared `AUDIO_*` function, no `contract_noaudio.c`, and no duplicate RIX/SOUND stubs.

**Step 2: Implement non-fatal resource/device lifecycle**

Open `mus.mkf` and `voc.mkf` without making them required boot resources. Use existing MKF helpers for checked chunk size/read. Allocate cache chunks with `pal_cold_alloc(..., PAL_MEMORY_TAG_RESOURCE)` and free them only from the game thread.

Implement play/stop/loop/fade, music and sound enable flags, configured volumes, four SFX voices, CD-unavailable behavior, and no-op lock functions compatible with the single-producer design.

**Step 3: Run focused and full host tests**

Run: `rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host all`

Run: `rtk python -m unittest discover -s projects/Edgi_Talk_M55_SDLPAL/tests/host -p "test_*.py" -v`

**Step 4: Commit**

Commit: `feat: connect SDLPal DOS audio contract`

## Task 9: Enable the project audio driver and reduce latency safely

**Files:**

- Modify: `projects/Edgi_Talk_M55_SDLPAL/Kconfig`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/.config`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/rtconfig.h`
- Modify: `libraries/HAL_Drivers/drv_i2s.h`
- Modify: `libraries/HAL_Drivers/drv_i2s.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`

**Step 1: Confirm the red project contract**

**Step 2: Enable project-local selections**

Make `BSP_USING_SDLPAL` select `BSP_USING_AUDIO` and `BSP_USING_AUDIO_PLAY`; regenerate or consistently update `.config` and `rtconfig.h`.

**Step 3: Add guarded driver constants**

For `BSP_USING_SDLPAL` at 16 kHz only:

- `PLAYBACK_DATA_FRAME_SIZE = 512` stereo `int16_t` elements;
- `TX_FIFO_SIZE = 1024` bytes, two 512-byte mono input blocks.

Keep the current 2048-element/4096-byte values in the `#else` path. Do not change device semantics or any non-SDLPal project path.

**Step 4: Run contract tests and compile the driver path**

**Step 5: Commit**

Commit: `feat: enable low-latency SDLPal I2S output`

## Task 10: Add diagnostics, documentation, and memory enforcement

**Files:**

- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_runtime.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_runtime.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_check_elf.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/README.md`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/UPSTREAM.md`

**Step 1: Add failing diagnostic/budget tests**

Require audio state, block counts, voice peak, render/write max time, underruns, queue drops, cache statistics, and audio stack high-water output. Require `.sdlpal_audio <= 48 KiB`, no audio in DTCM/GFX, and locked import provenance.

**Step 2: Implement telemetry and `pal_audio` shell report**

Do not print from the audio render thread. Snapshot counters from the shell/game thread.

**Step 3: Document resource requirements and board checks**

Add `mus.mkf`, `voc.mkf`, expected `sound0` configuration, `pal_audio`, memory targets, and the ten-minute validation procedure.

**Step 4: Run tests and commit**

Commit: `docs: document SDLPal audio operation and budgets`

## Task 11: Full verification

**Step 1: Run all host C/C++ tests**

Run: `rtk mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host clean all`

Expected: every executable passes with warnings treated as errors.

**Step 2: Run all Python checks**

Run: `rtk python -m unittest discover -s projects/Edgi_Talk_M55_SDLPAL/tests/host -p "test_*.py" -v`

Expected: PASS.

**Step 3: Build the target**

Run the project's configured SCons command with the known ARM GCC toolchain. Capture the final ELF/map size and `check_elf.py` result.

Expected: build succeeds, no duplicate audio symbols, `.sdlpal_audio <= 48 KiB`, and existing ITCM/GFX budgets remain valid.

**Step 4: Review source boundaries**

Run: `rtk git diff --name-only <design-baseline>..HEAD`

Expected: no path below `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream` and no `vg_lite_hal.c` change.

**Step 5: Board validation handoff**

Provide exact commands and expected telemetry for music, SFX, save/load, battle, touch/display responsiveness, and ten-minute soak testing. Hardware-only results must be reported as pending until observed on the board.
