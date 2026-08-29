# Edgi-Talk M55 Retro-Go 独立工程

该工程是从 `Edgi_Talk_M55_LVGL` 显示基础工程抽离出的独立 Retro-Go
移植，不编译、不链接 LVGL、LVGL demo、Virtual3D 或其它 GUI 示例。

工程只保留以下运行依赖：RT-Thread M55 内核与启动代码、PSoC Edge HAL/PDL、
LCD/GFXSS/VG-Lite、SDIO/FatFs、CherryUSB Host HID、I2C/PDL TDM 音频、HyperRAM
以及未经修改的 retro-go/gnuboy 与 gpSP portable interpreter 核心。

启动和游戏选择界面采用官方 retro-go launcher 的 GB/GBC/GBA carousel/browser
布局、主题资源和 VeraBold11 字体，通过本工程 platform shim 映射到 RT-Thread；
不引入 LVGL 字体、控件或渲染依赖。

`drivers/SConscript` 通过白名单直接编译原厂驱动文件，不调用 HAL/Common
聚合脚本，因此不会连带编译触控、PDM 录音或 soft-I2C；原厂文件本身保持
不变。默认音频由 platform 层直接调用 Infineon PDL TDM FIFO，并复用原厂
ES8388/I2C 驱动；不链接 RT-Audio、`drv_i2s.c`、ASRC 或音频线程。SConstruct
也不加载 LVGL、LittleFS、在线
packages、Virtual3D、retarget-io、serial-memory 或其它应用工程。

完整的 ROM、键位、构建、烧录和验收说明位于
[`applications/retro_go/README_zh.md`](applications/retro_go/README_zh.md)。
