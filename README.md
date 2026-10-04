# Charybdis (XI-MK1_2) — DYA Studio + Monitor 固件

这是为 Charybdis 分体键盘（`XI-MK1_2`）维护的 ZMK 固件仓库。

本项目基于 `main-20250226` 分支，参照 [Noirix44 monitor 分支](https://github.com/NoirGuo/zmk-config-Noirix44/tree/monitor) 移植：
在**保留原有 keymap 与 layout 完全不变**（键位、旋钮、RGB、轨迹球、11 层布局）的前提下，升级 ZMK/Zephyr 技术栈，
并增加 DYA Studio、运行时配置以及 **Monitor（独立 OLED 状态监视器）** 功能。

## 功能特性

- DYA Studio 改键（**右半**通过 USB 串口，右半为 split central）
- Runtime Macro（运行时宏）
- Runtime Combo（运行时组合键）
- Runtime Sensor Rotate（运行时传感器/编码器配置）
- Runtime Input Processor（运行时输入处理器）
- BLE 管理（DYA Studio 查看/管理蓝牙连接）
- Settings RPC（DYA Studio 在线修改并保存设置）
- Custom Settings（DYA Studio 自定义显示设置，可运行时调整）
- Device Info（固件、硬件和运行状态诊断）
- 键盘按键统计（累计按键数，NVS 持久化）
- WPM 打字速度统计
- **OLED 状态监视器（Monitor）**：
  - 独立接收器实时监听键盘状态广播（Prospector 协议，固定频道 `1`）
  - 底部三根电量横条 + 百分比数字：左 = 左手电量、中 = **Monitor 自身电量**、右 = 右手电量
  - 右上角连接状态：USB 状态（`U`/`-`）+ BLE 连接（`B`）+ **当前 BLE Profile 数字**（如 `- B0`）
  - 左上 WPM、中央大字号层名、下方修饰键名称（CTRL/SHIFT/ALT/GUI）
  - 键盘失联 15 秒后屏幕提示（层名位置显示 `WAITING`）
- keymap drawer 自动生成键位图（`keymap-drawer/charybdis.svg`）

> **重要**：Charybdis 的 split central 在**右半**（与原仓库一致），因此 USB 直连电脑、DYA Studio 串口连接
> 都在**右半**，与 Noirix44（左半为 central）操作相反，请勿接错。

## DYA Studio

本固件的大部分功能（改键、Runtime Macro、Runtime Combo、BLE 管理、Settings、Device Info）都通过 DYA Studio 操作，
使用前请先用 **USB 连接键盘右半（central）**，再打开工具：

- **网页版（免安装，推荐）：** https://studio.dya.cormoran.works/
- **桌面客户端下载：** https://github.com/cormoran/dya-studio/releases

浏览器使用网页版时，若提示串口被占用，请关闭其他 DYA Studio 页面或占用串口的软件。

### 技术栈

- ZMK：`cormoran/zmk#main+dya`
- Zephyr：`v4.1.0+zmk-fixes+nrf-half-duplex-uart`
- Prospector 状态广播：`prospector-zmk-module`（`OLED` 分支）
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

## 固件文件

GitHub Actions 构建完成后，在运行记录的 Artifacts 中下载固件压缩包。

| 固件 | 刷写位置 | 主控 |
| --- | --- | --- |
| `charybdis_left.uf2` | 键盘左半（peripheral，带 EC11 旋钮） | nice!nano (nRF52840) |
| `charybdis_right.uf2` | 键盘右半（central，带 PMW3610 轨迹球） | nice!nano (nRF52840) |
| `charybdis_monitor_display.uf2` | 独立 OLED 状态监视器 | nice!nano |
| `charybdis_settings_reset.uf2` | 清除键盘配对与设置 | nice!nano |
| `charybdis_monitor_settings_reset.uf2` | 清除监视器配对与设置 | nice!nano |

> 键盘左右半、监视器、两个 settings_reset 都是 nice!nano，请按上表对应刷写，不要混刷。

## Monitor 模式

本版固件为 Charybdis 增加独立的 OLED 状态监视器，采用 Prospector 广播协议，固定频道为 `1`。

工作方式：

| 设备 | 角色 |
| --- | --- |
| `charybdis_right` | 键盘右半 = central：直连电脑（USB HID）+ 连接左半（BLE）+ **广播状态给监视器** |
| `charybdis_left` | 键盘左半 = peripheral：仅通过 BLE 连接右半 |
| `charybdis_monitor_display` | 独立接收器：无按键，只监听状态广播并显示，不输出键盘 HID |

监视器屏幕实时显示（固定单布局，128×64，无主题切换）：

```
WPM 42          - B0
     BASE
  CTRL SHIFT
90%      80%      75%
[====]   [====]   [====]
```

- 左上：WPM 打字速度（未收到广播时显示 `WPM --`）
- 右上：连接状态 + 当前 BLE Profile。格式为 `U B0` / `- B0` / `U -` / `- -`：
  - 第 1 位 `U` = 键盘通过 USB 直连电脑，`-` = 未插 USB
  - `B` 后跟数字 = BLE 已连接时的 Profile 编号（来自键盘广播）
  - BLE 未连接时不显示 B 与数字
- 中央（大字号）：当前层名（来自键盘广播的 layer_name）；无层名时回退显示 `LAYER <n>`
- 中央下方：当前按下的修饰键名称（CTRL / SHIFT / ALT / GUI 组合）
- 底部三根横条 + 百分比数字：
  - 左条 = 左手电量，右条 = 右手电量，**中条 = Monitor 自身电量**（每 60 秒采样一次）
  - 收到广播前或电量不可用显示 `--%`
- 键盘失联（开机后未收到广播，或超过 15 秒没有新广播）：
  - 中央层名位置显示 `WAITING`
  - WPM 显示 `WPM --`、连接状态显示 `-- --`、左右手电量显示 `--%`
  - 中条（Monitor 自身电量）继续实时显示

> **左右电量映射说明**：Charybdis 的 central 在右半，广播端已按 `ZMK_STATUS_ADV_CENTRAL_SIDE="RIGHT"`
> 在协议层完成左右交换（`battery_level` 字段 = 左物理侧、`peripheral_battery[0]` = 右物理侧），
> 因此监视器左条始终是左手电量、右条始终是右手电量，与 Noirix44（central=左）显示一致。

Monitor 接收器是无按键的纯显示设备，不启用 ZMK Studio，因此无法通过 DYA Studio 编辑接收器设置；
键盘侧的 DYA 功能不受影响。

切换拓扑或升级固件前建议先刷对应的 `settings_reset`，然后重新配对左半与右半 central。

## Runtime Macro / Combo / Sensor Rotate / Input Processor

四个运行时功能均已启用（编译开关在 `charybdis_right.conf`，Studio RPC 均开启）：

- **Runtime Macro**：可在 DYA Studio 中创建/修改宏。keymap 第 2 层（layer_2）第一行最右侧按键已预留绑定：

  ```dts
  &rmacro 0
  ```

  使用方法：USB 连接右半 → 打开 DYA Studio → 进入 Macro 页面 → 创建 Macro 并确认 Slot 编号
  （Slot 0 对应当前预留的 `&rmacro 0` 按键）→ 保存后宏写入键盘设置。
  刚刷入固件、尚未创建 Slot 0 时，按下该键不会执行任何内容。
- **Runtime Combo**：可在 DYA Studio 中运行时创建组合键。固件只预留槽位，未增加默认 Combo，首次刷写不改变现有按键行为。
- **Runtime Sensor Rotate**：可在 DYA Studio 中运行时调整传感器（轨迹球/编码器）方向。
- **Runtime Input Processor**：可在 DYA Studio 中运行时调整输入处理器（如轨迹球滚动转换）。

## Device Info

键盘右半固件启用 Device Info。通过 USB 连接右半并打开 DYA Studio 的 Troubleshooting 页面后，可以查看：

- ZMK、Zephyr、配置仓库及模块的版本信息
- 编译时间、板型和固件 Build ID
- MCU、Flash、SRAM 和上次复位原因
- USB、BLE、分体、显示等编译配置
- 运行时间和 Zephyr 设备初始化状态

## 按键统计

键盘右半统计物理按键按下次数并 NVS 持久化：

- 仅统计按键按下事件（长按自动重复只计一次物理按下）
- 不统计编码器/鼠标等非按键事件

## Monitor 硬件接线

- 主控：nice!nano
- 屏幕：SH1106 128×64 OLED（I2C 地址 0x3C）
- 接线：OLED SDA → P0.17，SCL → P0.20（I2C0，与 Noirix44 / Sofle Monitor 接收器同款）
- 若你的 OLED 接线不同，修改 `boards/shields/monitor_adapter/monitor_adapter.overlay` 中的 `psels`

## 轨迹球（PMW3610）

右半轨迹球驱动已从第三方模块（DoctorWangWang/zmk-pmw3610-driver，基于 Zephyr 3.5、已停更）迁移到 **Zephyr 4.1 主线驱动**，
功能等价迁移（方向、CPI、滚动层行为均保持）：

- 原 `ORIENTATION_90 + INVERT_X` → 主线 `invert-x + invert-y` + input-processor `zip_xy_swap_mapper`（方向一致）
- 原 `scroll-layers = <1>` → `zmk,input-listener` child-binding（层 1 滚动，X/Y 移动转滚轮）
- 原 `CPI=400` → `res-cpi = <400>`；原 `SMART_ALGORITHM` → `smart-mode`

若实测轨迹球方向/滚动与预期不符，调整 `boards/shields/charybdis/charybdis_right.overlay` 中
`trackball` 节点的 `invert-x`/`invert-y` 与 `trackball_listener` 的 input-processors。

## 编译

仓库使用 GitHub Actions 自动构建，并自动生成键位图：

1. 推送代码后打开 Actions。
2. 运行 Build workflow（或向分支提交一次改动触发）。
3. `build` job 产出 5 个固件（左右半 / monitor / 两个 settings_reset）。
4. `draw` job 自动生成键位图到 `keymap-drawer/charybdis.svg`（**绘制全部层 0-10**；2-10 层为 `&trans` 占位，以空白层形式呈现）。
5. 下载 Artifacts。

键位图配置见 `keymap_drawer.config.yaml`（按 Charybdis 实际 binding 定制），
布局使用 `config/charybdis.json` 的 `default_transform`。

## 已知问题与使用建议

### 左半（peripheral）单独断电重开无法自动重连

**现象**：只关闭左半键盘的电源再重新打开，左半有时无法自动与右半（central）重连；
把右半也关闭再打开（或左右手同时重启）后即可恢复连接。

**原因**：这是 ZMK 分体 BLE 的已知时序问题。左半断电属于"非优雅断开"，右半需要等待 supervision
timeout（默认约 4 秒）才感知断开并重新扫描；而左半重新上电后先进入短暂的直连广播窗口，随后转为
低速广播（约 1.28 秒一次、窗口极小）。当右半的重扫窗口与左半的低速广播错开时，就会持续错过，
直到右半重启、BLE 栈整体复位后才重新对齐。

**使用建议**：

- 日常使用中，左半中途断电重开时，先等待 **5~10 秒**，一般可自动连回；
- 若超过 10 秒仍未连回，将左右半同时关闭再打开即可恢复；
- 这是 ZMK 生态的普遍现象，不是硬件故障，不影响正常使用；
- 当前版本**不做代码层面修复**，保持 ZMK 原生重连行为。

## 注意事项

- **DYA Studio 请连接右半（central）串口**，不是左半。
- 修改 DYA 运行时设置前，确保连接的是键盘右半串口。
- 浏览器提示串口已打开时，关闭其他 DYA Studio 页面或占用串口的软件。
- 刷写新版固件后出现连接问题时，优先执行一次完整的 Settings Reset 和重新配对。
- 建议键盘左半、右半和监视器使用同一次 Actions 构建生成的固件，不要混用不同分支或不同构建批次。
- 清除设置会删除已保存的蓝牙配对和运行时配置。
- 本固件仍需通过实机验证后再作为日常固件使用；如遇异常，保留 Actions 日志以便排查。

## 键位图

自动生成的键位图见仓库 `keymap-drawer/` 目录（`charybdis.svg` / `charybdis.yaml`）。
