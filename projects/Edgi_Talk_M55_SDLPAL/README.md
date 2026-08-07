# SDLPal for PSoC Edge M55

本工程将 SDLPal 直接适配到 Edgi-Talk PSoC Edge M55。显示、输入、SD
资源和存档路径已经接通，不依赖 LVGL。当前阶段的 audio is intentionally
disabled；屏幕显示、资源加载和触摸游玩完成实板验收后再实现音频。

## 功能范围

- 默认使用 USB Host 键盘和 800x480 横屏显示。
- 320x200 8-bit 游戏画面按 8:5 等比例最近邻放大；键盘模式不显示触摸按键。
- 可通过 Kconfig 改为触摸模式，显示方向、A、B、PgUp、PgDn 色块并支持组合键。
- 资源直接读取 `/sdcard/pal`，存档目录为 `/sdcard/pal/save`。
- 不编译 LVGL，不创建音频线程或音频设备。
- 可配置 0/90/180/270 度；90/270 度由 LCD 驱动使用 VG-Lite 旋转。

## SD 卡目录

使用 FAT32 格式的 TF 卡，将原版游戏资源放到以下目录。文件名大小写应与
列表一致：

```text
/sdcard/pal/
|-- abc.mkf
|-- ball.mkf
|-- data.mkf
|-- f.mkf
|-- fbp.mkf
|-- fire.mkf
|-- gop.mkf
|-- map.mkf
|-- mgo.mkf
|-- pat.mkf
|-- rgm.mkf
|-- rng.mkf
|-- sss.mkf
|-- word.dat
|-- m.msg
`-- save/
```

`save` 目录不存在时会自动创建。工程不包含受版权保护的游戏数据。

## 构建

在 BSP 根目录执行：

```powershell
rtk powershell -NoProfile -Command "`$env:RTT_EXEC_PATH='D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin'; & 'D:\workspace_work\env-windows\.venv\Scripts\scons.exe' -j16"
```

产物位于本目录的 `rt-thread.elf` 和 `rtthread.hex`。烧录时必须遵循开发板的
启动顺序：先确保 Secure M33 和 Non-secure M33 工程已正确烧录并开启 M55，
再烧录本 M55 固件。

当前输入模式的四方向构建与 ELF 约束检查：

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File projects\Edgi_Talk_M55_SDLPAL\tools\build_matrix.ps1 -InputMode keyboard
```

触摸/键盘两种输入模式和四个方向的完整 8 组矩阵：

```powershell
rtk powershell -NoProfile -ExecutionPolicy Bypass -File projects\Edgi_Talk_M55_SDLPAL\tools\build_input_matrix.ps1
```

结果保存在 `reports/<input>-rotation-*-size.txt`。检查器会拒绝 LVGL、错误的
framebuffer 尺寸、DTCM/GFX 越界、错误的 192 KiB 保存保留区、缺失或多余的
横屏 scanout buffer，以及与输入模式不符的 USB 专用段。单独检查 ELF 时必须
明确输入模式，例如 `--rotation 90 --input-mode keyboard`。

SDLPal 单函数静态栈帧由 GCC `.su` 报告检查，门限为 12 KiB：

```powershell
rtk D:\workspace_work\env-windows\.venv\Scripts\python.exe tools\check_stack_usage.py --root sdlpal --limit 12288
```

## 屏幕方向

默认配置为 `CONFIG_M55_BSP_LCD_ROTATION_90=y` 和
`CONFIG_BSP_LCD_ROTATION_DEGREES=90`。在 RT-Thread Settings 的
`Hardware Drivers Config -> Onboard Peripheral Drivers -> LCD logical rotation`
中可选择 0/90/180/270：

| 配置 | 逻辑分辨率 | 实现 |
| --- | --- | --- |
| 0 | 480x800 | 物理竖屏 |
| 90 | 800x480 | VG-Lite 旋转并使用独立 scanout buffer，默认 |
| 180 | 480x800 | 面板扫描方向翻转 |
| 270 | 800x480 | VG-Lite 旋转并使用独立 scanout buffer |

## 输入模式

在 RT-Thread Settings 的 `SDLPal input method` 中选择编译期输入后端，只能启用
一个：

- `BSP_SDLPAL_INPUT_USB_KEYBOARD`：默认。启用 CherryUSB DWC2 Host 和 HID，
  不编译触摸输入后端、不绘制触摸按键。
- `BSP_SDLPAL_INPUT_TOUCH`：使用屏幕触摸按键，不编译 CherryUSB Host 和键盘
  worker，其 `.sdlpal_usb` 与 `.usb_host_data` 链接段必须为空。

模式在编译期固定。键盘未连接或运行中断开时不自动回退到触摸，重新接入兼容
键盘后 Host 会重新枚举并恢复输入。

## 触摸操作

竖屏时方向键位于左下区域，A/B 位于右下区域，PgUp/PgDn 位于游戏画面下沿。
横屏时方向键和 A/B 分列屏幕左右，PgUp/PgDn 位于底部。按下的色块会改变
颜色。多点触摸可同时产生方向+A 等组合输入。

平台对物理触摸控制器设置约 10 ms 的最短采样间隔，并在间隔内返回缓存触点，
因此持续按压不会因为节流被误判为松开。控制区首次显示仍完整绘制背景；初始化
完成后，按键状态变化会在平台层立即刷新对应的变化按钮矩形，并且一次状态变化
只执行一次 display present，不等待下一帧游戏画面刷新整个下半屏。

- 方向：移动或菜单选择。
- A：确认、调查、对话。
- B：取消或返回。
- PgUp/PgDn：SDLPal 原生上一页/下一页输入。

## USB 键盘操作

当前固件在 M55 应用启动时初始化 CherryUSB DWC2 Host，并接收标准 USB
Boot Protocol 键盘的 8 字节输入报告。将键盘直接连接到开发板 USB Host
接口；Host 初始化成功以及键盘完成枚举后，串口应依次出现类似日志：

```text
[PAL USB] host ready: bus=0 base=0x...
[PAL USB] keyboard connected: vid=0x.... pid=0x.... ep=0x.. mps=...
```

方向键映射为 `PAL_CONTROL_UP/DOWN/LEFT/RIGHT`，Enter 映射为
`PAL_CONTROL_A`，Escape 映射为 `PAL_CONTROL_B`，PageUp/PageDown 映射为
`PAL_CONTROL_PGUP/PGDN`。按键按下和松开不输出逐键调试日志。不提供 Boot 键盘
接口、包长小于 8 字节或只提供厂商自定义 NKRO 接口的设备会打印 `HID ignored`，
不会占用当前键盘通道。

游戏操作对应关系为：方向键移动或选择，Enter/A 确认，Escape/B 取消，
PageUp/PageDown 翻页。支持方向键与动作键同时按下；按键状态由 USB worker
原子发布，SDLPal 输入桥统一生成游戏事件。断开键盘时立即释放所有游戏控制状态
并打印 `[PAL USB] keyboard disconnected`，
避免粘键。

仓库中的 DWC2 Host 驱动为按 CherryUSB 默认配置预编译的静态库，因此
`CONFIG_CONFIG_USBHOST_MAX_INTF_ALTSETTINGS` 必须保持为 `12`。修改这个值会
改变 Host 结构体布局，导致预编译驱动在设备接入中断中使用错误的成员偏移。
为保留 DTCM 主栈空间，Host 总线、Hub、HID 和 DWC2 静态状态统一放入
Secondary SRAM 的 `.usb_host_data` 段。

## 游戏显示区域

触摸模式保留原有按键区，游戏区域为竖屏 `(0, 0, 480, 300)`、横屏
`(160, 0, 480, 300)`。键盘模式删除屏幕触摸按键，将 320x200 游戏画面按
8:5 等比例放大并居中：竖屏为 `(0, 250, 480, 300)`，横屏为
`(16, 0, 768, 480)`；未覆盖区域使用黑色填充。VG-Lite 与 CPU fallback 使用
同一视口计算。

## 启动状态

| 代码 | 含义 | 处理 |
| --- | --- | --- |
| E00 | 正常启动阶段或运行中 | 无需处理 |
| E01 | `/sdcard` 尚未挂载 | 插入/检查 TF 卡，固件每秒重试 |
| E02 | 资源文件缺失 | 屏幕显示缺失文件名，补齐后复位 |
| E03 | LCD、保存目录或输入初始化失败 | 检查驱动和硬件后复位 |
| E04 | SDLPal 线程创建或启动失败 | 使用 `pal_mem` 检查片内堆 |
| E05 | SDLPal 主循环异常退出 | 查看串口 fatal 日志和内存快照 |

## RAM 策略

HyperRAM 不会自动绑定为主堆的后备区。高频数据只进入片内 SRAM：两个固定
indexed framebuffer 放在 M55 DTCM，SDL 临时 surface 从 Secondary SRAM 主堆
分配，24 KiB 游戏线程栈固定在 Secondary SRAM 专用链接段。`pal_hot_alloc()`
失败时直接失败，不回退到 HyperRAM。SDLPal 上游已有的 `PAL_LARGE` 标记由平台
强制包含的头文件映射到 `.cy_gpu_buf.sdlpal_large`，因此大对象静态存放在 GFX
SRAM，无需修改 upstream 源码，也不会占用线程栈或 HyperRAM。存档临时结构优先
从 Secondary SRAM 主堆分配；仅当约 180--192 KiB 的存档分配失败时，使用固定的
192 KiB GFX SRAM 保留区，仍不回退到 HyperRAM。

当前 GCC 键盘模式 rotation=90 链接结果：

| 区域 | 固定占用/容量 | 说明 |
| --- | ---: | --- |
| M55 DTCM | framebuffer 128 KiB | primary + backup，各 64 KiB |
| M55 DTCM | 19,536 B headroom | `.bss` 结束到 4 KiB MSP 主栈之间 |
| Secondary SRAM | 24,720 B 静态段 | 24 KiB 游戏线程栈、线程控制块及对齐 |
| Secondary SRAM | 15,440 B 音频静态段 | 8 KiB 音频线程栈、队列和解码状态 |
| Secondary SRAM | 2,512 B USB 静态段 | 允许范围 2--4 KiB；含 2 KiB 键盘 worker 栈、消息队列和控制块 |
| Secondary SRAM | 32,920 B USB Host 状态 | 允许范围 28--64 KiB；含 CherryUSB Host 总线、Hub、HID 和 DWC2 静态对象 |
| Secondary SRAM | 1,359,712 B 主堆窗口 | SDL surface 和其他高频动态对象 |
| GFX SRAM, 0/180 | 1,876,992 B / 3 MiB | LCD render、VG-Lite、15 KiB strip、64,000 B INDEX8 staging、593,408 B `PAL_LARGE` 和 192 KiB 保存保留区 |
| GFX SRAM, 90/270 | 2,999,296 B / 3 MiB | 另含 819,200 B scanout buffer |
| HyperRAM | 8 MiB 显式冷堆 | 不作为主堆 fallback；当前游戏路径的 `PAL_LARGE` 对象不使用该区域 |

当前 427 个 SDLPal 函数中最大静态栈帧为 `PAL_LoadDefaultGame()` 的 7,224 B，
低于 12 KiB 门限。24 KiB 线程栈为其调用链、中断现场和 C 库调用保留约 17 KiB；
静态报告不能替代实板 watermark，验收时要求 `used_peak < 18,432` 且长期稳定。

这些是静态链接结果；实际峰值必须在实板运行中通过 FinSH 命令查看：

```text
msh /> pal_mem
```

`pal_mem` 输出 DTCM 段、片内主堆 total/used/peak、HyperRAM
total/used/peak/largest、GFX 固定段、VG-Lite/CPU fallback 帧计数、游戏帧最大耗时、
`control updates/last_us/max_us` 即时按键刷新指标和 `sdlpal` 线程栈峰值。
引擎入口、标题完成、地图完成、战斗入口和 fatal 退出也会自动输出同一快照。

## 存档诊断

`.rpg` 存档以无缓冲模式打开，并拆成最大 512 B 的写请求；块间主动让出线程，
避免 SDIO 驱动长时间占用系统。每完成 32 KiB 会输出一次进度，正常日志顺序为：

```text
[PAL SAVE] alloc bytes=... source=SRAM
[PAL SAVE] open ok stream=... chunk=512
[PAL SAVE] write begin bytes=...
[PAL SAVE] write offset=0
[PAL SAVE] write offset=32768
...
[PAL SAVE] write end bytes=.../... elements=1/1
[PAL SAVE] close end result=0 errno=0
```

若主堆暂时无法提供完整存档结构，第一行会显示 `source=GFX-reserve`。发生卡顿时，
最后一条 `write offset` 可定位到具体 SDIO 写入阶段；出现 `short write`、非零
`close result` 或 `errno` 时，应同时检查 TF 卡和 FAT 文件系统状态。

## 实板验收

以下动态项目不能由交叉编译代替，合入或开始音频适配前必须逐项记录串口日志
和 `pal_mem` 输出：

- [ ] 不插卡启动显示红色 E01，插卡后自动继续。
- [ ] 临时移除 `map.mkf` 后显示红色 `E02 map.mkf`。
- [ ] 恢复资源后进入标题并开始新游戏。
- [ ] 调色板渐变、地图滚动、键盘方向、Enter/A、Escape/B、PageUp/PageDown 均正确。
- [ ] 键盘方向+动作组合输入正确，拔插后无粘键且可重新枚举。
- [ ] 触摸配置仍支持两指方向+A 组合输入，且不启动 USB Host。
- [ ] 保存、复位并读取存档成功。
- [ ] 完成场景切换和至少一场战斗，连续运行至少 30 分钟。
- [ ] 触摸响应低于 100 ms，按键 `control max_us` 低于 30 ms，游戏帧 display present 的 p95 低于 33 ms。
- [ ] 片内堆、HyperRAM 和线程栈峰值稳定，无单调增长；`cold current=0`，`sdlpal stack used_peak < 18432 total=24576`。

## DOS audio

DOS audio is implemented by the project-private `audio/` group. Place
`mus.mkf` and `voc.mkf` next to the other game resources under
`/sdcard/pal`. Either audio archive may be absent; the game still boots and
only the missing music or sound-effect path is disabled.

The port opens RT-Thread device `sound0` as 16 kHz, signed PCM16, mono input.
It renders 256 samples (512 bytes) per block. RIX music uses the fixed-memory
OPL2 core at 22.05 kHz and is converted to 16 kHz; DOS VOC effects use at most
four simultaneous voices. Mutable decoder, mixer and the 8 KiB audio thread
stack stay in the `.sdlpal_audio` Secondary SRAM section. Raw MKF chunks use a
bounded 1 MiB HyperRAM LRU cache.

For board acceptance, play title and battle music, trigger overlapping
movement/menu/battle effects, change volume, pause and resume music, then run
for at least 10 minutes. Also repeat save/load, battle entry, touch controls
and display scrolling to check that audio does not regress the existing
workflows.

SDLPal 上游版本和本地差异见 `sdlpal/UPSTREAM.md`。
