# SDLPal USB Keyboard Input Modes Design

## 目标

在已通过实板枚举和按键日志验收的 CherryUSB HID Host 基础上完成两个阶段：

1. 将 USB Boot 键盘的方向、确认、取消和翻页按键接入 SDLPal 游戏交互。
2. 通过 Kconfig 在屏幕触摸与 USB 键盘之间编译期二选一；键盘模式不显示触摸按键，并将 320x200 游戏画面按 16:10 等比放大到当前旋转方向允许的最大尺寸。

当前工程默认使用 USB 键盘模式。配置可切回触摸模式，但不提供运行时切换或键盘断开后的触摸回退。

## 输入架构

新增 `pal_input_port.[ch]` 作为游戏线程唯一输入入口：

```c
bool pal_input_port_init(void);
uint32_t pal_input_port_poll(void);
```

`pal_engine_bridge.c` 不再直接依赖触摸驱动。它只轮询统一的 `PAL_CONTROL_*` 掩码，并继续使用现有状态差分和 SDL 事件队列生成 `SDL_KEYDOWN`/`SDL_KEYUP`。这保留触摸模式已经验证的事件顺序、长按行为和同帧释放后延迟按下逻辑，不修改 SDLPal 上游源码。

触摸模式下，`pal_input_port_poll()` 调用现有 `pal_touch_port_poll()`、坐标转换和按键命中检测，并通过 `pal_display_controls_set()` 更新虚拟按键状态。

键盘模式下，CherryUSB worker 在解析 Boot report 后维护原子的控制掩码，`pal_input_port_poll()` 读取该快照。USB worker 不直接写 SDL 事件队列，从而保持事件队列只有游戏线程一个写入者。

键盘映射保持阶段一已经打印并验证的定义：

| USB 键 | 控制位 | SDL 键 |
| --- | --- | --- |
| Up | `PAL_CONTROL_UP` | `SDLK_UP` |
| Down | `PAL_CONTROL_DOWN` | `SDLK_DOWN` |
| Left | `PAL_CONTROL_LEFT` | `SDLK_LEFT` |
| Right | `PAL_CONTROL_RIGHT` | `SDLK_RIGHT` |
| Enter | `PAL_CONTROL_A` | `SDLK_RETURN` |
| Escape | `PAL_CONTROL_B` | `SDLK_ESCAPE` |
| PageUp | `PAL_CONTROL_PGUP` | `SDLK_PAGEUP` |
| PageDown | `PAL_CONTROL_PGDN` | `SDLK_PAGEDOWN` |

Boot report 的按下事件设置控制位，释放事件清除控制位。键盘断开、设备代次变化和 Host 停止均清除全部控制位，防止角色或菜单方向卡住。未映射按键继续打印日志，但不改变游戏控制状态。

## Kconfig 与构建

在 `BSP_USING_SDLPAL` 下增加 choice：

- `BSP_SDLPAL_INPUT_TOUCH`
- `BSP_SDLPAL_INPUT_USB_KEYBOARD`，作为默认值

键盘模式选择 `RT_USING_CHERRYUSB`、`RT_CHERRYUSB_HOST`、`RT_CHERRYUSB_HOST_DWC2_INFINEON` 和 `RT_CHERRYUSB_HOST_HID`。触摸模式选择 RT-Thread touch 支持。`platform/SConscript` 只为选中的后端加入端口源文件和 include path。

预编译 `libusb_hc_dwc2.a` 使用 CherryUSB 默认 Host 结构布局，因此键盘模式固定 `CONFIG_USBHOST_MAX_INTF_ALTSETTINGS=12`。不得通过缩小该值节省 DTCM；Host 总线、Hub、HID 和 DWC2 状态继续放在 Secondary SRAM 的 `.usb_host_data` 段。

触摸模式不启动 USB Host。链接脚本允许 `.usb_host_data` 和 `.sdlpal_usb` 为空；键盘模式仍要求 Host 段为 28--64 KiB、worker 段为 2--4 KiB。ELF 检查器按输入模式验证对应约束。

## 显示布局

新增纯计算 viewport 接口，由 VG-Lite 和 CPU fallback 共用。游戏源画面始终为 320x200，目标矩形始终保持 16:10。

触摸模式保持现有布局：

- 0/180 度：逻辑屏幕 480x800，游戏矩形 `(0, 0, 480, 300)`。
- 90/270 度：逻辑屏幕 800x480，游戏矩形 `(160, 0, 480, 300)`。
- 游戏矩形之外绘制现有虚拟方向、确认、取消和翻页按键。

键盘模式使用最大等比布局：

- 0/180 度：游戏矩形 `(0, 250, 480, 300)`，上下各 250 像素黑边。
- 90/270 度：游戏矩形 `(16, 0, 768, 480)`，左右各 16 像素黑边。

键盘模式不采样触摸、不绘制按键，也不执行按键脏区域刷新。首个游戏帧主动清黑 viewport 之外的区域，防止启动状态页和旧图像残留。

CPU fallback 的索引色转换改为接受目标宽高，使用 `src_x = dst_x * 320 / dst_width` 和 `src_y = dst_y * 200 / dst_height` 做最近邻采样。工作 strip 宽度扩展到 800 像素，使横屏 768 像素 viewport 不需要额外横向分块。VG-Lite 使用同一目标矩形。

## 启动与错误处理

`boot_initialize_io()` 只初始化所选输入后端：

- 触摸模式要求触摸设备初始化成功。
- 键盘模式要求 Host 控制器和 worker 初始化成功，但不要求启动时已经插入键盘。

所选输入后端初始化失败时使用现有 `E03` 启动错误。键盘可在游戏启动前后热插拔；断开后游戏继续运行且控制掩码归零，重新连接后恢复输入。枚举、忽略的非 Boot HID、传输错误、连接和断开日志继续保留。

## 测试与验收

主机测试覆盖：

- Boot report 映射、多键同时按下、释放、断开清键和未映射键。
- 原子键盘控制快照到 SDL 事件的 DOWN/UP 顺序。
- 触摸模式继续生成与当前相同的控制掩码和显示更新。
- 四种旋转下触摸与键盘 viewport 的位置、尺寸和 16:10 比例。
- CPU fallback 对 480x300 和 768x480 目标的边界采样。
- Kconfig choice、默认键盘模式、条件编译和 DWC2 ABI 配置契约。
- ELF 中键盘模式的 Host/worker 段预算，以及触摸模式不携带 Host 静态段。

目标构建覆盖 2 种输入模式乘以 4 种旋转，共 8 个配置。每个配置运行 ELF 和栈预算检查。

实板键盘模式验收：

- 方向键可在菜单和地图中持续移动。
- Enter/Escape 可确认和取消。
- PageUp/PageDown 可完成游戏中对应翻页操作。
- 组合键不会丢失已按下状态。
- 拔出键盘时无卡键，重新插入后恢复。
- 横屏画面为 768x480 且无触摸按键残留。

实板触摸模式回归：虚拟按键布局、按下高亮、方向、确认、取消和翻页行为保持现状。

## 非目标

- 不支持 NKRO 或厂商自定义非 Boot 键盘报告。
- 不增加运行时输入模式切换。
- 不在键盘断开时自动启用触摸。
- 不修改 SDLPal 上游按键语义或增加新的游戏功能键。
