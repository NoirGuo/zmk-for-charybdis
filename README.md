# Charybdis 接入 DYA Studio 完整实施说明

本文档说明如何将 `zmk-for-charybdis` 的 `main-20250226` 分支改造为支持 **DYA Studio** 全部功能，并附带改动依据。

> 参考：`zmk-config-Noirix44`（`dya` 分支）与 [DYA Studio 开发者指南](https://studio.dya.cormoran.works/developer-guide)。

---

## 一、改造目标与结论速览

`main-20250226` 当前状态：**ZMK v0.3（zmkfirmware）+ Zephyr 3.x**，仅有一个自定义模块 `zmk-pmw3610-driver`（DoctorWangWang 的轨迹球驱动），**无任何 DYA 功能**。

改造后：**ZMK `main+dya`（cormoran）+ Zephyr 4.1**，引入全部 DYA 功能模块，轨迹球替换为 cormoran 的带 RPC 驱动，encoder（旋钮）启用运行时配置。

**关键硬件事实**（决定配置的关键差异）：

| 事实 | 值 | 影响 |
|---|---|---|
| right 半是 **central** | `Kconfig.defconfig` 中 `SHIELD_CHARYBDIS_RIGHT` 默认 `ZMK_SPLIT_BLE_ROLE_CENTRAL=y` | Studio 走 right 半 USB，right 半承载全部 central 侧功能 |
| left 半是 **peripheral** | 反向 | left 半只承载最小 peripheral 功能 |
| 轨迹球（PMW3610）在 **right 半** | `charybdis_right.overlay` | 轨迹球是 **central 本地设备**，无需 split RPC relay |
| 旋钮（EC11）两侧都有 | `charybdis.dtsi`（left 半 `left_encoder_1` 启用，right 半 `right_encoder` 启用） | 两侧均需 encoder 配置 |

---

## 二、完整改动清单

### 2.1 `config/west.yml`（核心改动）

| 变更 | 原值 | 新值 | 依据 |
|---|---|---|---|
| `remotes` 增加 `cormoran` | 无 | `url-base: https://github.com/cormoran` | 所有 DYA 模块与内核都在 cormoran |
| `projects.zmk.remote` | `zmkfirmware` | `cormoran` | 需 Custom Studio Protocol，仅 cormoran 支持 |
| `projects.zmk.revision` | `v0.3` | `main+dya` | 开发者指南指定的 DYA 内核分支 |
| `projects.zmk.import` | `app/west.yml` | `file: app/west.yml` | 新 west 格式需显式 `file:` |
| 新增 `projects.zephyr` | 无（v0.3 内置） | `cormoran/zephyr#v4.1.0+zmk-fixes+nrf-half-duplex-uart` + `name-blocklist` | Zephyr 4.1 需显式引入，用 cormoran 修复版 |
| 删除 `projects.zmk-pmw3610-driver` | `DoctorWangWang` | 删除 | 替换为 cormoran 版本 |
| 新增 `projects.zmk-driver-pmw3610-with-custom-studio-rpc` | 无 | `cormoran#main` + `import: true` | 轨迹球驱动替换，自带 RPC |
| 新增 8 个 DYA 模块 | 无 | 见下表 | 对齐 noirix44 + 开发者指南 |

**新增的 DYA 模块清单**：

| 模块 | 用途 | 备注 |
|---|---|---|
| `zmk-feature-custom-settings` | 自定义设置持久化（多个模块依赖底座） | 必须 |
| `zmk-feature-device-info` | 固件/硬件/运行状态诊断 | — |
| `zmk-feature-runtime-macro` | 运行时宏 | `import: true` |
| `zmk-feature-runtime-combo` | 运行时组合键 | `import: true` |
| `zmk-behavior-runtime-sensor-rotate` | 运行时旋钮（encoder）配置 | — |
| `zmk-module-ble-management` | BLE 连接管理 | — |
| `zmk-module-battery-history` | 电池历史 | — |
| `zmk-module-settings-rpc` | 在线改设置 | — |
| `zmk-module-runtime-input-processor` | 运行时输入处理器（轨迹球灵敏度调节） | — |

---

### 2.2 `config/charybdis.conf`（主配置，两侧通用）

在原配置基础上**新增**以下 DYA 开关（保留 RGB / 旋钮 / 鼠标 / BLE 原有配置）。

> ⚠️ **关键分层原则**：`*_STUDIO_RPC` / `*_RPC_STUDIO` / `SPLIT_RPC_RELAY` 这些配置会
> 依赖或 `select` 出 `proto/zmk/custom.pb.h` 的编译需求，而该 proto 只在
> `CONFIG_ZMK_STUDIO_RPC=y` 时由 ZMK 核心生成（`ZMK_STUDIO` 通过
> `select ZMK_STUDIO_RPC if !ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL` 仅在 **central 半**开启）。
> 因此这些配置**只能放在 central 半**（`charybdis_right.conf`），**不能放在两侧通用的
> `charybdis.conf`**，否则 peripheral（left）半会因 `fatal error: proto/zmk/custom.pb.h:
> No such file or directory` 而编译失败。

```ini
# 内核升级后运行时绑定必需
CONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS=y

# 自定义设置（底座）
CONFIG_ZMK_CUSTOM_SETTINGS=y

# 设备信息（底座）
CONFIG_ZMK_DEVICE_INFO=y

# 运行时宏（底座）
CONFIG_ZMK_RUNTIME_MACRO=y
CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_CRC16=y

# 运行时组合键（底座）
CONFIG_ZMK_RUNTIME_COMBO=y

# 运行时旋钮（底座）
CONFIG_ZMK_RUNTIME_SENSOR_ROTATE=y

# 设置 RPC（底座）
CONFIG_SETTINGS=y
CONFIG_ZMK_SETTINGS_RPC=y

# 栈/缓冲区（两侧通用部分）
CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=4096
CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN=240
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096
```

> 各模块的 `*_STUDIO_RPC` 变体及 `CUSTOM_SETTINGS_SPLIT_RPC_RELAY`、RPC 专属栈参数
> （`ZMK_STUDIO_RPC_*`）统一放在 central 半的 `charybdis_right.conf`（见 2.3 节）。

---

### 2.3 `boards/shields/charybdis/charybdis_right.conf`（central 半）

**最大改动**：轨迹球驱动替换 + central 侧 DYA 功能。

**删除**旧驱动专属选项：

```ini
# 以下旧驱动（DoctorWangWang）专属选项已删除：
# CONFIG_PMW3610_CPI=400
# CONFIG_PMW3610_CPI_DIVIDOR=1
# CONFIG_PMW3610_ORIENTATION_90=y
# CONFIG_PMW3610_SNIPE_CPI=200
# CONFIG_PMW3610_SNIPE_CPI_DIVIDOR=1
# CONFIG_PMW3610_SCROLL_TICK=70
# CONFIG_PMW3610_POLLING_RATE_125_SW=y
```

**新增**新驱动 + central 侧功能：

```ini
# 轨迹球驱动（cormoran 版，compatible = "cormoran,pmw3610"）
CONFIG_PMW3610=y
CONFIG_PMW3610_SMART_ALGORITHM=y
CONFIG_PMW3610_INVERT_X=y

# 轨迹球 Studio RPC + 运行时设置
CONFIG_ZMK_PMW3610_STUDIO_RPC=y
CONFIG_ZMK_PMW3610_CUSTOM_SETTINGS=y

# central 侧 DYA 功能
CONFIG_ZMK_STUDIO=y
CONFIG_BT_MAX_CONN=5
CONFIG_BT_MAX_PAIRED=5
CONFIG_ZMK_BLE_MANAGEMENT=y
CONFIG_ZMK_BLE_MANAGEMENT_STUDIO_RPC=y
CONFIG_ZMK_BATTERY_HISTORY=y
CONFIG_ZMK_BATTERY_HISTORY_STUDIO_RPC=y
CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y
CONFIG_ZMK_SETTINGS_RPC=y
CONFIG_ZMK_SETTINGS_RPC_STUDIO=y
CONFIG_ZMK_SPLIT_RELAY_EVENT=y
CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_STACK_SIZE=768
CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR=y
CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR_STUDIO_RPC=y

# DYA 功能模块的 Studio RPC 变体（仅 central 半，见 2.2 节分层原则）
CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y
CONFIG_ZMK_CUSTOM_SETTINGS_SPLIT_RPC_RELAY=y
CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC=y
CONFIG_ZMK_RUNTIME_MACRO_STUDIO_RPC=y
CONFIG_ZMK_RUNTIME_COMBO_STUDIO_RPC=y
CONFIG_ZMK_RUNTIME_SENSOR_ROTATE_STUDIO_RPC=y

# RPC 专属栈参数（仅 central 半）
CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=256
CONFIG_ZMK_STUDIO_RPC_TX_BUF_SIZE=256
CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_REQUEST_PAYLOAD_MAX_BYTES=256
CONFIG_ZMK_STUDIO_RPC_THREAD_STACK_SIZE=6000
CONFIG_ZMK_CUSTOM_SETTINGS_LARGE_VALUE_MAX_SIZE=256
```

> ⚠️ **轨迹球无需 split RPC relay**：轨迹球在 right 半（central），是本地设备，不是分体外设。因此**不需要** `CONFIG_ZMK_PMW3610_SPLIT_RPC_RELAY`。

---

### 2.4 `boards/shields/charybdis/charybdis_left.conf`（peripheral 半）

只承载 peripheral 侧最小 DYA 功能：

```ini
CONFIG_ZMK_BATTERY_HISTORY=y
CONFIG_ZMK_SETTINGS_RPC=y
CONFIG_ZMK_SPLIT_RELAY_EVENT=y
CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE=10000
```

（保留原有 EC11 + pointing 配置）

---

### 2.5 `boards/shields/charybdis/charybdis_right.overlay`（轨迹球节点）

**关键改动**：trackball 节点的 `compatible` 从 `pixart,pmw3610` 改为 `cormoran,pmw3610`，并补充新驱动 binding 的 required 属性：

```dts
trackball: trackball@0 {
    status = "okay";
    compatible = "cormoran,pmw3610";   /* 原为 pixart,pmw3610 */
    reg = <0>;
    spi-max-frequency = <2000000>;
    irq-gpios = <&gpio0 6 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
    /* 以下为新驱动 binding 的 required 属性 */
    evt-type = <INPUT_EV_REL>;
    x-input-code = <INPUT_REL_X>;
    y-input-code = <INPUT_REL_Y>;
    cpi = <400>;
    settings-id = "trackball";
};
```

同时文件顶部新增 `#include <zephyr/dt-bindings/input/input-event-codes.h>`（提供 `INPUT_EV_REL` / `INPUT_REL_X` / `INPUT_REL_Y` 宏）。

---

### 2.6 `config/charybdis.keymap`（运行时宏 + 运行时旋钮）

**新增 include**：

```dts
#include <behaviors/runtime_macro.dtsi>
#include <behaviors/runtime-sensor-rotate.dtsi>
```

> `&studio_unlock` 无需单独 include —— 内核的 `#include <behaviors.dtsi>` 已自动引入 `behaviors/studio_unlock.dtsi`。

**新增运行时旋钮行为**（encoder 支持，可在线改绑定）：

```dts
behaviors {
    rsr_encoder_1: rsr_encoder_1 {
        compatible = "zmk,behavior-runtime-sensor-rotate";
        #sensor-binding-cells = <0>;
        tap-ms = <5>;
        cw-binding = <&msc SCRL_DOWN>;
        ccw-binding = <&msc SCRL_UP>;
    };
    rsr_encoder_2: rsr_encoder_2 {
        compatible = "zmk,behavior-runtime-sensor-rotate";
        #sensor-binding-cells = <0>;
        tap-ms = <5>;
        cw-binding = <&msc SCRL_LEFT>;
        ccw-binding = <&msc SCRL_RIGHT>;
    };
};
```

**encoder 绑定替换**：各层的 `sensor-bindings` 从静态 `&scroll_vertical_encoder` 改为运行时 `&rsr_encoder_1 &rsr_encoder_2`。

**预留 runtime macro 与 studio_unlock 绑定**（CONF 层）：

```dts
layer_2 {
    display-name = "CONF";
    bindings = <
&studio_unlock  &trans  ...  &rmacro 0
...
    >;
};
```

> `&studio_unlock` 是必需的：轨迹球 RPC 子系统受 Studio 锁保护，需在 keymap 绑定解锁键，否则 Studio 锁定后无法再使用轨迹球 RPC（含 Web UI）。

---

### 2.7 `build.yaml`（构建矩阵）

**关键改动**：snippet 从 left 半移到 right 半（因为 charybdis 的 central 是 right 半）。

```yaml
include:
  # peripheral（left）半：BLE 分体外设，无需 snippet
  - board: nice_nano_v2
    shield: charybdis_left
  # central（right）半：走 USB Studio，需 studio-rpc-usb-uart snippet
  - board: nice_nano_v2
    shield: charybdis_right
    snippet: studio-rpc-usb-uart
    cmake-args: -DCONFIG_ZMK_STUDIO=y
  - board: nice_nano_v2
    shield: settings_reset
```

---

## 三、未改动文件（无需修改）

| 文件 | 说明 |
|---|---|
| `charybdis.dtsi` | 矩阵/kscan/encoder 节点定义不变，runtime-sensor-rotate 不改 dtsi |
| `charybdis_left.overlay` | left 半无轨迹球，RGB/encoder 定义不变 |
| `Kconfig.defconfig` / `Kconfig.shield` | 分体角色定义不变（right=central） |
| `charybdis.zmk.yml` / `charybdis.json` | shield 元数据 / 物理布局不变 |

---

## 四、Zephyr 版本差异与升级影响

| 维度 | ZMK v0.3（现状） | ZMK main+dya（目标） |
|---|---|---|
| Zephyr | 3.x（随 v0.3 内置） | 4.1.0 + cormoran 修复补丁 |
| 内核 API | 旧 input 子系统 | 新 Zephyr input 子系统 |
| 行为绑定 ID | 传统索引 | 需 `CONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS=y` |
| SPI/pinctrl | charybdis 已是 pinctrl 风格 | 无需改 |
| 主要影响 | — | ① 轨迹球驱动需换 cormoran 版（旧驱动不兼容 4.1）② 栈/缓冲区需调大 ③ keymap 新增运行时行为 |

---

## 五、功能覆盖对照（你要求的 + 额外补充的）

| 功能 | 是否加入 | 实现方式 |
|---|---|---|
| 轨迹球替换为 cormoran 驱动 | ✅ | `zmk-driver-pmw3610-with-custom-studio-rpc` + overlay 改 compatible |
| runtime macro | ✅ | `zmk-feature-runtime-macro` + `&rmacro 0` 绑定 |
| runtime combo | ✅ | `zmk-feature-runtime-combo` |
| encoder 运行时配置 | ✅ | `zmk-behavior-runtime-sensor-rotate` + `&rsr_encoder_*` 绑定 |
| 轨迹球在线调节（灵敏度/轴/CPI） | ✅ | `zmk-module-runtime-input-processor` + `ZMK_PMW3610_CUSTOM_SETTINGS` |
| 设备信息诊断 | ✅ | `zmk-feature-device-info` |
| 自定义设置 | ✅ | `zmk-feature-custom-settings` |
| BLE 连接管理 | ✅ | `zmk-module-ble-management` |
| 电池历史 | ✅ | `zmk-module-battery-history` |
| 在线改设置 | ✅ | `zmk-module-settings-rpc` |
| 轨迹球帧捕获/传感器诊断 | ✅ | 驱动自带的 `ZMK_PMW3610_STUDIO_RPC` |
| 设备身份上报（白名单） | ✅ | 内嵌 `noirix-auth` 模块 + `CONFIG_NOIRIX_AUTH=y`（见 5.1） |

---

### 5.1 设备身份上报（noirix-auth 白名单）

本包同时引入了之前为 Noirix 键盘做的**设备身份校验（UID 白名单）**功能，让 dya studio 宿主侧能识别「是否是授权的 charybdis 键盘」。

**原理**：固件通过 `noirix__auth` RPC 子系统上报 MCU 唯一 ID（`device_id`），宿主侧（dya studio）把它与白名单里的 SHA-256 哈希比对。无密钥、无 HMAC，只防第三方 fork。

**接入方式**（内嵌模块，无需独立 Git 仓库）：

- `modules/noirix-auth/`：模块本体（proto + handler + Kconfig + CMakeLists）
- `zephyr/module.yml`：config 仓库根的模块声明（`cmake: modules/noirix-auth`）
- `charybdis_right.conf`：`CONFIG_NOIRIX_AUTH=y`（只在 central 半开启）

**关键技术点**：

| 点 | 说明 |
|---|---|
| 只在 central（right）半开启 | auth 模块 Kconfig `depends on ZMK_STUDIO_RPC`；peripheral 半无 RPC 会自动关闭 |
| `select HWINFO` | Kconfig 自动带上 `CONFIG_HWINFO`（`hwinfo_get_device_id` 依赖），无需手动开 |
| 宿主侧配套 | dya studio 构建时注入 `VITE_NOIRIX_AUTH_DEVICE_IDS` 白名单哈希（宿主侧改动不在此包内） |
| device_id 明文 | 不可公开；白名单存的是 SHA-256 哈希 |

> 宿主侧（dya studio）的白名单校验逻辑与构建注入方式，见之前交付的 `dya-studio-auth-package`（独立分发包）。

---

## 六、验证与风险提示

1. **最高风险**：轨迹球驱动替换。新驱动 `compatible = "cormoran,pmw3610"` 的 binding 要求 `evt-type` / `x-input-code` / `y-input-code`（required），已在 overlay 补齐；`cpi = <400>` 对应原 `CONFIG_PMW3610_CPI=400`。
2. **Studio 解锁**：`&studio_unlock` 必须绑定，否则轨迹球 RPC 锁定后无法使用（含 Web UI）。
3. **栈溢出**：多 Custom Protocol 模块叠加易栈溢出，已按 noirix44 同款参数调大栈/缓冲区。
4. **构建验证**：建议先单独编译 right 半，确认轨迹球驱动在 Zephyr 4.1 下通过编译，再编译 left 半。

---

## 七、文件清单

> **目录结构说明**：本包采用与 `zmk-config-Noirix44`（`dya` 分支）一致的**新版结构**——shield 相关文件放在**仓库根 `boards/shields/<name>/`**（而非旧版的 `config/boards/shields/<name>/`），配合 `zephyr/module.yml` 的 `board_root: .` 让 west 从仓库根解析 shield。`config/` 目录下只保留 `west.yml`、`*.conf`、`*.keymap`、`*.json` 等 ZMK 配置。

```
charybdis-dya-package/
├── .github/workflows/build.yml                       # CI 构建（含 build + draw 两个 job）
├── build.yaml                                        # 构建矩阵（board=nice_nano//zmk + artifact-name）
├── keymap_drawer.config.yaml                         # keymap-drawer 绘图配置
├── zephyr/module.yml                                 # board_root: . + 内嵌 noirix-auth 模块声明
├── boards/                                           # ★ 新版：shield 提到仓库根
│   └── shields/charybdis/
│       ├── charybdis.dtsi                            # 不变
│       ├── charybdis.zmk.yml                         # 不变
│       ├── Kconfig.defconfig                         # 不变（right=central）
│       ├── Kconfig.shield                            # 不变
│       ├── charybdis_left.conf                       # peripheral 侧 DYA
│       ├── charybdis_left.overlay                    # 不变
│       ├── charybdis_right.conf                      # central 侧 + 轨迹球驱动 + 白名单
│       └── charybdis_right.overlay                   # 轨迹球 compatible 替换
├── modules/
│   └── noirix-auth/                                  # 设备身份上报模块（白名单）
│       ├── CMakeLists.txt
│       ├── Kconfig
│       ├── proto/zmk/auth/auth.proto + auth.options
│       └── src/studio/auth_handler.c
├── config/
│   ├── west.yml                                      # 内核升级 + 全部 DYA 模块
│   ├── charybdis.conf                                # 主配置（两侧通用 DYA 开关）
│   ├── charybdis.keymap                              # 运行时宏 + 运行时旋钮 + studio_unlock
│   └── charybdis.json                                # 不变
├── keymap-drawer/                                    # CI 自动生成（draw job），含 charybdis.svg
└── README.md                                         # 本说明
```

---

## 八、键位图

> 键位图由 keymap-drawer 在 CI 的 `draw` job 中自动生成，产物存放在 `keymap-drawer/` 目录（`charybdis.svg` 与 `charybdis.yaml`）。首次推送后，GitHub Actions 会自动提交生成结果。

<img src="keymap-drawer/charybdis.svg" >
