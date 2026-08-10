# SDLPal ESP32 清理与 RT-Thread SDL 端口设计

## 背景

`Edgi_Talk_M55_SDLPAL` 运行在 PSoC Edge M55 和 RT-Thread 上，但当前构建仍从
`sdlpal/upstream/esp32s3/native_engine_shim` 编译轻量 SDL 兼容层。该目录名来自
上游嵌入式分支的历史组织方式，不代表当前固件使用 ESP32-S3。

现有 `sdl_shim.c` 不能删除。SDLPal 主循环、输入、显示和 surface 管理直接依赖
其中的 SDL API，当前 ELF 也保留了 27 个由该文件提供的 `SDL_*` 符号。计时与
延时目前通过 `PalEngineBridge_GetTicks()` 和 `PalEngineBridge_Delay()` 间接调用
RT-Thread，行为有效但平台归属不清晰。

## 目标

1. `projects/Edgi_Talk_M55_SDLPAL` 的构建路径和源码中不再保留 ESP32、ESP32-S3
   或 Espressif 平台依赖。
2. 保留当前 SDL shim 提供的 surface、调色板、事件、键盘、计时和兼容函数，
   不改变 SDLPal 的显示、输入、存档和音频行为。
3. 将 shim 明确定义为 RT-Thread 端口，计时和线程延时直接使用 RT-Thread API。
4. 完成后项目默认配置可以通过完整 `scons` 构建，并继续满足 ELF 和静态栈约束。

## 非目标

- 不接入桌面版或完整 SDL 库。
- 不重构 SDLPal 游戏逻辑、显示算法、输入映射、存档或音频实现。
- 不启用上游的 `MEM_LEVEL1`、`MEM_LEVEL2`、`PAL_PAGED_EVENT_STATE` 或
  `PAL_EXTREME_TWO_SCREENS` 模式。
- 不回退或吸收工作区中已有的用户改动。

## 目录与构建结构

将以下文件从历史 ESP32-S3 目录迁移到 RT-Thread 端口目录：

```text
sdlpal/port/rtthread/
|-- sdl_shim.c
`-- include/
    |-- SDL.h
    |-- SDL_endian.h
    |-- SDL_events.h
    `-- SDL_video.h
```

`sdlpal/SConscript` 从新目录编译 `sdl_shim.c`。`sdlpal`、`platform` 和 `audio`
三个构建组统一包含新的 `include` 目录。迁移完成后删除空的历史平台目录。

## RT-Thread 时间实现

`sdl_shim.c` 直接包含 `rtthread.h`，时间接口按以下规则实现：

- `SDL_GetTicks()` 返回 `rt_tick_get_millisecond()` 的低 32 位，符合 SDL 32 位
  毫秒计数和自然回绕语义。
- `SDL_Delay(ms)` 对非零时长调用 `rt_thread_mdelay()`；输入超过单次安全范围时
  使用小于 `RT_TICK_MAX / 2` 的分段上限等待，避免 RT-Thread 定时器边界断言、
  有符号参数截断，同时保持请求总时长。
- `SDL_Delay(0)` 保持当前无操作语义，避免引入额外调度行为。
- `SDL_GetPerformanceCounter()` 返回 `rt_tick_get()`，
  `SDL_GetPerformanceFrequency()` 返回 `RT_TICK_PER_SECOND`，两者使用同一
  RT-Thread tick 时间基准。

从 `pal_engine_bridge.h/.c` 删除 `PalEngineBridge_GetTicks()` 和
`PalEngineBridge_Delay()`，避免重复封装。显示和输入仍通过 engine bridge 与
PSoC 驱动交互，因为这两类操作属于板级端口而不是 RTOS 通用能力。

## 失效平台分支清理

清理仅针对当前项目快照中明确属于其他平台、且当前配置没有启用的分支：

- 删除 `ESP_PLATFORM` 和 `esp_attr.h` 相关条件；GNU 构建继续使用已有的通用
  section 属性分支。
- 将依赖缺失平台文件的 `PAL_PAGED_EVENT_STATE` 和
  `PAL_EXTREME_TWO_SCREENS` 路径标记为本端口不支持，禁止未来误开启后得到
  难以定位的缺头文件错误。
- 保留与平台无关的 `MEM_LEVEL1`/`MEM_LEVEL2` 数据结构和算法，除非它们只用于
  上述不支持的专属路径。
- 更新 `sdlpal/UPSTREAM.md`，说明 shim 已由项目维护为 RT-Thread 端口。

清理后对项目源码、构建脚本和资源目录执行大小写不敏感扫描，`esp32`、
`esp32s3`、`espressif`、`ESP_PLATFORM` 和 `esp_attr.h` 均不得出现。设计文档
可以保留历史平台名称，用于记录清理边界和迁移原因。

## 错误处理与兼容性

RT-Thread API 在游戏线程上下文调用。`SDL_Delay()` 不在中断上下文使用；大延时
通过多个小于 `RT_TICK_MAX / 2` 的安全片段完成，不发生负数转换。32 位
`SDL_GetTicks()` 的回绕由现有 `SDL_TICKS_PASSED` 比较方式处理，不增加全局
状态或锁。

未启用的平台专属宏通过编译期错误显式拒绝。默认 PSoC 配置不定义这些宏，因此
不会改变当前生成代码。

## 验证策略

1. 修改前运行平台标识扫描并确认失败，证明检查能够捕获现有 ESP32-S3 内容。
2. 增加 RT-Thread SDL 时间契约测试，使用假的 RT-Thread tick/delay API 验证：
   毫秒读取、零延时、普通延时、安全上限分段、性能计数器及频率。
3. 迁移后运行契约测试和平台标识扫描，要求全部通过。
4. 运行现有项目检查器，确认 ELF 段布局、输入模式和单函数静态栈限制不回归。
5. 在 `projects/Edgi_Talk_M55_SDLPAL` 中使用项目 README 指定的工具链执行完整
   `scons -j16`，要求退出码为 0。
6. 对新 ELF 再次检查关键 SDL 符号和 RT-Thread API 引用，确认 shim 仍被链接，
   且不再引用已删除的时间 bridge 函数。
