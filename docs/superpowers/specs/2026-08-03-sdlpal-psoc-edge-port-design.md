# SDLPal PSoC Edge 适配设计

日期：2026-08-03

## 1. 背景

目标是在英飞凌 PSoC Edge M55 平台上运行 SDLPal，并通过板载 `480x800` LCD 和 ST7102 触摸屏完成游戏操作。

实现以 `projects/Edgi_Talk_M55_LVGL` 的板级配置为基础，参考：

- 上游 SDLPal：<https://github.com/sdlpal/sdlpal>
- ESP32 参考实现：`D:\workspace_rb\OpenSouce\sdlpal-embedded` 的 `origin/extreme` 分支

最终目标工程不链接 LVGL。现有 LCD、GFXSS/VG-Lite、ST7102、SDIO/DFS、RT-Thread 和 HyperRAM 驱动继续复用。

## 2. 目标与非目标

### 2.1 第一阶段目标

1. 从 SD 卡 `/sdcard/pal` 加载原始 PAL DOS/Win95 资源文件。
2. 在物理竖屏方向显示正确的游戏画面。
3. 使用屏幕上的 RGB565 色块虚拟按键完成移动、菜单、确认和取消操作。
4. 支持存档读取和写入。
5. 输出 DTCM、片内 Secondary SRAM、GFX SRAM、HyperRAM 和线程栈的峰值占用。
6. 提供 `0/90/180/270` 度屏幕旋转配置。

### 2.2 非目标

第一阶段不实现音频，不初始化音频设备，也不创建音频线程。只有屏幕显示、游戏资源加载和触摸操作通过验收后，才开始音频适配。

第一阶段不移植完整 SDL2，不使用 ESP32 `pal_full.pak` 资源打包方案，也不建立占用数 MiB 的资源缓存。

## 3. 已选架构

采用“SDLPal 主体 + 最小 SDL 兼容层”。保留 SDLPal 游戏逻辑，只实现目标代码实际使用的 SDL 子集。

没有采用以下方案：

- 直接修改 `gpScreen`、`g_InputState` 等 SDLPal 内部状态：内存略少，但与游戏实现耦合过深。
- 移植完整 SDL2：兼容性更高，但代码量、堆内存和线程开销不适合本目标。

新建 `projects/Edgi_Talk_M55_SDLPAL`。它从 `Edgi_Talk_M55_LVGL` 继承板级配置，但其 Kconfig、SConscript 和源码依赖中不包含 LVGL。仓库共享 LVGL 组件不物理删除，以免影响其他示例工程。

建议目录结构：

```text
projects/Edgi_Talk_M55_SDLPAL/
  applications/
  sdlpal/                  SDLPal 游戏主体
  platform/
    sdl_compat/            最小 SDL API
    video/                 索引画面、缩放、色块按键、旋转
    input/                 ST7102 多点触摸和按键状态
    fs/                    DFS 路径及资源检查
    memory/                内存池和峰值统计
    audio/                 第一阶段无音频桩
```

## 4. 运行模型

启动顺序如下：

```text
板级初始化
  -> 等待 /sdcard 挂载
  -> 检查必需资源
  -> 创建存档目录
  -> 初始化内存池、LCD、触摸
  -> 启动 SDLPal 游戏线程
  -> 输入采样 -> 游戏逻辑 -> 画面转换 -> LCD 提交
```

第一阶段只使用一个游戏线程驱动 SDLPal 逻辑和显示提交，避免多个线程并发访问 8 位索引帧缓冲。LCD DMA、GFXSS 或 VG-Lite 操作完成后通过现有信号量同步；等待超时后丢弃当前帧并记录错误，游戏线程不能永久阻塞。

最小 SDL 兼容层覆盖以下能力：

- 毫秒时钟和延时
- 键盘状态和按键事件
- 8 位索引 Surface、调色板和矩形操作
- SDLPal 实际使用的互斥、信号量或线程包装
- 基于 RT-Thread libc/DFS 的文件访问
- 无音频设备桩

## 5. 显示设计

### 5.1 默认竖屏

物理屏幕为 `480x800`，默认 `BSP_LCD_ROTATION_DEGREES=0`。

SDLPal 的逻辑画面为 `320x200`。使用最近邻算法精确放大 1.5 倍，得到 `480x300` 游戏区域。剩余 `480x500` 区域用于触摸控制。

```text
480 x 800
┌──────────────────────────┐
│ SDLPal 320x200           │
│ 最近邻缩放到 480x300     │
├──────────────────────────┤
│                          │
│   方向色块      PgUp/PgDn│
│                    A / B │
│                          │
└──────────────────────────┘
```

显示路径为：

```text
320x200 8 位索引 Surface
  -> 256 项 RGB565 调色板查表
  -> 小型多行转换缓冲完成 1.5 倍缩放
  -> lcd_flush_rgb565_area(..., present=false)
  -> 绘制控制区色块
  -> 提交 LCD 帧
```

应用层不再分配一个额外的 `480x800` RGB565 全屏缓冲。转换缓冲建议为 16 行，约占 15 KiB。LCD 驱动拥有最终渲染缓冲并负责 cache clean/invalidate 和 present 同步。

控制区色块在调色板转换之后绘制，因此不受游戏调色板、淡入淡出和颜色循环影响。每个色块使用固定高对比度颜色、清晰边框和按下亮度反馈；实际命中区域略大于可见色块。

### 5.2 旋转

旋转只使用现有 `BSP_LCD_ROTATION_DEGREES` 作为单一配置源：

| 配置 | 逻辑方向 | 后端 |
|---|---|---|
| `0` | `480x800` 竖屏 | 直接扫描输出 |
| `180` | `480x800` 竖屏 | GFXSS 图层旋转 |
| `90` | `800x480` 横屏 | VG-Lite 旋转到物理扫描缓冲 |
| `270` | `800x480` 横屏 | VG-Lite 旋转到物理扫描缓冲 |

`0` 度默认构建不承担 VG-Lite 旋转开销。`90/270` 度模式使用独立逻辑渲染缓冲和物理扫描缓冲；横屏控制色块放置在画面边缘并允许覆盖游戏区域。

## 6. 触摸与按键

当前 `ST7102_get_single_touch()` 只返回第一个触点，但 ST7102 驱动内部和 RT-Thread touch device 接口最多支持 5 个触点。SDLPal 输入层直接批量读取 RT-Thread touch device，不依赖 LVGL 输入端口。

默认色块映射：

| 色块 | SDLPal 按键 | 用途 |
|---|---|---|
| 上/下/左/右 | `kKeyUp/Down/Left/Right` | 移动和菜单选择 |
| A | `kKeySearch` | 确认、交谈、调查 |
| B | `kKeyMenu` | 取消、游戏菜单 |
| PgUp/PgDn | `kKeyPgUp/PgDn` | 角色或页面切换 |

其他战斗快捷键第一阶段不单独显示，玩家通过游戏内菜单访问相同功能。

每个触点独立进行按下、保持、滑动和释放状态跟踪。SDLPal 原有按键重复策略继续生效。批量触点使方向键和 A/B 可以同时保持；开发板验收必须验证双指操作。

触摸原始坐标先按旋转配置转换为逻辑屏幕坐标，再进入色块命中判断。保留 `swap XY`、`invert X` 和 `invert Y` 板级校准选项，最终参数通过开发板实测确定。

## 7. 文件系统与资源

默认路径：

- 游戏资源：`/sdcard/pal`
- 存档：`/sdcard/pal/save`

启动时自动创建存档目录，并检查以下必需资源：

```text
abc.mkf  ball.mkf  data.mkf  f.mkf    fbp.mkf
fire.mkf gop.mkf   map.mkf   mgo.mkf  pat.mkf
rgm.mkf  rng.mkf   sss.mkf   word.dat m.msg
```

`desc.dat` 根据游戏版本作为可选文件。禁用音频时，不要求音乐和音效资源。

资源保持 SDLPal 原始文件形式，使用 `fopen/fread/fseek` 经 RT-Thread libc 和 DFS 访问。大文件按需读取和解压，不复制成常驻全文件缓存。资源版本继续使用 SDLPal 原有 DOS/Win95 检测逻辑。

## 8. RAM 设计

### 8.1 当前基线

未移植 SDLPal 的 `Edgi_Talk_M55_LVGL` 基线构建数据：

| 区域 | 容量 | 基线情况 |
|---|---:|---|
| DTCM | 256 KiB | `.data + .bss + .noinit` 约 47 KiB，至主栈限制约剩 205.6 KiB |
| Secondary SRAM | 1.375 MiB | 当前作为 RT-Thread 系统堆 |
| GFX SRAM | 3 MiB | 当前占用约 2.43 MiB |
| HyperRAM | 8 MiB 映射 | 当前可作为系统堆后备 |

当前 GFX SRAM 中两个 LVGL `480x800x2` 缓冲共占 1,536,000 字节。移除 LVGL 后释放约 1.5 MiB。

### 8.2 目标分区

| 区域 | 目标用途 |
|---|---|
| DTCM | 两个对齐到 64 KiB 的 `320x200` 索引帧槽，共 128 KiB；其余用于高频小状态和主栈 |
| Secondary SRAM | RT-Thread/DFS 堆、SDLPal 热数据、活动场景或战斗共享工作区、文件 I/O 临时区 |
| GFX SRAM | LCD 渲染缓冲、小型行转换缓冲，以及旋转模式按需启用的扫描/VG-Lite 工作区 |
| HyperRAM | 仅用于明确标记的低频大型资源或冷暂存数据 |

Secondary SRAM 目标预算：

- RT-Thread、DFS 和通用分配至少保留 320 KiB。
- SDLPal 持久热数据目标不超过 256 KiB。
- 场景和战斗共享一个最大约 512 KiB 的工作区，不同时保留两套最大缓冲。
- 文件读取、解压临时区目标不超过 128 KiB。
- 剩余约 192 KiB 作为增长和碎片余量。

默认 `0` 度模式的 GFX SRAM 目标占用约为：

- LCD RGB565 缓冲：819,200 字节
- 16 行转换缓冲：约 15 KiB
- 不分配 LVGL 缓冲
- 不分配 VG-Lite 旋转扫描缓冲

`90/270` 度模式预计额外使用：

- `800x480x2` 逻辑渲染缓冲：768,000 字节
- 物理 `512x800x2` 扫描缓冲：819,200 字节
- VG-Lite 命令及细分工作区：约 264 KiB

### 8.3 HyperRAM 策略

HyperRAM 不再作为普通分配器的透明后备。提供显式的冷数据分配接口；通用 `malloc`、SDL Surface 和每帧显示路径只能使用片内存储。

优先把事件表、当前场景热数据和当前战斗热数据留在片内 SRAM。只有分析确认不参与逐帧访问的大型精灵资源、非活动资源或短期大块暂存才进入 HyperRAM。

加入 `pal_mem` FinSH 命令和启动/退出场景日志，报告：

- 各内存池总量、当前使用、峰值和最大连续空闲块
- DTCM 与 GFX SRAM 固定段占用
- HyperRAM 分配标签及峰值
- 游戏线程栈高水位

链接阶段对 DTCM、Secondary SRAM 和 GFX SRAM 增加越界检查。最终是否需要把更多资源移动到 HyperRAM，以开发板峰值数据为依据，而不是依赖自动回退。

## 9. 配置项

目标工程至少提供以下配置：

```text
BSP_LCD_ROTATION_DEGREES = 0 | 90 | 180 | 270
PAL_GAME_PATH            = /sdcard/pal
PAL_SAVE_PATH            = /sdcard/pal/save
PAL_ENABLE_AUDIO         = n
PAL_MEM_DIAGNOSTICS      = y
PAL_TOUCH_SWAP_XY
PAL_TOUCH_INVERT_X
PAL_TOUCH_INVERT_Y
```

旋转配置同时驱动 LCD 布局、旋转后端和触摸坐标转换，不允许各模块分别保存角度。

## 10. 错误处理

在不使用 LVGL 的情况下，使用内置小型 ASCII 点阵字体和 RGB565 状态色块显示启动及运行错误。串口日志保留完整诊断信息。

| 错误 | 行为 |
|---|---|
| SD 卡未挂载 | 红色状态页并周期性重试 |
| 缺少资源 | 显示错误编号和第一个缺失文件名，不启动游戏 |
| 内存分配失败 | 输出内存池、标签和请求大小，进入错误页 |
| LCD 提交超时 | 记录计数并丢弃当前帧，避免永久阻塞 |
| 存档写入失败 | 保持游戏运行，短暂提示并记录日志 |
| 运行期关键资源读取失败 | 停止游戏逻辑并进入错误页，防止继续使用无效数据 |

## 11. 测试策略

### 11.1 主机测试

- 8 位调色板到 RGB565 的转换
- 320x200 到 480x300 的 1.5 倍最近邻缩放
- `0/90/180/270` 度触摸坐标转换
- 色块边界、重叠优先级、按下/滑动/释放
- 多触点到 SDLPal 按键位的合并
- 内部/HyperRAM 分配标签和峰值统计
- 缺失文件及短读错误处理

### 11.2 构建检查

- 构建四种旋转配置
- 确认 ELF 和 map 中没有 LVGL 符号及 LVGL 全屏缓冲
- 确认音频源和音频线程没有进入第一阶段镜像
- 检查 DTCM、Secondary SRAM、GFX SRAM 的链接占用
- 检查默认竖屏没有 VG-Lite 旋转扫描缓冲

### 11.3 开发板测试

- 无 SD 卡时显示错误并能在插卡后重试
- 缺少单个资源时显示正确文件名
- 正常进入标题画面和新游戏
- 调色板切换、淡入淡出、地图滚动显示正确
- 单指方向、A/B、PgUp/PgDn 操作正确
- 双指保持方向并按 A/B
- 存档后重新启动并读档
- 连续切换场景和进入战斗至少 30 分钟
- 收集显示耗时、触摸延迟、各内存池峰值和线程栈水位

## 12. 第一阶段验收标准

1. 从 `/sdcard/pal` 原始文件启动并进入可操作游戏。
2. 默认物理竖屏下颜色、比例和刷新正确，无明显撕裂。
3. 色块虚拟按键可以完成移动、菜单、确认和取消。
4. 触摸到 SDLPal 按键状态的响应时间低于约 100 ms。
5. 竖屏显示转换和提交的 95% 帧耗时低于 33 ms。
6. 连续运行、切换场景并进入战斗至少 30 分钟，无持续内存增长或分配失败。
7. 存档写入和重新读取成功。
8. 提供片内 SRAM、GFX SRAM、HyperRAM 和线程栈峰值报告。
9. 目标 ELF 不含 LVGL，第一阶段不含音频实现。

## 13. 实施顺序和阶段门

1. 创建无 LVGL 的 SDLPal 目标工程，保持 LCD、触摸、SDIO/DFS 和内存驱动可用。
2. 引入 SDLPal 无音频源码和最小 SDL 兼容层，通过主机单元测试与目标构建。
3. 完成竖屏索引画面转换、缩放和 LCD 刷新。
4. 完成色块虚拟按键和 ST7102 多触点输入。
5. 从 SD 卡加载资源，进入标题、新游戏、地图和战斗。
6. 运行内存及性能分析，调整片内 SRAM 与 HyperRAM 分配。
7. 完成第一阶段验收后，另行设计和实施音频。

音频阶段不得与第一阶段并行开始，避免显示、存储和内存问题与音频实时性问题相互干扰。
