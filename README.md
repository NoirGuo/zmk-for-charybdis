# Charybdis (XI-MK1_2) — Dongle 模式固件（DYA Studio + GC9A01 屏）

> 分支：`4.0-dya-dongle-gc9a01`（dongle 模式）　·　ZMK：`cormoran/zmk#main+dya`　·　Zephyr：`v4.1.0+zmk-fixes+nrf-half-duplex-uart`

这是为 Charybdis 分体键盘（`XI-MK1_2`）维护的 ZMK 固件仓库，参照
[S7venYoung/zmk-sofle-dongle-dya `4.1` 分支](https://github.com/S7venYoung/zmk-sofle-dongle-dya/tree/4.1)
的 **dongle（独立接收器）模式**重写：

- **独立接收器（Dongle）**：nice_nano v2 + GC9A01 240×240 圆形屏，USB 直连电脑输出键盘/鼠标 HID；
- **左右手均为纯无线 peripheral**：不再需要任何半区插 USB，不再有独立 OLED 监听广播。

在**保留原有 keymap 与 layout 完全不变**（键位、旋钮、RGB、轨迹球、11 层布局）的前提下，
升级 ZMK/Zephyr 技术栈，并增加 DYA Studio、运行时配置与 Dongle 屏幕功能。

## 架构说明

```
┌────────────┐   BLE split   ┌──────────────────────────────┐
│ 左手        │◄────────────►│ Dongle（split central）        │
│ (peripheral)│   position    │  - GC9A01 圆形屏（本地聚合显示）│
└────────────┘               │  - USB 连电脑（HID + Studio） │
┌────────────┐   BLE split   │  - 轨迹球/传感器事件汇聚        │
│ 右手        │◄────────────►│  - DYA Studio 全协议           │
│ (peripheral)│  position/    └──────────────────────────────┘
│  + 轨迹球    │  sensor
└────────────┘
```

- **按键**：左右手按键位置经 split 上报到 Dongle，Dongle 生成 HID 报告；
- **轨迹球**：右半（peripheral）的 PMW3610 原始 input 事件经 `zmk,input-split`
  转发到 Dongle，Dongle 上的 `zmk,input-listener` 完成方向交换/滚动转换后输出鼠标 HID
  （与原"轨迹球在右半 central"的行为一致）；
- **屏幕数据**：全部在 Dongle 本地聚合（层名、WPM、修饰键、输入字符、左右手电量、
  Dongle 自身电量），不再依赖任何 BLE 广播协议。

## 功能特性

**核心功能**

- DYA Studio 改键（**USB 连接 Dongle**）
- Runtime Macro（运行时宏，第 4 层左上角绑定 `&rmacro 0`）
- Runtime Combo（运行时组合键）
- Runtime Sensor Rotate（运行时传感器/编码器配置）
- Runtime Input Processor（运行时输入处理器）
- BLE 管理（DYA Studio 查看/管理蓝牙连接）
- Settings RPC（DYA Studio 在线修改并保存设置）
- Custom Settings（DYA Studio 自定义显示设置，可运行时调整）
- Device Info（固件、硬件和运行状态诊断）
- 键盘按键统计（累计按键数，NVS 持久化）
- WPM 打字速度统计

**Dongle 屏幕（GC9A01 240×240 圆形屏）**

- 三根电量弧 + 百分比：左 = 左手电量、中 = **Dongle 自身电量**、右 = 右手电量
- 顶部状态：USB 连接（`USB`/`--`）+ BLE Profile（`BLE 0`/`--`）
- 左上 WPM、中央大字号层名（`display-name`）、下方最近输入字符、修饰键图标
- 修饰键使用 Mac 风格图标（CTRL/SHIFT/ALT/CMD）
- Dongle 端本地聚合，无"键盘失联 WAITING"状态

**其他**

- keymap drawer 自动生成键位图（`keymap-drawer/charybdis.svg`）
- 左右手 / Dongle 三份固件由同一 Actions 构建，settings_reset 固件通用

## 技术栈

- ZMK：`cormoran/zmk#main+dya`（Zephyr `v4.1.0+zmk-fixes+nrf-half-duplex-uart`）
- DYA Studio Custom Protocol
- `zmk-feature-custom-settings`
- `zmk-feature-device-info`
- `zmk-feature-runtime-macro`
- `zmk-feature-runtime-combo`
- `zmk-behavior-runtime-sensor-rotate`
- `zmk-module-ble-management`
- `zmk-module-battery-history`
- `zmk-module-settings-rpc`
- `zmk-module-runtime-input-processor`
- `mario-peripheral-animation`

（原独立 OLED 监视器使用的 `prospector-zmk-module` 广播协议已随 dongle 模式移除；
原 `DoctorWangWang/zmk-pmw3610-driver` 已移除，PMW3610 由 Zephyr 4.1 主线自带驱动接管。）

## 固件文件

GitHub Actions 构建完成后，在运行记录的 Artifacts 中下载固件压缩包。

| 固件 | 刷写位置 |
| --- | --- |
| `charybdis_dongle_display.uf2` | 独立接收器（Dongle） |
| `charybdis_left.uf2` | 键盘左手 |
| `charybdis_right.uf2` | 键盘右手 |
| `charybdis_settings_reset.uf2` | 清除配对与设置（左右手/Dongle 通用，nice_nano v2） |

升级/首次使用建议接收器、左手、右手使用**同一次 Actions 构建**的固件，不要混用不同分支或构建批次。
如连接异常，可先刷 `settings_reset` 清空配对，再重新刷入三份固件并重新配对
（左右手与 Dongle 同为 nice_nano v2，settings_reset 固件通用）。

## 配对步骤

1. 给 Dongle 供电（USB 连电脑），刷入 `charybdis_dongle_display.uf2`。
2. 左右手分别刷入 `charybdis_left.uf2` / `charybdis_right.uf2`。
3. 首次开机左右手会自动广播，Dongle 作为 split central 自动连接左右两个 peripheral
   （`CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=2`）。
4. Dongle 通过 USB 向电脑输出键盘/鼠标 HID，同时可用 DYA Studio 管理蓝牙。

> 蓝牙 profile（0–4）作用于 Dongle 与电脑之间；左右手与 Dongle 之间是固定的 split 链路，
> 无需（也无法）单独管理左右手的 BLE profile。

**连接异常排查**

- 左右手与 Dongle 无法自动配对：三台设备都刷 `settings_reset` 清空配对后，再重新刷入正式固件重新配对；
- 仅右手（带轨迹球）无响应：确认右手的 PMW3610 接线，且左右手与 Dongle 构建批次一致；
- Dongle 连电脑无输出：确认 USB 线数据通路正常，DYA Studio 里能看到设备说明 USB 正常。

## DYA Studio

本固件的大部分功能（改键、Runtime Macro、Runtime Combo、BLE 管理、Settings、Device Info）
都通过 DYA Studio 操作，使用前请先用 **USB 连接 Dongle**，再打开工具：

- **网页版（免安装，推荐）：** https://studio.dya.cormoran.works/
- **桌面客户端下载：** https://github.com/cormoran/dya-studio/releases

浏览器使用网页版时，若提示串口被占用，请关闭其他 DYA Studio 页面或占用串口的软件。

## Dongle 屏幕接线（GC9A01 240×240 圆形屏）

8 针屏（VCC / GND / SCL / SDA / DC / CS / RST / BL），Dongle 的 nice_nano v2 接线：

| 屏引脚 | nice_nano v2 |
| --- | --- |
| SCL (SCK) | P0.17 |
| SDA (MOSI) | P0.20 |
| CS | P1.00 |
| DC | P0.24 |
| RST | P0.22 |
| BL（背光，PWM） | P0.11 |

> 本接线仅适用于 Dongle（独立接收器）；左右手不含屏幕。
> 屏幕使用 SPI0，与 RGB 总线（spi3，MOSI=P0.10）互不冲突。

## 已知限制

- Dongle 模式下轨迹球事件经 split 转发，链路依赖左右手与 Dongle 的 BLE 连接；
  断开连接时轨迹球与键盘同时不可用（与有线 central 模式不同）；
