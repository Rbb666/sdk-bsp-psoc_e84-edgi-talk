# SDLPal I2S Replay Handshake Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the SDLPal audio deadlock after three writes and make the first PCM frame start the I2S ISR state machine deterministically.

**Architecture:** Keep RT-Thread audio core and the two-block 512-byte replay pool unchanged. Under `BSP_USING_SDLPAL` only, prime TDM from the first prepared PCM buffer and let hardware consumption drive unconditional `rt_audio_tx_complete()` requests; retain the legacy polling behavior for every other project. Add SDLPal-only counters from the shared driver through the project-owned diagnostics path.

**Tech Stack:** C99, RT-Thread audio/data queue/message queue APIs, Infineon AudioTDM PDL, Python `unittest` contract tests, SCons/GNU Arm Embedded.

## Global Constraints

- Every shell command starts with `rtk`.
- Use `apply_patch` for manual edits.
- Do not modify `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream`.
- Do not modify `libraries/components/mtb-device-support-pse8xxgp/pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/vg_lite_hal.c`.
- Do not modify RT-Thread audio core sources.
- Shared I2S behavior changes must remain inside `BSP_USING_SDLPAL`.
- Keep `RT_AUDIO_REPLAY_MP_BLOCK_SIZE=512` and `RT_AUDIO_REPLAY_MP_BLOCK_COUNT=2`.

---

### Task 1: Make The I2S Consumer Hardware-Paced

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`
- Modify: `libraries/HAL_Drivers/drv_i2s.c`

**Interfaces:**
- Consumes: `rt_audio_tx_complete(struct rt_audio_device *)`, `i2s_write_32_samples(void)`, `active_i2s_playback_buffer_ptr`.
- Produces: SDLPal-only helpers `sdlpal_prime_first_frame(void)` and `sdlpal_request_next_frame(struct rt_audio_device *)`.

- [ ] **Step 1: Write the failing handshake contract test**

Add this test to `ProjectContractTest`:

```python
def test_sdlpal_i2s_replay_is_hardware_paced(self):
    source = (
        BSP_ROOT / "libraries" / "HAL_Drivers" / "drv_i2s.c"
    ).read_text(encoding="utf-8")

    prime = re.search(
        r"static void sdlpal_prime_first_frame\(void\)\s*"
        r"\{(?P<body>[\s\S]*?)\n\}",
        source,
    )
    self.assertIsNotNone(prime)
    self.assertIn(
        "i2s_playback_ptr = active_i2s_playback_buffer_ptr;",
        prime.group("body"),
    )
    self.assertIn("i2s_write_32_samples();", prime.group("body"))

    request = re.search(
        r"static void sdlpal_request_next_frame\("
        r"struct rt_audio_device \*audio\)\s*"
        r"\{(?P<body>[\s\S]*?)\n\}",
        source,
    )
    self.assertIsNotNone(request)
    self.assertIn("rt_audio_tx_complete(audio);", request.group("body"))
    self.assertIn("sdlpal_request_next_frame(audio);", source)
    self.assertRegex(
        source,
        r"#if defined\(BSP_USING_SDLPAL\)\s*"
        r"sdlpal_request_next_frame\(audio\);\s*#else\s*"
        r"while \(audio->replay->queue.is_empty == 1\)",
    )
```

- [ ] **Step 2: Run the contract test and verify RED**

Run:

```powershell
rtk python -m unittest discover -s projects\Edgi_Talk_M55_SDLPAL\tests\host -p test_project_contract.py -v
```

Expected: FAIL because `sdlpal_prime_first_frame` and `sdlpal_request_next_frame` do not exist.

- [ ] **Step 3: Add the SDLPal-only first-frame and completion helpers**

In `drv_i2s.c`, immediately before `i2s_playback_task`, add:

```c
#if defined(BSP_USING_SDLPAL)
static void sdlpal_prime_first_frame(void)
{
    i2s_playback_ptr = active_i2s_playback_buffer_ptr;
    i2s_data_ready_flag = false;
    app_i2s_enable();
    i2s_write_32_samples();
    app_i2s_activate();
}

static void sdlpal_request_next_frame(struct rt_audio_device *audio)
{
    rt_audio_tx_complete(audio);
}
#endif
```

In the `if (first_frame)` block, keep the legacy manual-zero startup under `#else` and call `sdlpal_prime_first_frame()` under `BSP_USING_SDLPAL`:

```c
#if defined(BSP_USING_SDLPAL)
            sdlpal_prime_first_frame();
#else
            app_i2s_enable();
            for (int i = 0; i < HW_FIFO_SIZE; i++)
            {
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t)0);
                i++;
                Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t)0);
            }
            app_i2s_activate();
#endif
```

At the end of `i2s_playback_task`, request the next frame unconditionally only for SDLPal:

```c
#if defined(BSP_USING_SDLPAL)
        sdlpal_request_next_frame(audio);
#else
        while (audio->replay->queue.is_empty == 1)
        {
            rt_thread_mdelay(1);
#if defined(PKG_USING_WAVPLAYER) && !defined(BSP_USING_XiaoZhi)
            if (count >= 50)
            {
                rt_completion_done(&audio->replay->cmp);
                count = 0;
            }
            count++;
#endif
        }
        rt_audio_tx_complete(audio);
#endif
```

- [ ] **Step 4: Run the focused test and verify GREEN**

Run the Step 2 command.

Expected: all `test_project_contract.py` tests PASS.

- [ ] **Step 5: Build the ARM object path before committing**

Run from `projects/Edgi_Talk_M55_SDLPAL`:

```powershell
rtk cmd.exe /d /c "set ""RTT_EXEC_PATH=D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin""&& ""D:\workspace_work\env-windows\.venv\Scripts\scons.exe"" -j8"
```

Expected: `drv_i2s.o` compiles and `rt-thread.elf` links successfully.

- [ ] **Step 6: Commit the handshake fix**

```powershell
rtk git add projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py libraries/HAL_Drivers/drv_i2s.c
rtk git commit -m "fix: keep SDLPal I2S replay pipeline moving"
```

---

### Task 2: Expose Driver Pipeline Diagnostics

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`
- Modify: `libraries/HAL_Drivers/drv_i2s.h`
- Modify: `libraries/HAL_Drivers/drv_i2s.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_diagnostics.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_contract.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_diagnostics.c`

**Interfaces:**
- Produces: `drv_i2s_sdlpal_metrics_t` and `drv_i2s_sdlpal_metrics_get(drv_i2s_sdlpal_metrics_t *)`.
- Extends: `pal_audio_port_metrics_t` and `pal_audio_diagnostics_t` with `driver_tx_messages`, `driver_rx_messages`, `driver_fifo_irqs`, `driver_sem_releases`, `driver_completion_requests`, and `driver_mq_send_failures`.

- [ ] **Step 1: Write the failing diagnostics contract test**

Extend `test_audio_diagnostics_and_documentation_contract`:

```python
for metric in (
    "driver_tx_messages",
    "driver_rx_messages",
    "driver_fifo_irqs",
    "driver_sem_releases",
    "driver_completion_requests",
    "driver_mq_send_failures",
):
    self.assertIn(metric, header)
self.assertIn("drv_i2s_sdlpal_metrics_get", i2s_source)
self.assertIn("driver tx=", diagnostics)
```

- [ ] **Step 2: Run the contract test and verify RED**

Run:

```powershell
rtk python -m unittest discover -s projects\Edgi_Talk_M55_SDLPAL\tests\host -p test_project_contract.py -v
```

Expected: FAIL because the new metrics are absent.

- [ ] **Step 3: Define and collect SDLPal driver metrics**

Under `BSP_USING_SDLPAL` in `drv_i2s.h`, define:

```c
typedef struct drv_i2s_sdlpal_metrics
{
    uint32_t tx_messages;
    uint32_t rx_messages;
    uint32_t fifo_irqs;
    uint32_t sem_releases;
    uint32_t completion_requests;
    uint32_t mq_send_failures;
    uint32_t underruns;
} drv_i2s_sdlpal_metrics_t;

void drv_i2s_sdlpal_metrics_get(drv_i2s_sdlpal_metrics_t *metrics);
```

Store a volatile metrics object in `drv_i2s.c`. Snapshot it with interrupts disabled. Increment counters at these boundaries:

```c
static volatile drv_i2s_sdlpal_metrics_t sdlpal_i2s_metrics;

void drv_i2s_sdlpal_metrics_get(drv_i2s_sdlpal_metrics_t *metrics)
{
    rt_base_t level;

    if (metrics == RT_NULL)
    {
        return;
    }
    level = rt_hw_interrupt_disable();
    metrics->tx_messages = sdlpal_i2s_metrics.tx_messages;
    metrics->rx_messages = sdlpal_i2s_metrics.rx_messages;
    metrics->fifo_irqs = sdlpal_i2s_metrics.fifo_irqs;
    metrics->sem_releases = sdlpal_i2s_metrics.sem_releases;
    metrics->completion_requests =
        sdlpal_i2s_metrics.completion_requests;
    metrics->mq_send_failures = sdlpal_i2s_metrics.mq_send_failures;
    metrics->underruns = sdlpal_i2s_metrics.underruns;
    rt_hw_interrupt_enable(level);
}
```

Use these exact update points:

```c
result = rt_mq_send(snd_dev->tx_mq, &i2s_playback_q_data,
                    sizeof(i2s_playback_q_data_t));
if (result == RT_EOK)
{
    ++sdlpal_i2s_metrics.tx_messages;
}
else
{
    ++sdlpal_i2s_metrics.mq_send_failures;
}

++sdlpal_i2s_metrics.rx_messages;             /* after successful recv */
++sdlpal_i2s_metrics.completion_requests;     /* before tx_complete */
++sdlpal_i2s_metrics.fifo_irqs;               /* FIFO trigger ISR */
++sdlpal_i2s_metrics.sem_releases;            /* before sem release */
++sdlpal_i2s_metrics.underruns;                /* underflow ISR */
```

- successful/failed `rt_mq_send()` in `sound_transmit`;
- successful `rt_mq_recv()` in `i2s_playback_task`;
- `sdlpal_request_next_frame()`;
- TX FIFO trigger ISR entry;
- successful `rt_sem_release()`;
- TX underflow ISR entry.

Reset all counters in `sound_start()` before requesting the first frame.

```c
rt_memset((void *)&sdlpal_i2s_metrics, 0,
          sizeof(sdlpal_i2s_metrics));
```

- [ ] **Step 4: Propagate and print the metrics**

Copy the driver metrics into `pal_audio_port_metrics_t`, then into `pal_audio_diagnostics_t`. Add this line to `pal_audio` output:

Add these fields, with identical names, to both project-owned metric structs:

```c
uint32_t driver_tx_messages;
uint32_t driver_rx_messages;
uint32_t driver_fifo_irqs;
uint32_t driver_sem_releases;
uint32_t driver_completion_requests;
uint32_t driver_mq_send_failures;
```

In `pal_audio_port_metrics_get`, fetch and map the driver snapshot:

```c
drv_i2s_sdlpal_metrics_t driver;

drv_i2s_sdlpal_metrics_get(&driver);
metrics->driver_tx_messages = driver.tx_messages;
metrics->driver_rx_messages = driver.rx_messages;
metrics->driver_fifo_irqs = driver.fifo_irqs;
metrics->driver_sem_releases = driver.sem_releases;
metrics->driver_completion_requests = driver.completion_requests;
metrics->driver_mq_send_failures = driver.mq_send_failures;
metrics->hardware_underruns = driver.underruns;
```

In `pal_audio_diagnostics_get`, copy the six new `port` fields into the
matching `diagnostics` fields. Add this line to `pal_audio` output:

```c
rt_kprintf("  driver tx=%u rx=%u complete=%u irq=%u sem=%u mq_fail=%u\n",
           diagnostics.driver_tx_messages,
           diagnostics.driver_rx_messages,
           diagnostics.driver_completion_requests,
           diagnostics.driver_fifo_irqs,
           diagnostics.driver_sem_releases,
           diagnostics.driver_mq_send_failures);
```

- [ ] **Step 5: Run the focused contract test and verify GREEN**

Run:

```powershell
rtk python -m unittest discover -s projects\Edgi_Talk_M55_SDLPAL\tests\host -p test_project_contract.py -v
```

Expected: all project contract tests PASS.

- [ ] **Step 6: Run complete host verification**

```powershell
rtk mingw32-make -C projects\Edgi_Talk_M55_SDLPAL\tests\host clean all
rtk python -m unittest discover -s projects\Edgi_Talk_M55_SDLPAL\tests\host -p test_*.py -v
```

Expected: all C/C++ host tests and all Python tests PASS.

- [ ] **Step 7: Commit diagnostics**

```powershell
rtk git add libraries/HAL_Drivers/drv_i2s.h libraries/HAL_Drivers/drv_i2s.c projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.h projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.c projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_diagnostics.h projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_contract.c projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_diagnostics.c projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py
rtk git commit -m "feat: diagnose SDLPal I2S replay progress"
```

---

### Task 3: Verify Firmware And Project Boundaries

**Files:**
- Verify only: `projects/Edgi_Talk_M55_SDLPAL/rt-thread.elf`
- Verify only: `projects/Edgi_Talk_M55_SDLPAL/rtthread.map`

**Interfaces:**
- Consumes: completed Task 1 and Task 2 implementation.
- Produces: target build evidence and board retest instructions.

- [ ] **Step 1: Build the final ARM firmware**

Run from `projects/Edgi_Talk_M55_SDLPAL`:

```powershell
rtk cmd.exe /d /c "set ""RTT_EXEC_PATH=D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin""&& ""D:\workspace_work\env-windows\.venv\Scripts\scons.exe"" -j8"
```

Expected: SCons completes and prints final `text`, `data`, and `bss` sizes.

- [ ] **Step 2: Validate ELF memory placement**

Run from `projects/Edgi_Talk_M55_SDLPAL`:

```powershell
rtk python tools\check_elf.py --elf rt-thread.elf --map rtthread.map --nm D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin\arm-none-eabi-nm.exe --rotation 90
```

Expected: `PASS`, audio SRAM remains at or below 48 KiB, and DTCM headroom remains positive.

- [ ] **Step 3: Verify protected boundaries**

```powershell
rtk git diff --quiet 315a071 -- projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream
rtk git diff --quiet 315a071 -- libraries/components/mtb-device-support-pse8xxgp/pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/vg_lite_hal.c
rtk git diff --check 315a071..HEAD
```

Expected: all commands exit zero with no output.

- [ ] **Step 4: Prepare the board acceptance check**

After flashing `rtthread.hex`, enter the game, wait five seconds, and run:

```text
pal_audio
list mempool
list msgqueue
```

Expected:

- `rendered` and `written` continue increasing between repeated `pal_audio` calls;
- `driver tx`, `driver rx`, `complete`, `irq`, and `sem` increase;
- `mq_fail=0`, `release_overflows=0`, and underruns remain zero or do not grow;
- `rix ticks` is nonzero while music is active;
- `adu_mp` is not permanently `free=0` with `pal_audi` suspended;
- DOS music and VOC effects are audible.

- [ ] **Step 5: Confirm clean worktree**

```powershell
rtk git status --short
```

Expected: no output.
