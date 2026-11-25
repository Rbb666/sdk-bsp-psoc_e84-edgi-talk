# Edgi-Talk M55 Spine

在 Edgi-Talk 的 480×800 LCD 上演示 Spine 4.3 骨骼动画。默认加载 C012，居中播放待机动画。轻触播放动作，拖动移动角色。也可构建 Orbit 演示。

## 构建

准备 Python、SCons 和 Arm GNU Toolchain 13.3。在本工程目录执行以下命令。工具链的 `bin` 目录可加入 `PATH`，也可通过 `RTT_EXEC_PATH` 指定。

```powershell
python -m SCons -j 8
```

构建 Orbit 时加上 `model=orbit`。构建产物为 `rt-thread.elf`、`rtthread.hex` 和 `rtthread.map`。

## 烧录

板上须已有配套的 M33 安全与非安全启动固件。通过 KitProg3 连接板卡，在 Infineon OpenOCD 的 `bin` 目录运行以下命令，将 `D:/path/to/rtthread.hex` 换成本次构建的 HEX 绝对路径。

```powershell
./openocd.exe -s ../scripts -s ../flm/cypress/cat1d -f interface/kitprog3.cfg -f target/infineon/pse84xgxs2.cfg -c "set QSPI_FLASHLOADER ../flm/cypress/cat1d/PSE84_SMIF.FLM" -c "transport select swd" -c "set ENABLE_ACQUIRE 0" -c "init; reset init; flash write_image erase {D:/path/to/rtthread.hex}; verify_image {D:/path/to/rtthread.hex}; reset run; exit"
```

## 使用

通过 KitProg3 USB-UART 以 **115200 8N1** 打开 MSH。坐标单位为 LCD 像素，原点在左上角，X 向右，Y 向下。

| 命令 | 用途 |
| --- | --- |
| `spine_status` | 查看模型、帧率、锚点和错误状态 |
| `spine_move 160 400` | 将角色锚点移到指定坐标 |
| `spine_move by 40 -30` | 相对移动角色 |
| `spine_move center` | 将角色锚点恢复到屏幕中心 |
| `spine_demo <idle\|action\|dash\|hop>` | 播放待机、触摸动作、冲刺或弹跳 |

`spine_action` 也可触发触摸动作。C012 冲刺后停在新位置，弹跳后落回原位。

## 接入其他模型

1. 用 Spine 4.3 导出 `.skel`、`.atlas` 和单页预乘 Alpha PNG。旧版导出用对应版本恢复成工程，再用 4.3 重新导出。图集宽度须为 16 的倍数，解码缓冲占用 `宽 × 高 × 4` 字节。
2. 在工程目录运行 `python platform/assets/pack_pma_atlas.py model.png model.rle`。参考 [C012 素材描述](platform/assets/c012/c012_asset.c)和[内嵌方式](platform/assets/c012/c012_data.S)，配置图集、皮肤、待机和触摸动作。
3. 在 [SConstruct](SConstruct) 和 [platform/SConscript](platform/SConscript) 注册模型，再用 `python -m SCons -j 8 model=<id>` 构建。烧录前检查图形 SRAM 和 DTCM 余量。
