# SDLPal 音频诊断链路移除实施计划

> **供代理执行者：** 必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans`，逐项执行本计划。步骤使用复选框（`- [ ]`）跟踪状态。

**目标：** 移除临时的 `pal_audio` shell 命令及其诊断链路，同时保留已经验证通过的 SDLPal DOS 音频播放路径。

**架构：** 保留 SDLPal 音频契约、静态渲染缓冲区、RT-Audio 重放队列、I2S 首帧预填充、硬件节拍驱动的完成通知，以及启停代际保护。仅删除诊断聚合层、`pal_mem` 中的音频报告、项目音频端口指标，以及共享驱动中 SDLPal 专用的指标计数器和接口；项目音频 SConscript 已通过 `Glob('*.c')` 自动发现剩余源文件。

**技术栈：** C/C++ 嵌入式固件、RT-Thread、Infineon PSoC Edge M55、SCons、Python `unittest`、MinGW 宿主测试、ARM GCC、ELF 契约检查器。

## 全局约束

- 不得修改 `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/**`。
- 不得修改 `libraries/components/mtb-device-support-pse8xxgp/pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/vg_lite_hal.c`。
- 共享驱动的改动必须受 `#if defined(BSP_USING_SDLPAL)` 保护；不得改变非 SDLPal 的 I2S 行为。
- 保留 DOS RIX/VOC 播放、静态音频缓冲区、双块重放池、首帧预填充、硬件节拍驱动的完成通知、停止代际保护，以及已经修正的 `rt_mq_recv()` 消息长度判断。
- 不得因为相关计数器不再打印而删除缓存、混音器、队列或 RIX 的运行行为；仅删除已批准设计中明确列出的接口和计数器。
- 所有验证命令均从仓库根目录运行，并以 `rtk` 开头。

---

### 任务 1：添加“诊断链路不存在”契约测试（TDD 红灯阶段）

**文件：**
- 修改：`projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py:469-540`

**接口：**
- 使用现有项目契约测试夹具和源码读取辅助逻辑。
- 产出一个契约测试：只要待删除的文件、符号、shell 导出或 README 命令引用仍然存在，测试就会失败。

- [ ] **步骤 1：将存在性测试替换为不存在性测试**

用下列断言替换 `test_audio_diagnostics_and_documentation_contract`。保留该测试中与 README、上游源码、链接脚本及音频生命周期相关的既有断言，删除要求诊断文件和指标符号必须存在的旧断言：

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

测试必须继续检查 `drv_i2s.c` 中既有的生命周期约束，包括 `sdlpal_reset_playback_state`、队列和信号量复位、空缓冲区判断顺序以及 `BSP_USING_SDLPAL` 宏保护，防止移除诊断代码时掩盖音频回归。

- [ ] **步骤 2：运行聚焦测试并确认出现预期红灯**

运行：

```text
rtk powershell -Command "python -m unittest projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py -v"
```

预期：新测试失败，因为两个诊断文件及其符号仍然存在。观察到该失败之前不得修改生产代码。

- [ ] **步骤 3：提交红灯阶段测试**

```text
rtk powershell -Command "git add projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py"
rtk powershell -Command "git commit -m 'test: require SDLPal audio diagnostics removal'"
```

预期：生成一个只包含契约测试变更的提交。

### 任务 2：移除项目层 shell 命令和聚合诊断

**文件：**
- 删除：`projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_diagnostics.c`
- 删除：`projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_diagnostics.h`
- 修改：`projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_contract.c:4-10,594-655`
- 修改：`projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.c:1-10,124-196`
- 修改：`projects/Edgi_Talk_M55_SDLPAL/README.md:196-206`

**接口：**
- 移除 `pal_audio_diagnostics_get()` 和 `pal_audio_diagnostics_t` 类型。
- 保持 `pal_audio_contract_init`、`pal_audio_contract_shutdown` 和所有播放命令函数不变。

- [ ] **步骤 1：删除 shell 命令和公开诊断头文件**

删除上述两个文件。音频 SConscript 使用 `Glob('*.c')`，因此无需单独修改源文件列表；确认没有构建脚本显式引用这两个文件：

```text
rtk powershell -Command "rg -n 'pal_audio_diagnostics|pal_audio_diagnostics.c' projects/Edgi_Talk_M55_SDLPAL --glob '!tests/host/test_project_contract.py'"
```

预期：删除完成后，只有测试中的不存在性断言可能提及这些名称。

- [ ] **步骤 2：移除聚合读取函数和内存报告依赖**

在 `pal_audio_contract.c` 中，移除 `#include "pal_audio_diagnostics.h"`，并完整删除 `pal_audio_diagnostics_get()` 定义。不得改动初始化、关闭、音乐、音效、缓存所有权或队列代码。

在 `pal_memory.c` 中，移除诊断头文件引用、局部变量 `pal_audio_diagnostics_t audio`、`pal_audio_diagnostics_get(&audio)` 调用，以及以 `"  audio blocks=` 开头的 `rt_kprintf`。保留 SRAM、HyperRAM、GFX、显示和 SDLPal 线程栈输出，以及 `pal_mem` 命令。

- [ ] **步骤 3：移除 README 中的命令说明**

删除提示用户运行 `msh /> pal_audio` 并解释其输出的段落。保留音频资源文件名、`sound0`、栈空间布局和其他资源配置说明。

- [ ] **步骤 4：运行聚焦契约测试**

```text
rtk powershell -Command "python -m unittest projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py -v"
```

预期：测试仍然失败，但仅剩音频端口和 I2S 指标符号相关断言失败。这证明任务 2 已在修改共享驱动前完成 shell 和聚合层清理。

- [ ] **步骤 5：提交项目层移除改动**

```text
rtk powershell -Command "git add projects/Edgi_Talk_M55_SDLPAL/audio projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory.c projects/Edgi_Talk_M55_SDLPAL/README.md"
rtk powershell -Command "git commit -m 'refactor: remove SDLPal audio shell diagnostics'"
```

### 任务 3：移除音频端口和共享 I2S 指标链路

**文件：**
- 修改：`projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.h:11-35`
- 修改：`projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.c:10-230`
- 修改：`libraries/HAL_Drivers/drv_i2s.h:150-165`
- 修改：`libraries/HAL_Drivers/drv_i2s.c:47-119,532-570,800-810,1050-1140`

**接口：**
- 保留 `pal_audio_port_start`、`pal_audio_port_stop` 和 `pal_audio_port_is_running`。
- 保留 SDLPal 重放代际辅助函数，以及 `sdlpal_request_next_frame()` 对 `rt_audio_tx_complete()` 的调用。
- 移除 `pal_audio_port_metrics_t`、`pal_audio_port_metrics_get`、`drv_i2s_sdlpal_metrics_t`、`drv_i2s_sdlpal_metrics_get`、`sdlpal_i2s_metrics` 及这些计数器的全部更新代码。

- [ ] **步骤 1：移除项目音频端口指标，不改变渲染和写入循环**

在 `pal_audio_port.h` 中删除 `pal_audio_port_metrics_t` 类型定义及其读取函数声明。在 `pal_audio_port.c` 中移除 `drv_i2s.h` 引用、`pal_audio_port_state_t` 的 `metrics` 成员以及 `update_elapsed()`。

音频线程仍必须渲染一个包含 `PAL_AUDIO_BLOCK_SAMPLES` 个采样的块并将其写出；短写时使用清零后的块重试；停止时关闭设备。循环只保留 `written` 变量和短写恢复逻辑，移除耗时及块计数赋值。完整删除 `pal_audio_port_metrics_get()` 及其线程栈水位和驱动快照代码。

- [ ] **步骤 2：移除共享 I2S 指标类型和读取接口**

在 `drv_i2s.h` 中，仅删除声明 `drv_i2s_sdlpal_metrics_t` 和 `drv_i2s_sdlpal_metrics_get` 的 `#if defined(BSP_USING_SDLPAL)` 代码块。保持其他 I2S API 声明和宏不变。

- [ ] **步骤 3：移除 I2S 计数状态和更新点，保留播放时序**

在 `drv_i2s.c` 顶部的 SDLPal 代码块中，删除 `sdlpal_i2s_metrics` 对象和 `drv_i2s_sdlpal_metrics_get()`。保留：

```c
static volatile rt_uint32_t sdlpal_replay_generation;
static volatile bool sdlpal_replay_active;
```

将 `sdlpal_request_next_frame()` 保持为：

```c
static void sdlpal_request_next_frame(struct rt_audio_device *audio)
{
    rt_audio_tx_complete(audio);
}
```

仅移除 `sound_start`、`sound_transmit`、播放任务和 I2S 中断中的指标复位及递增语句。保留 SDLPal 的 `rt_mq_send()` 调用、已修正的 `rt_ssize_t received` 精确长度检查、首帧预填充、代际检查、相互独立的 FIFO 触发和下溢 `if` 分支，以及既有非 SDLPal 分支。SDLPal 发送路径保持与下列代码等价：

```c
#if defined(BSP_USING_SDLPAL)
    (void)rt_mq_send(snd_dev->tx_mq, &i2s_playback_q_data,
                     sizeof(i2s_playback_q_data_t));
#else
    rt_data_queue_push(&audio->replay->queue, tx_buff, tx_len, 0);
#endif
```

共享信号量释放路径必须保留一次 `rt_sem_release(snd_dev->tx_sem)` 调用，只删除 SDLPal 成功计数分支。保留下溢日志，因为它报告真实播放故障，不属于对外暴露的诊断 API。

- [ ] **步骤 4：确认所有待删除符号已消失，契约测试转为绿灯**

```text
rtk powershell -Command "rg -n --hidden --glob '!**/tests/**' --glob '!upstream/**' 'pal_audio_diagnostics|pal_audio_port_metrics|drv_i2s_sdlpal_metrics|sdlpal_i2s_metrics|MSH_CMD_EXPORT\(pal_audio' projects/Edgi_Talk_M55_SDLPAL libraries/HAL_Drivers"
rtk powershell -Command "python -m unittest projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py -v"
```

预期：符号搜索在生产代码中没有匹配项，不存在性测试源码中可以保留对应字符串；Python 契约测试输出 `OK`。

- [ ] **步骤 5：提交指标链路移除改动**

```text
rtk powershell -Command "git add projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.h projects/Edgi_Talk_M55_SDLPAL/audio/pal_audio_port.c libraries/HAL_Drivers/drv_i2s.h libraries/HAL_Drivers/drv_i2s.c"
rtk powershell -Command "git commit -m 'refactor: remove SDLPal audio metric plumbing'"
```

### 任务 4：执行完整验证矩阵并检查受保护边界

**文件：**
- 不修改生产文件；命令仅更新测试和构建产物。

**接口：**
- 使用清理后的项目以及现有宿主和 ARM 验证脚本。
- 产出证据，证明播放代码仍可构建，且受保护的上游和 GFXSS 文件未被修改。

- [ ] **步骤 1：运行全部宿主 C/C++ 测试**

```text
rtk powershell -Command "mingw32-make -C projects/Edgi_Talk_M55_SDLPAL/tests/host clean all"
```

预期：所有宿主测试程序均成功构建并正常退出。

- [ ] **步骤 2：运行完整 Python 契约及 ELF 单元测试**

```text
rtk powershell -Command "python -m unittest discover -s projects/Edgi_Talk_M55_SDLPAL/tests/host -p 'test_*.py' -v"
```

预期：所有 Python 测试通过，包括诊断链路不存在性测试，以及既有队列、握手和停止代际测试。

- [ ] **步骤 3：构建 ARM 目标**

```text
rtk powershell -Command "scons -C projects/Edgi_Talk_M55_SDLPAL -j1"
```

预期：SCons 以退出码 0 完成，SDLPal 目标链接成功，不存在未解析的诊断符号。

- [ ] **步骤 4：执行旋转角度 90 度的 ELF 内存验证**

```text
rtk powershell -Command "python projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py --elf projects/Edgi_Talk_M55_SDLPAL/rt-thread.elf --map projects/Edgi_Talk_M55_SDLPAL/rtthread.map --nm D:/workspace_work/env-windows/tools/gnu_gcc/arm_gcc/mingw/bin/arm-none-eabi-nm.exe --rotation 90"
```

预期：输出包含 `PASS rotation=90`，并报告帧缓冲区、GFX、索引缓冲区、音频、ITCM 和 DTCM 均处于既有预算范围内。

- [ ] **步骤 5：检查受保护文件和仓库状态**

```text
rtk powershell -Command "git diff --check"
rtk powershell -Command "git diff --quiet HEAD -- projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream libraries/components/mtb-device-support-pse8xxgp/pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/vg_lite_hal.c"
```

预期：两个命令均以退出码 0 完成。检查 `git status --short`，确认仅存在计划、测试、源码和文档的预期提交，没有生成的构建产物进入暂存区。

- [ ] **步骤 6：仅在验证产生必要改动时提交，并报告验证证据**

如果验证命令执行后工作区保持干净，则无需创建额外提交。最终报告必须包含三个实施提交的 ID、宿主测试、Python 测试、ARM 构建和 ELF 检查结果，并确认 `sdlpal/upstream` 与 `vg_lite_hal.c` 未被修改。

## 自检清单

- 设计覆盖：任务 1 至任务 3 覆盖已批准设计中的全部删除项；任务 4 覆盖全部验证要求。
- 占位内容检查：计划包含明确路径、符号、代码片段、命令和预期结果，不存在未明确的后续步骤。
- 类型一致性：任务 3 完成后，对外仅保留 `pal_audio_port_start/stop/is_running`；后续任务不会调用已删除的读取函数或类型。
