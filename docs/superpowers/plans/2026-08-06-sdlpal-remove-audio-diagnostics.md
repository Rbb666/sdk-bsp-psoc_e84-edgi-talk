# SDLPal Audio Diagnostics Removal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the temporary `pal_audio` shell command and its diagnostic plumbing while preserving the validated SDLPal DOS audio playback path.

**Architecture:** Keep the SDLPal audio contract, static render buffers, RT-Audio replay queue, first-frame I2S priming, hardware-paced completion, and start/stop generation guards. Delete only the diagnostic aggregation, `pal_mem` audio report, project audio-port metrics, and SDLPal-specific shared-driver metric counters/API; the project audio SConscript already discovers remaining sources with `Glob('*.c')`.

**Tech Stack:** C/C++ embedded firmware, RT-Thread, Infineon PSoC Edge M55, SCons, Python `unittest`, host MinGW tests, ARM GCC, ELF contract checker.

## Global Constraints

- Do not modify `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/**`.
- Do not modify `libraries/components/mtb-device-support-pse8xxgp/pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/vg_lite_hal.c`.
- Keep shared-driver changes under `#if defined(BSP_USING_SDLPAL)`; do not change non-SDLPal I2S behavior.
- Preserve DOS RIX/VOC playback, static audio buffers, the two-block replay pool, first-frame priming, hardware-paced completion, stop-generation protection, and the corrected `rt_mq_recv()` message-length check.
- Do not remove cache/mixer/queue/RIX runtime behavior merely because their counters are no longer printed; remove only the interfaces and counters named in the approved design.
- Every verification command is run from the repository root and starts with `rtk`.

---

### Task 1: Add the diagnostics-absence contract (TDD red phase)

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py:469-540`

**Interfaces:**
- Consumes the existing project contract test fixture and source-reading helpers.
- Produces a test that fails while any removed file, symbol, shell export, or README command reference remains.

- [ ] **Step 1: Replace the presence-based test with an absence-based test**

Replace `test_audio_diagnostics_and_documentation_contract` with the following assertions. Keep the existing unrelated README, upstream, linker, and audio-lifecycle assertions in that test, but remove the old assertions that require diagnostic files and metric symbols:

```python
    def test_audio_diagnostics_are_removed(self):
        diagnostics_path = ROOT / "audio" / "pal_audio_diagnostics.c"
        diagnostics_header_path = ROOT / "audio" / "pal_audio_diagnostics.h"
        memory = (ROOT / "platform" / "pal_memory.c").read_text(
            encoding="utf-8"
        )
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        upstream = (ROOT / "sdlpal" / "UPSTREAM.md").read_text(
            encoding="utf-8"
        )
        elf_check = (ROOT / "tools" / "check_elf.py").read_text(
            encoding="utf-8"
        )
        contract = (ROOT / "audio" / "pal_audio_contract.c").read_text(
            encoding="utf-8"
        )
        audio_sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (ROOT / "audio").glob("*.c")
        )
        port_header = (ROOT / "audio" / "pal_audio_port.h").read_text(
            encoding="utf-8"
        )
        port_source = (ROOT / "audio" / "pal_audio_port.c").read_text(
            encoding="utf-8"
        )
        i2s_header = (
            BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_i2s.h"
        ).read_text(encoding="utf-8")
        i2s_source = (
            BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_i2s.c"
        ).read_text(encoding="utf-8")

        self.assertFalse(diagnostics_path.exists())
        self.assertFalse(diagnostics_header_path.exists())
        self.assertNotIn("pal_audio_diagnostics", memory)
        self.assertNotIn("pal_audio_diagnostics", contract)
        self.assertNotIn("pal_audio_port_metrics", port_header)
        self.assertNotIn("pal_audio_port_metrics_get", port_source)
        self.assertNotIn("drv_i2s_sdlpal_metrics", i2s_header)
        self.assertNotIn("drv_i2s_sdlpal_metrics_get", i2s_source)
        self.assertNotIn("sdlpal_i2s_metrics", i2s_source)
        self.assertNotIn("MSH_CMD_EXPORT(pal_audio", audio_sources)
        self.assertNotIn("pal_audio", readme)

        for text in ("mus.mkf", "voc.mkf", "sound0", "10"):
            self.assertIn(text, readme)
        self.assertIn("audio/third_party", upstream)
        self.assertIn("PAL_AUDIO_MAX_BYTES = 48 * 1024", elf_check)
        self.assertIn("PAL_AUDIO_MAX_LIVE_HANDLES", contract)
        self.assertIn(
            "pal_audio_release_capacity_covers_all_owners", contract
        )
```

The test must continue to assert the existing `drv_i2s.c` lifecycle invariants (`sdlpal_reset_playback_state`, queue/semaphore reset, null-buffer ordering, and the `BSP_USING_SDLPAL` guard) so the removal cannot hide an audio regression.

- [ ] **Step 2: Run the focused test and verify the expected red result**

Run:

```text
rtk powershell -Command "python -m unittest projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py -v"
```

Expected: the new test fails because both diagnostic files and their symbols are still present. Do not edit production code until this failure is observed.

- [ ] **Step 3: Commit the red-phase test**

```text
rtk powershell -Command "git add projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py"
rtk powershell -Command "git commit -m 'test: require SDLPal audio diagnostics removal'"
```

Expected: one commit containing only the contract-test change.

### Task 2: Remove the project shell and aggregate diagnostics

**Files:**
- Delete: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_diagnostics.c`
- Delete: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_diagnostics.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_contract.c:4-10,594-655`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.c:1-10,124-196`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/README.md:196-206`

**Interfaces:**
- Removes `pal_audio_diagnostics_get()` and the `pal_audio_diagnostics_t` type.
- Leaves `pal_audio_contract_init`, `pal_audio_contract_shutdown`, and all playback command functions unchanged.

- [ ] **Step 1: Delete the shell command and public diagnostics header**

Delete both files. The audio SConscript uses `Glob('*.c')`, so no separate source-list edit is required; verify that no build script names either file explicitly:

```text
rtk powershell -Command "rg -n 'pal_audio_diagnostics|pal_audio_diagnostics.c' projects/Edgi_Talk_M55_SDLPAL --glob '!tests/host/test_project_contract.py'"
```

Expected after the deletion: only the test's absence assertions may mention those names.

- [ ] **Step 2: Remove the aggregate getter and memory-report dependency**

In `pal_audio_contract.c`, remove the `#include "pal_audio_diagnostics.h"` line and delete the complete `pal_audio_diagnostics_get()` definition. Do not alter initialization, shutdown, music, sound, cache ownership, or queue code.

In `pal_memory.c`, remove the diagnostics header include, the local `pal_audio_diagnostics_t audio`, the `pal_audio_diagnostics_get(&audio)` call, and the `rt_kprintf` line beginning `"  audio blocks=`. Keep the SRAM/HyperRAM/GFX/display/SDLPal-stack output and the `pal_mem` command.

- [ ] **Step 3: Remove the README command reference**

Delete the paragraph that tells users to run `msh /> pal_audio` and describes its output. Preserve the audio asset names, `sound0`, stack placement, and the remaining resource/setup instructions.

- [ ] **Step 4: Run the focused contract test**

```text
rtk powershell -Command "python -m unittest projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py -v"
```

Expected: it still fails only on the remaining port/I2S metric symbols. This confirms Task 2 removed the shell and aggregate layers before touching the shared driver.

- [ ] **Step 5: Commit the project-layer removal**

```text
rtk powershell -Command "git add projects/Edgi_Talk_M55_SDLPAL/audio projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.c projects/Edgi_Talk_M55_SDLPAL/README.md"
rtk powershell -Command "git commit -m 'refactor: remove SDLPal audio shell diagnostics'"
```

### Task 3: Remove port and shared-I2S metric plumbing

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.h:11-35`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.c:10-230`
- Modify: `libraries/HAL_Drivers/drv_i2s.h:150-165`
- Modify: `libraries/HAL_Drivers/drv_i2s.c:47-119,532-570,800-810,1050-1140`

**Interfaces:**
- Retains `pal_audio_port_start`, `pal_audio_port_stop`, and `pal_audio_port_is_running`.
- Retains the SDLPal replay generation helpers and `sdlpal_request_next_frame()` call to `rt_audio_tx_complete()`.
- Removes `pal_audio_port_metrics_t`, `pal_audio_port_metrics_get`, `drv_i2s_sdlpal_metrics_t`, `drv_i2s_sdlpal_metrics_get`, `sdlpal_i2s_metrics`, and all updates to those counters.

- [ ] **Step 1: Remove project audio-port metrics without changing the render/write loop**

In `pal_audio_port.h`, delete the `pal_audio_port_metrics_t` typedef and its getter declaration. In `pal_audio_port.c`, remove the `drv_i2s.h` include, the `metrics` member from `pal_audio_port_state_t`, and `update_elapsed()`.

The audio thread must still render one `PAL_AUDIO_BLOCK_SAMPLES` block, write it, retry a short write with a zero-filled block, and close the device on stop. The loop should retain only the `written` variable and short-write recovery logic; remove timing and block-counter assignments. Delete `pal_audio_port_metrics_get()` and its stack high-water/driver snapshot code.

- [ ] **Step 2: Remove the shared I2S metrics type and getter**

In `drv_i2s.h`, delete only the `#if defined(BSP_USING_SDLPAL)` block that declares `drv_i2s_sdlpal_metrics_t` and `drv_i2s_sdlpal_metrics_get`. Leave all existing I2S API declarations and macros untouched.

- [ ] **Step 3: Remove I2S counter state and update sites, preserving playback sequencing**

In the SDLPal block at the top of `drv_i2s.c`, delete the `sdlpal_i2s_metrics` object and `drv_i2s_sdlpal_metrics_get()`. Keep:

```c
static volatile rt_uint32_t sdlpal_replay_generation;
static volatile bool sdlpal_replay_active;
```

Keep `sdlpal_request_next_frame()` as:

```c
static void sdlpal_request_next_frame(struct rt_audio_device *audio)
{
    rt_audio_tx_complete(audio);
}
```

Remove only metric reset/increment statements from `sound_start`, `sound_transmit`, the playback task, and the I2S ISR. Preserve the SDLPal `rt_mq_send()` call, the corrected `rt_ssize_t received` exact-size check, the first-frame prime, generation checks, independent FIFO trigger/underflow `if` blocks, and the existing non-SDLPal branches. The SDLPal transmit path remains equivalent to:

```c
#if defined(BSP_USING_SDLPAL)
    (void)rt_mq_send(snd_dev->tx_mq, &i2s_playback_q_data,
                     sizeof(i2s_playback_q_data_t));
#else
    rt_data_queue_push(&audio->replay->queue, tx_buff, tx_len, 0);
#endif
```

For the shared semaphore release path, retain one `rt_sem_release(snd_dev->tx_sem)` call; remove only the SDLPal success-counter branch. Retain the underflow log itself because it reports an actual playback fault rather than exposing a diagnostic API.

- [ ] **Step 4: Prove no removed symbols remain and the contract turns green**

```text
rtk powershell -Command "rg -n --hidden --glob '!**/tests/**' --glob '!upstream/**' 'pal_audio_diagnostics|pal_audio_port_metrics|drv_i2s_sdlpal_metrics|sdlpal_i2s_metrics|MSH_CMD_EXPORT\(pal_audio' projects/Edgi_Talk_M55_SDLPAL libraries/HAL_Drivers"
rtk powershell -Command "python -m unittest projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py -v"
```

Expected: the symbol search returns no production matches (the absence-test source may contain literal names), and the Python contract suite reports `OK`.

- [ ] **Step 5: Commit the metric-plumbing removal**

```text
rtk powershell -Command "git add projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.h projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.c libraries/HAL_Drivers/drv_i2s.h libraries/HAL_Drivers/drv_i2s.c"
rtk powershell -Command "git commit -m 'refactor: remove SDLPal audio metric plumbing'"
```

### Task 4: Run the complete verification matrix and protect boundaries

**Files:**
- Modify: no production files; update only test/build outputs generated by commands.

**Interfaces:**
- Consumes the cleaned project and the existing host/ARM verification scripts.
- Produces evidence that playback code still builds and the protected upstream/GFXSS files are unchanged.

- [ ] **Step 1: Run all host C/C++ tests**

```text
rtk powershell -Command "mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host clean all"
```

Expected: every host executable builds and exits successfully.

- [ ] **Step 2: Run the complete Python contract and ELF tests**

```text
rtk powershell -Command "python -m unittest discover -s projects/Edgi_Talk_M55_SDLPAL/tests/host -p 'test_*.py' -v"
```

Expected: all Python tests pass, including the diagnostics-absence test and the existing queue/handshake/stop-generation tests.

- [ ] **Step 3: Build the ARM target**

```text
rtk powershell -Command "scons -C projects/Edgi_Talk_M55_SDLPAL -j1"
```

Expected: SCons exits with code 0 and the SDLPal target links without unresolved diagnostics symbols.

- [ ] **Step 4: Run the rotation-90 ELF memory validation**

```text
rtk powershell -Command "python projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py --elf projects/Edgi_Talk_M55_SDLPAL/rt-thread.elf --map projects/Edgi_Talk_M55_SDLPAL/rtthread.map --nm D:/workspace_work/env-windows/tools/gnu_gcc/arm_gcc/mingw/bin/arm-none-eabi-nm.exe --rotation 90"
```

Expected: output contains `PASS rotation=90` and reports framebuffer/GFX/indexed/audio/ITCM/DTCM budgets within their existing limits.

- [ ] **Step 5: Verify protected files and repository hygiene**

```text
rtk powershell -Command "git diff --check"
rtk powershell -Command "git diff --quiet HEAD -- projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream libraries/components/mtb-device-support-pse8xxgp/pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/vg_lite_hal.c"
```

Expected: both commands exit 0. Review `git status --short` and ensure only the intended plan/test/source/docs commits are present; no generated build artifacts are staged.

- [ ] **Step 6: Commit the verification-only changes if any, then report evidence**

No commit is needed when the verification commands leave the worktree clean. The final report must include the three implementation commit IDs, host/Python/ARM/ELF results, and confirmation that `sdlpal/upstream` and `vg_lite_hal.c` were not modified.

## Self-Review Checklist

- Spec coverage: Tasks 1-3 remove every item in the approved design scope; Task 4 covers every verification requirement.
- Placeholder scan: the plan contains concrete paths, symbols, code snippets, commands, expected results, and no unresolved follow-up step.
- Type consistency: after Task 3, only `pal_audio_port_start/stop/is_running` remain public; no task calls a removed getter or type.
