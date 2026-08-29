# Retro-Go PSoC Edge 移植与验收

## 支持范围

本适配基于 retro-go `4ced1206` 中未经修改的 `gnuboy` 核心，支持 Game
Boy 和 Game Boy Color ROM：`.gb`、`.gbc`；同时接入 retro-go issue #349
同源的 HowBoyAdvance gpSP 快照，支持未压缩的 `.gba` ROM。

Issue #349 的 4～13 倍加速来自 ESP32-P4 专用 RV32 dynarec，它生成 RISC-V
机器码并依赖 ESP 可执行 PSRAM 映射，不能在 Thumb-only Cortex-M55 上执行。
gpSP 旧 `ARM_ARCH` 后端生成 A32，也不兼容 Armv8.1-M。因此本工程明确排除
所有 dynarec/emitter/stub，使用 `cpu.cc` 的 portable ARM7TDMI 解释器，并将
解释循环放入 ITCM。该方案可运行 GBA，但不能把 issue 中 ESP32-P4 的性能数字
直接套用到 M55；重型 3D 游戏是否全速必须以板端实测为准。

gnuboy 是用 C 实现的 Game Boy LR35902 解释器，不包含 RISC-V/Xtensa 专属
执行代码，也不需要切换所谓“ARM guest 模式”。Cortex-M55 适配重点是 host
C ABI：核心单独使用 `-O2 -fno-strict-aliasing -fno-strict-overflow -fwrapv`
编译，并使用 platform bounded frame runner 处理游戏在帧中关闭 LCDC、LY
固定为 0 的行为；独立工程使用 release/O2，platform 显示、USB 和音频代码
使用 O3。

硬件访问全部位于 `platform/`：

- 显示：GBA默认保持在模拟器main线程内，不创建显示worker。一次VG-Lite矩阵
  将240x160画面直接完成3倍缩放和90/270度旋转，写入两个物理scanout缓冲并在
  DC vblank非阻塞切换；若上一flip尚未完成，只跳过本次PPU绘制，CPU和音频仍
  继续运行。该路径去掉旧的720x480 staging、691 KiB AXIDMAC复制、第二次
  VG-Lite旋转和阻塞present；运行期契约不匹配时自动恢复原厂scanout并回退到
  `lcd_flush_rgb565_area()`稳定路径，原厂LCD驱动不作修改。
  GBA 原生 240x160 RGB565 使用整数 3 倍放大为 720x480，左右各 40 黑边；
  跳帧时在执行核心前设置 gpSP `skip_next_frame`，同时跳过软件扫描线渲染。
- 输入：CherryUSB 1.6.0 Host HID boot keyboard，不修改协议栈。
- 菜单：移植官方 `launcher/main/gui.c` 的 cold-boot carousel 和 browser 两态
  布局，使用官方 GB/GBC background、logo、banner 和 VeraBold11 字体。官方
  320x240 画布以 2 倍最近邻显示成 640x480，左右各 80 黑边；不依赖 LVGL。
  完整 `rg_system` 与 ESP 应用分区模型不适用于本工程，因此由 platform 层映射
  显示、USB 输入和 ROM catalog，选中游戏后直接返回 gnuboy ROM 路径。
- 性能监控：上游 `rg_system`/`rg_gui` 已提供 FPS、SPEED、frameskip、BUSY 和
  Debug 信息，但官方没有游戏内常驻侧栏。独立工程在 `platform/retro_go_perf.c`
  采用相同的一秒累计差分统计和 VeraBold11 文字风格实现右侧 80x320 面板，
  不引入 ESP/FreeRTOS GUI，也不启用会扰动时序的函数级 profiler。GBA 仍保持
  720x480 三倍整数缩放，只改为左对齐；面板位于 x=720～799。GB 画面本来就
  结束于 x=665，面板不会遮挡游戏；Launcher 阶段不显示该面板。
- 音频：默认由新增的 platform PDL 后端直接驱动 Infineon TDM FIFO，不经过
  RT-Audio、`sound0`、`drv_i2s.c`、ASRC 或音频 worker。模拟器提交的数据契约
  固定为 `16000 Hz / channels=1 / signed 16-bit mono`，8192-sample SPSC ring
  只存单声道样本；FIFO IRQ 每次取 32 个 mono frame，并把每个样本各写一次到
  L/R 两个 16-bit slot，因此软件输入仍严格为 1 通道，物理链路则必须保持
  2-channel × 16-bit dual-mono。不能把 TDM `channelNum` 改成 1。
- ES8388 工作于 slave single-speed。PDL 后端把 49.152 MHz 外设时钟设为 `/12`
  得到 4.096 MHz MCLK（256fs），TDM 再 `/8` 得到 512 kHz BCLK 和精确 16 kHz
  LRCK；原先 2.048 MHz/16 kHz 的 128fs 不在 ES8388 支持的比例中，可能造成
  尖锐、音高错误或失真。启动时在中断屏蔽状态下预填完整 128-word FIFO，之后
  每半 FIFO 补 64 word，并分别统计源欠载 `plc` 和硬件 FIFO 欠载 `i2s`。
- GBA mixer默认直接使用精确16000 Hz，不再先生成16384 Hz再逐样本重采样；
  32 kHz和24-tap FIR保留为有CPU余量时的A/B选项。PDL后端在生产者上下文使用
  512-sample grain、256-sample overlap和±96 sample波形搜索的轻量WSOLA，生成
  4096-sample物理输出ring并维持3584 samples（224 ms）目标水位。即使portable
  core低于实时速度，WSOLA也以时间拉伸代替线性降音调；TDM IRQ仍只做1:1取样
  和dual-mono复制。最低跟随速度降为25%，速率使用250 ms非对称EMA，真正欠载
  才使用10 ms淡出/淡入。
- 存储：SD 卡 FAT 文件系统；自动扫描 ROM，并保存 SRAM/即时存档。

GBA 的 ARM7TDMI 解释循环、整套扫描线渲染、事件调度、全部 memory/DMA 与
sound 热代码位于 ITCM；模拟 GBA IWRAM/VRAM 以及音频工作块位于 DTCM，
大型冷状态位于 Secondary SRAM。链接断言固定保留至少 32 KiB ITCM 与 8 KiB
DTCM 栈前余量。ROM bank 由 `0x64400000` 开始的 8 MiB HyperRAM heap 承载；
独立 platform 在启用 D-Cache 前把这一私有 heap 从原生成配置的 non-cacheable
改为 Normal WB/RA/WA，而 `0x64c00000` 后的共享 4 MiB 仍保持 non-cacheable。
该覆盖仅适用于本独立工程固定的 8 MiB M55 heap；若以后让 M33 或没有执行
cache clean/invalidate 的 DMA 直接访问此区，必须先关闭该 Kconfig 或补齐一致性
维护，编译期也会拒绝与 8 MiB 不一致的 heap 尺寸。

当前链接结果中 GFXRAM 使用2884.5/3072 KiB，剩余187.5 KiB；新增的128 KiB
用于把原staging空间扩展成第二个物理scanout。ITCM 剩余
36.3 KiB；DTCM 扣除 MSP 后只剩 9.4 KiB。M33 Template 的
`.cy_shared_socmem` 实际为 0，因此独立工程将双核 SOCMEM 共享窗口保守缩为
`0x261f0000..0x261fffff` 的 64 KiB，并把下方 192 KiB 返还给 M55。M55 片内
Secondary SRAM heap 因而从509.1 KiB增至约704.3 KiB。M33/M55 IPC semaphore
仍使用独立的 4 KiB allocatable-shared 区，不受影响。gpSP ROM cache 单块固定
为 1 MiB，新增的 192 KiB 不能直接替代任何一个 HyperRAM ROM block；当前
7 MiB ROM cache 仍留在 WB HyperRAM，避免为凑 1 MiB 而挤压 TCM 或安全余量。
为遵守不修改公共原厂 GeneratedSource 的约束，本布局由 M55 两份 project linker、
M33 Template linker 和 M55 platform MPU 覆盖共同定义，并由校验脚本检查一致性；
若以后业务开始使用生成的 `CYMEM_m33_m55_shared_*` 宏，应先在 `design.modus`
同步该边界并通过 Device Configurator 重新生成，而不能混用旧宏地址。

## Kconfig 配置

默认 `.config` 已启用模拟器。主要配置项如下：

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `BSP_USING_RETRO_GO` | `y` | 编译并启动模拟器 |
| `BSP_RETRO_GO_ROM_DIR` | `/sdcard/roms` | 自动扫描目录 |
| `BSP_RETRO_GO_ROM_PATH` | 空 | 指定菜单的初始高亮 ROM；为空时高亮第一项 |
| `BSP_RETRO_GO_SAVE_DIR` | `/sdcard/retro-go/saves` | SRAM 和即时存档目录 |
| `BSP_RETRO_GO_SD_MOUNT_TIMEOUT_MS` | `30000` | 单次 SD FAT 挂载等待周期，超时后继续重试 |
| `BSP_RETRO_GO_SCALE_FIT` | `y` | 等比放大到全屏高度 |
| `BSP_RETRO_GO_SCALE_INTEGER` | `n` | 可选 3 倍整数缩放 |
| `BSP_RETRO_GO_USE_VGLITE` | `y` | 使用VG-Lite；GBA优先融合direct-scanout，失败时回退稳定路径 |
| `BSP_RETRO_GO_MAX_FRAME_SKIP` | `2` | 落后时最多连续跳过的显示帧数；CPU/音频帧不会跳过 |
| `BSP_RETRO_GO_GBA_MAX_FRAME_SKIP` | `1` | GBA落后时最多连跳1帧，避免约100ms冻结 |
| `BSP_RETRO_GO_MAX_ROMS` | `64` | 游戏选择菜单最多保留和显示的 ROM 数量 |
| `BSP_RETRO_GO_CJK_MENU` | `y` | 使用独立16x16点阵显示UTF-8中文ROM名称 |
| `BSP_RETRO_GO_GB_BOOT_BIOS` | `y` | 文件存在时执行上游gnuboy真实GB/GBC Boot ROM启动动画 |
| `BSP_RETRO_GO_GB_BIOS_PATH` | `/sdcard/retro-go/bios/gb_bios.bin` | DMG Boot ROM路径，必须为256字节 |
| `BSP_RETRO_GO_GBC_BIOS_PATH` | `/sdcard/retro-go/bios/gbc_bios.bin` | CGB Boot ROM路径，必须为2304字节 |
| `BSP_RETRO_GO_GBA` | `y` | 启用 gpSP portable interpreter 的 GBA 支持 |
| `BSP_RETRO_GO_GBA_ROM_CACHE_MB` | `7` | HyperRAM中的GBA分页ROM cache，减少大ROM缺页读 |
| `BSP_RETRO_GO_GBA_AUDIO_MIX_KHZ` | `16` | gpSP内部精确16000 Hz实时混音配置 |
| `BSP_RETRO_GO_GBA_FIR_RESAMPLER` | `n` | 32kHz质量A/B时使用的24-tap低通 |
| `BSP_RETRO_GO_GBA_PERF_STATS` | `y` | 每 5 秒打印 FPS、跳帧、最长工作帧及音频生产速率 |
| `BSP_RETRO_GO_GBA_DEEP_PROFILE` | `y` | 用DWT拆分解释器、update、PPU、DMA、声音和32KiB ROM缺页 |
| `BSP_RETRO_GO_M55_BRANCH_PREDICTION` | `y` | 使能并回读M55 CCR.BP，降低解释器多级跳转分派开销 |
| `BSP_RETRO_GO_GBA_ARM_AL_FASTPATH` | `y` | 生成式M55 overlay；ARM cond=AL绕过16路条件分派，不修改上游快照 |
| `BSP_RETRO_GO_GBA_LAZY_CPSR_WRITEBACK` | `y` | guest未改NZCV时跳过指令边界CPSR冗余写回；显式同步点保留 |
| `BSP_RETRO_GO_GBA_CHEATS` | `n` | 当前无作弊入口，关闭后O3消除每guest指令的空hook检查 |
| `BSP_RETRO_GO_GBA_FAST_RAM_WRITE_INLINE` | `n` | EWRAM/IWRAM内联写A/B；当前因ITCM安全余量默认关闭 |
| `BSP_RETRO_GO_GBA_FAST_RAM_WRITE_WRAPPER` | `n` | 共享EWRAM/IWRAM短分派A/B；实测无FPS/解释器收益，发布配置关闭 |
| `BSP_RETRO_GO_ASYNC_GBA_DISPLAY` | `n` | 不创建显示线程；默认由main提交非阻塞双scanout |
| `BSP_RETRO_GO_GBA_DIRECT_SCANOUT` | `y` | main线程内融合3倍缩放+旋转并双缓冲非阻塞flip |
| `BSP_RETRO_GO_GBA_FLIP_WAIT_MS` | `3` | 满速时用剩余帧预算短等vblank，减少额外物理帧合并 |
| `BSP_RETRO_GO_GBA_DIRECT_RENDER` | `n` | 旧的logical framebuffer直写实验路径；默认由物理direct-scanout替代 |
| `BSP_RETRO_GO_GBA_DIRECT_CPU_SCALE` | `n` | CPU直写会使低优先级worker饥饿，默认关闭 |
| `BSP_RETRO_GO_PERF_OVERLAY` | `y` | 在游戏右侧显示 80x320 动态文字性能面板 |
| `BSP_RETRO_GO_PERF_UPDATE_MS` | `1000` | 面板统计和文字刷新周期；不创建额外 UI 线程 |
| `BSP_RETRO_GO_DWT_TIMING` | `y` | 使用M55 DWT CYCCNT统计core/work/blit/BUSY/MAX |
| `BSP_RETRO_GO_AUDIO` | `y` | 开启游戏音频 |
| `BSP_RETRO_GO_AUDIO_BACKEND_PDL` | `y` | 直接使用 Infineon PDL TDM FIFO；不链接 RT-Audio/`drv_i2s` |
| `BSP_RETRO_GO_AUDIO_BACKEND_RTTHREAD` | `n` | 兼容性回退后端；启用后才出现原厂 I2S/音频线程配置 |
| `BSP_RETRO_GO_AUDIO_PREROLL_SAMPLES` | `2048` | 16kHz mono源启动预缓冲 |
| `BSP_RETRO_GO_AUDIO_CONTINUITY_LINEAR` | `y` | 固定节拍输出并在真正欠载时平滑淡出/恢复 |
| `BSP_RETRO_GO_AUDIO_VARISPEED` | `y` | 仅在核心持续慢速时平滑跟随生产率，避免PLC爆音 |
| `BSP_RETRO_GO_AUDIO_WSOLA` | `y` | 使用轻量WSOLA在低速时保持基本音高 |
| `BSP_RETRO_GO_AUDIO_WSOLA_GRAIN_SAMPLES` | `512` | 32 ms分析grain，改善低频与混合音效连续性 |
| `BSP_RETRO_GO_AUDIO_WSOLA_SEARCH_SAMPLES` | `96` | 在名义位置±6 ms内匹配波形，不累计到长期时钟 |
| `BSP_RETRO_GO_AUDIO_OUTPUT_TARGET_SAMPLES` | `3584` | 已合成输出ring目标，约224 ms抗抖动余量 |
| `BSP_RETRO_GO_AUDIO_TARGET_SAMPLES` | `1536` | 自适应控制器的源ring目标水位 |
| `BSP_RETRO_GO_AUDIO_BUFFER_CORRECTION_PERCENT` | `12` | 源ring偏离目标时允许的WSOLA临时推进修正 |
| `BSP_RETRO_GO_AUDIO_MIN_SPEED_PERCENT` | `25` | WSOLA允许的最低源推进速度 |
| `BSP_RETRO_GO_AUDIO_RATE_WINDOW_MS` | `250` | 稳态音频生产率EMA周期 |
| `BSP_RETRO_GO_AUDIO_STARTUP_RATE_WINDOW_MS` | `50` | 首次快速采样周期，仅用于建立初始速率 |
| `BSP_RETRO_GO_AUDIO_CONCEAL_FADE_MS` | `10` | 真正欠载时的淡出/恢复淡入时间 |
| `CONFIG_USBHOST_PSC_PRIO` | `23` | CherryUSB Host Hub 线程优先级 |
| `BSP_RETRO_GO_INPUT_THREAD_PRIORITY` | `24` | USB HID 按键处理线程优先级 |
| `BSP_RETRO_GO_INPUT_RELEASE_TIMEOUT_MS` | `1000` | 游戏返回菜单时等待Esc释放并丢弃旧会话事件 |
| `RT_MAIN_THREAD_PRIORITY` | `25` | 欢迎页、菜单和模拟器核心线程优先级 |
| `BSP_RETRO_GO_AUTOSAVE_SECONDS` | `0` | 关闭同步周期写盘；Esc退出时仍保存SRAM |
| `BSP_RETRO_GO_CORE_ITCM` | `n` | 实验性 gnuboy 热代码 ITCM；稳定配置先使用 Flash + I-Cache |
| `BSP_RETRO_GO_HYPERRAM_WRITEBACK_CACHE` | `y` | 将 Retro-Go 私有 8 MiB HyperRAM heap 配置为 WB/RA/WA |
| `BSP_RETRO_GO_GBA_FRAMEBUFFER_CACHEABLE` | `n` | 当前GFXRAM不支持WB事务，开启会在display初始化时卡死 |

独立工程固定使用 `M55_BSP_LCD_ROTATION_90` 的 90 度横屏配置；显示适配层
本身仍保留 270 度支持，若以后扩展板级 Kconfig 可再开放选择。

RT-Thread 的线程优先级数值越大，实际调度优先级越低。本工程保持 MSH/tshell
为 20，所有游戏线程使用 21～25，因此串口 MSH 始终可以抢占菜单、模拟器、
USB 和显示任务。默认 PDL 音频没有 `sound_thread` 或 feeder 线程；TDM FIFO 使用
与原厂相同的 IRQ priority 2，每次仅补 32 个 mono frame，不参与 RT-Thread
线程调度。原厂 `drv_i2s.c/.h` 没有修改，也不会进入默认固件。

## ROM 资源准备

请使用自行持有卡带的合法备份，或作者明确允许再分发的 GB/GBC homebrew
ROM。可从 [itch.io 的 Game Boy ROM + Homebrew 分类](https://itch.io/games/tag-gameboy/tag-gameboy-rom/tag-homebrew)
选择明确提供 ROM 下载且许可符合用途的作品。不要从未经授权的商业 ROM
下载站获取资源。

1. 将 SD 卡格式化为 FAT32。
2. 在卡根目录创建 `roms`：主机侧对应 `X:\roms`，板端对应
   `/sdcard/roms`。
3. 放入至少一个未压缩的 `.gb`、`.gbc` 或 `.gba` 文件，例如
   `X:\roms\demo.gba`。当前适配不读取 ZIP。
4. 首次运行会自动创建 `/sdcard/retro-go/saves`；同名游戏生成
   GB/GBC 生成 `<game>.sav/.state`；GBA 使用独立的
   `/sdcard/retro-go/saves/gba/<game>.sav/.state`，避免同名跨平台冲突。
5. 可选的真实Game Boy启动动画沿用retro-go上游Boot ROM机制。在SD卡创建
   `X:\retro-go\bios`，放入自己有权使用的硬件转储：
   `gb_bios.bin`必须恰好256字节，`gbc_bios.bin`必须恰好2304字节。Nintendo
   Boot ROM属于专有内容，本工程和构建产物均不包含也不分发这些文件；文件
   缺失或尺寸错误时会记录原因并安全跳过动画，直接启动游戏。

## 编译、烧录与验收

在 PowerShell 中执行：

```powershell
cd D:\workspace_work\IFX\sdk-bsp-psoc_e84-edgi-talk\projects\Edgi_Talk_M55_RetroGo
$env:RTT_EXEC_PATH = "D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin"
D:\workspace_work\env-windows\.venv\Scripts\scons.exe --useconfig=.config
D:\workspace_work\env-windows\.venv\Scripts\scons.exe -j8
```

构建产物为 `rtthread.hex` 和 `rt-thread.elf`。按 Edgi-Talk 的启动顺序先
烧录能启动 M55 的 M33 固件，再通过板载 DAP 烧录本工程的 M55 固件。

上电前插入 SD 卡，将 USB Host 口连接标准 boot-protocol HID 键盘。串口
应看到 `USB host keyboard ready` 和 `/sdcard FAT filesystem is ready`；键盘
完成枚举后还会出现 `keyboard connected`。屏幕先显示官方 Game Boy 或 Game
Boy Color carousel：完整系统背景和居中的官方 logo/banner，不再显示自制
`RETRO-GO / PRESS ENTER` 页面。carousel 包含 GB、GBC 和 GBA；按 Enter 或
Z/J 进入官方风格 browser，选中 ROM 后再次按 Enter 或 Z/J 开始游戏。
发布配置启动时只清黑屏，不再显示调试彩条，也不再输出逐帧 signature、寄存器
或 watchdog 日志。

Launcher 键位：carousel 使用方向键/WASD、Q/E 或 Select 在 GB/GBC/GBA 间
切换，Enter 或 Z/J 进入列表；browser 使用上下逐项、左右整页、X/K 返回
carousel，Q/E 或 Select 切换系统，Enter 或 Z/J 启动；browser中的Esc返回
carousel，carousel中的Esc会被消费并继续停留在Launcher，不再结束应用。当当前系统
不足一页时，browser 左右键也会直接切换系统。底部和状态栏显示当前 GB/GBC/GBA，
串口输出 `ROM catalog: GB=n GBC=n GBA=n` 便于确认扫描结果。按键重复采用官方
400 ms 首延时及逐步加速。FatFs LFN API 配置为 UTF-8，ROM 的 UTF-8 原始路径
直接用于 `stat/open`；菜单按 Unicode 解码，并使用独立的 16x16 中文点阵显示
常用简体汉字。超出字库的字符才回退为 `?`。

游戏键位：

| 游戏功能 | 键盘按键 |
| --- | --- |
| 方向 | 方向键或 W/A/S/D |
| A | Z 或 J |
| B | X 或 K |
| L / R（GBA） | Q / E |
| Start | Enter |
| Select | 右 Shift 或 Backspace |
| 即时存档 | F5 |
| 读取即时存档 | F9 |
| 硬复位游戏 | R |
| 快进 | 按住 Space |
| 保存 SRAM 并返回 Launcher | Esc |

验收建议：进入游戏后依次验证方向和 A/B/Start/Select；确认有音频；按 F5
后按 F9 能恢复画面；产生游戏内存档后重启，确认 `.sav` 能恢复；拔插键盘
后确认所有按键状态被释放且重新连接可继续操作。持续运行 10 分钟，串口不应
出现 `PDL audio ring overrun`，性能日志中的 `i2s(+delta)` 暖机后应保持 0，
声音应连续、音高正常且游戏速度稳定。

GBA 验收建议优先使用合法备份的 2D 游戏，确认 `GBA interpreter running`、
画面为 720x480、Q/E 肩键、声音、SRAM、F5/F9 状态和 Esc 保存并返回菜单。再测试
16/32 MiB ROM，确认分页读盘不会崩溃。解释器模式下高负载 3D 游戏可能低于
全速，这是 M55 首版的已知性能边界，而不是启用了 issue #349 的 RV32 JIT。

返回菜单验收应看到 `GBA stopped`、`returning to launcher` 和新的
`ROM catalog`，随后停留在可交互carousel；不应再出现
`game selection cancelled` 或 `emulator exited`。测试GB/GBC Boot ROM时，成功
日志为 `GB boot BIOS ready` 或 `GBC boot BIOS ready`，随后应看到BIOS生成的
真实Logo动画和开机音效。

性能固件启动时还应看到：

```text
[retro-go] display 800x480 ... vglite-scaler=1, perf-panel=80
[retro-go] USB host keyboard ready, RT-Thread IRQ wrapper active, queue=16
[retro-go] clocks: SystemCore=... HF0=... HF1=399999999 Hz HF4=399999999 Hz
[retro-go] M55 cache I=1 D=1 BP=0->1 CCR=...
[retro-go] DWT timing: active=1 clock=399999999 Hz wrap=10737 ms
[retro-go] SOCMEM: M55=26060000-261effff heap=... (... KiB), M33/M55 shared=261f0000-261fffff (64 KiB)
[retro-go] MPU HyperRAM: 64400000-64bfffff WB/RA/WA, ... MAIR0=...ff
[retro-go] PDL I2S clock: divider=11+0/32 (/12), MCLK=4096000 Hz BCLK=512000 Hz LRCK=16000 Hz
[retro-go] direct GBA VG-Lite scanout active: ... no display worker
[retro-go] PDL audio ready: ... WSOLA=512/256 search=+-96 out-target=3584
[retro-go] GBA audio: native=16000 Hz output=16000 Hz, MVE-downmix=1 FIR=0
[retro-go] GBA M55 ARM cond=AL fast path active
[retro-go] GBA M55 dirty-guarded CPSR writeback active
[retro-go] GBA perf: ... blit=... drop=... ain=... str=... buf=... obuf=... plc=...(+delta) i2s=...(+delta) ...
[retro-go] GBA deep: int=... upd=... ppu=... dma=... snd=... mode=A.../T... page=... total=... max=... bytes=...K
```

正常速度下5秒统计应接近59～60 FPS，`AIN/AOUT`都应接近16000，`STR`接近
100%，`PLC(+delta)`、`I2S(+delta)`和`conceal`在暖机后不应继续增长。若核心持续低速，`STR`
会平滑下降到实际模拟速度附近，WSOLA用片段重叠保持音高；`OBUF`应长期保持在
约210～224 ms。若`PLC delta>0`且`OBUF`反复归零，才说明核心出现了超过输出
余量的长时间停顿；`i2s delta>0`则单独表示硬件FIFO服务异常。
`core=draw/skip` 分别给出绘制帧与真正跳过扫描线渲染时的核心平均耗时，`blit`
是main同步完成的LCD平均耗时；当前28～37 FPS低于52.3 Hz屏幕刷新率时，
`drop`应接近0；核心接近60 FPS时允许约12%的物理帧合并。
这些字段可直接区分解释器、PPU和显示瓶颈。
`mode=A/T`按每次核心事件边界统计ARM与Thumb执行状态占比，用于判断ARM条件分派
快路是否值得继续；它不是逐指令计数。EWRAM/IWRAM短写wrapper在同一Sega Rally
场景中实测FPS与`int`均无改善，因此发布配置保持关闭。
上述core/work/blit/BUSY/MAX均由DWT cycle累计并换算到0.1ms；FPS/DFPS的一秒
墙钟窗口、帧调度、自动存档和超时仍使用RT tick。DWT在约10.7秒回卷，单次
区间使用无符号差值，能够安全跨越回卷。
若出现 `VG-Lite scale failed` 会自动使用 CPU 3 倍缩放，性能会明显下降；验收
时请连同连续 3 组 `GBA perf` 日志一起记录。

右侧性能面板字段如下：

| 字段 | 含义 |
| --- | --- |
| `FPS` | 最近一秒模拟帧率 |
| `DFPS` | 最近一秒真正完成present的物理帧率；不要与模拟FPS混淆 |
| `DRW` / `SKP` | 最近一秒实际绘制帧数和跳过渲染帧数 |
| `BUSY` / `MAX` | 核心、音频和显示工作占用比例，以及最长工作帧耗时 |
| `AIN` / `AOUT` | 核心实际产生速率，以及 PDL FIFO 的连续输出速率 |
| `STR` / `BUF` | WSOLA分析推进比例，以及按该比例换算的源缓存毫秒数 |
| 串口 `OBUF` | 已由WSOLA合成、可由PDL直接播放的输出缓存毫秒数 |
| `PLC` | 真正源欠载并启用淡出隐藏的事件累计数；稳定运行时不应持续增长 |
| 串口 `i2s` | TDM FIFO/接口硬件欠载累计数；稳定运行时必须保持不增长 |

侧栏不再重复显示 `RETRO-GO`、`SPD`、`MEM`、`GPU`、`CACHE`、`HRAM` 和
`MVE` 等静态或派生信息；平台能力仍通过启动日志确认。周期刷新区域由
80x480 缩到 80x320，每次少搬运 25.6 KiB RGB565 数据。

GBA显示默认在main中执行融合VG-Lite direct-scanout，不创建`rg_disp`线程，
也不使用异步snapshot邮箱。DWT的`blit`只统计GPU融合blit与flip提交，不再等待
原厂整屏present；不再在执行核心前检查pending flip，避免固定draw/skip交替。
完成核心后若物理DC仍未释放back，只合并该显示帧且不触碰front buffer。

## 官方 Launcher 资源与许可证

界面资源来自 retro-go `4ced120669750ca7228fd0414211430c1d923166`，生成文件
记录了每个官方 PNG 和 VeraBold11 源文件的 SHA256，校验工具会阻止过期或来源
不一致的资源进入固件。GPL-2.0、原作者 CREDITS、Bitstream Vera 字体许可及
GBZ35 主题来源说明保存在 `applications/retro_go/upstream/launcher/`。

gpSP 快照来自 `Irak4t0n/HowBoyAdvance@d2ed7442`，其完整文件 SHA256 manifest
保存在 `upstream/gpsp/SNAPSHOT.sha256`；开放 GBA BIOS 由该快照生成并校验，
未包含 Nintendo 官方 BIOS。gpSP 与开放 BIOS 为 GPLv2 系许可证。

中文菜单点阵由仓库内 SDLPAL `fontglyph_cn.h` 离线生成，共 8836 个 Unicode
字形，固件不链接 SDLPAL。Red Flag Linux/YH 的两条款再分发声明完整保存在
`upstream/fonts/CJK_FONT_LICENSE.txt`，生成文件同时记录源文件 SHA256。

需要注意：GBZ35 主题美术采用 CC BY-NC-SA 3.0，包含非商业和相同方式共享
约束。若固件用于商业产品，应先取得相应授权或替换为拥有商业许可的主题资源。
