# ESP32-S3 BLE to USB HID Bridge

> 将蓝牙键盘/鼠标通过 ESP32-S3 桥接为 USB HID 设备，实现无线输入设备到 PC 的有线接入。

## 项目简介

本项目基于 ESP32-S3 开发板，实现了一个 **BLE HID Host → USB HID Device** 桥接器。蓝牙键盘和鼠标通过 BLE 连接到 ESP32-S3，ESP32-S3 再通过原生 USB OTG 接口将输入数据转发给电脑，表现为一个标准的 USB 复合 HID 设备（键盘 + 鼠标）。

```
蓝牙键盘/鼠标 ──BLE──▶ ESP32-S3 ──USB──▶ PC
                   (HID Host)    (HID Device)
```

### 核心特性

- **多设备支持**：同时连接最多 4 个 BLE HID 设备（键盘 + 鼠标混合）
- **即插即用**：USB 端无需驱动，Windows/Linux/macOS 原生识别为标准 HID 设备
- **配对持久化**：SMP Bonding 机制，配对一次后开机自动重连，无需重新配对
- **低延迟优化**：BLE 连接间隔 7.5-15ms，FreeRTOS 队列直通转发
- **自适应扫描**：根据已连接设备数动态调整扫描策略，减少 BLE radio 争用

---

## 技术栈

| 层级 | 技术 | 说明 |
|------|------|------|
| **MCU** | ESP32-S3 (QFN56) | Xtensa LX7 双核 240MHz，原生 USB OTG，BLE 5.0 |
| **固件框架** | ESP-IDF v6.0.1 | Espressif 官方 IoT 开发框架 |
| **BLE 协议栈** | Bluedroid | 选择 Bluedroid 而非 NimBLE，因 `esp_hid` 组件对 Bluedroid 兼容性更好 |
| **BLE HID Host** | esp_hid 组件 | 封装 GATTC + HID 报告解析，提供 `esp_hidh_*` API |
| **USB HID Device** | esp_tinyusb (TinyUSB) | USB HID 设备栈，配置为复合设备 |
| **RTOS** | FreeRTOS | 任务调度、队列通信 |
| **存储** | NVS Flash | 存储 BLE 配对密钥（Bonding） |
| **构建系统** | CMake + Ninja | ESP-IDF 标准构建流程 |

---

## 硬件要求

### ESP32-S3 开发板

- **芯片**：ESP32-S3-WROOM-1（或类似带原生 USB 的 S3 模组）
- **Flash**：8MB（可调整分区表适配其他容量）
- **PSRAM**：可选（当前固件未使用）
- **USB 接口**：需要两个 USB 口（或使用一拖二线缆）
  - **UART/USB 口**：用于烧录固件和串口日志（115200 baud）
  - **OTG/USB 口**：GPIO19 (D-) / GPIO20 (D+)，用于 HID 设备输出到 PC

> **注意**：ESP32-S3 的 USB Serial JTAG 和 TinyUSB 共享 GPIO19/20，必须禁用 USB Serial JTAG 以释放给 TinyUSB 使用。

### BLE 输入设备

- 支持 BLE HID 的蓝牙键盘/鼠标（**不支持** BT Classic 设备，因 ESP32-S3 仅支持 BLE）
- 测试通过的设备：
  - 小米无声鼠标 (Mi Silent Mouse) — BLE, Appearance 0x03C2
  - 联想 BK001 键盘 (LECOO BK001) — BLE, Appearance 0x03C1

---

## 项目结构

```
ESP-BLE-USB/
├── CMakeLists.txt              # 顶层 CMake 配置
├── partitions.csv              # 分区表 (NVS 24KB + factory 2MB)
├── sdkconfig.defaults          # ESP-IDF 默认配置
├── .gitignore
├── README.md
├── run_idf.py                  # ESP-IDF 环境配置 + idf.py 包装器
├── monitor_serial.py           # 串口监控脚本 (不触发 DTR/RTS 复位)
└── main/
    ├── CMakeLists.txt          # 组件 CMake 配置
    ├── idf_component.yml       # 组件依赖 (esp_tinyusb)
    ├── main.c                  # 系统入口: NVS → USB → BLE → 扫描
    ├── ble_hid_host.c          # BLE HID Host: 扫描/连接/接收报告
    ├── ble_hid_host.h          # 统一 HID 报告结构定义
    ├── usb_hid_device.c        # USB HID Device: 队列读取 → USB 转发
    ├── usb_hid_device.h
    ├── usb_descriptors.c       # USB 描述符 (设备/配置/HID Report)
    ├── usb_descriptors.h
    ├── esp_hid_gap.c           # HID GAP 层 (从 ESP-IDF 示例复制并修改)
    └── esp_hid_gap.h
```

---

## 架构设计

### 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                      ESP32-S3                            │
│                                                          │
│  ┌──────────┐     ┌───────────┐     ┌───────────────┐   │
│  │ BLE HID  │     │ FreeRTOS  │     │ USB HID       │   │
│  │ Host     │────▶│ Queue     │────▶│ Device        │   │
│  │          │     │ (32 slots)│     │ (TinyUSB)     │   │
│  └────┬─────┘     └───────────┘     └───────┬───────┘   │
│       │                                     │           │
│       │ Bluedroid Stack              USB OTG PHY       │
│       │ (GATTC + SMP)                (GPIO19/20)       │
└───────┼─────────────────────────────────────┼──────────┘
        │                                     │
   BLE Radio                              USB D+/D-
    (2.4GHz)                               │
        │                                     │
  ┌─────┴─────┐                        ┌────┴────┐
  │ BT 鼠标   │                        │   PC    │
  │ BT 键盘   │                        │ (HID)   │
  └───────────┘                        └─────────┘
```

### 任务模型

系统运行 3 个 FreeRTOS 任务：

| 任务名 | 优先级 | 栈大小 | 职责 |
|--------|--------|--------|------|
| `ble_hid_task` | 2 | 6KB | BLE 扫描、连接、断线重连 |
| `usb_hid_task` | 5 | 4KB | 从队列读取报告，通过 USB 转发 |
| `tud_task` | — | — | TinyUSB 设备栈（由 esp_tinyusb 内部创建） |

### 数据流

```
BLE 设备发送 HID 报告 (GATT Notification)
    │
    ▼
esp_hidh 回调: ESP_HIDH_INPUT_EVENT
    │
    ├─ usage == KEYBOARD → handle_keyboard_input()
    │   解析: modifier(1) + reserved(1) + keycode[6]
    │
    └─ usage == MOUSE → handle_mouse_input()
        解析: buttons(1) + x(1) + y(1) + wheel(1)
    │
    ▼
封装为 hid_report_t → xQueueSend(s_report_queue)
    │
    ▼
usb_hid_task: xQueueReceive() 阻塞等待
    │
    ├─ type == KEYBOARD → tud_hid_keyboard_report(ID=1, ...)
    └─ type == MOUSE → tud_hid_mouse_report(ID=2, ...)
    │
    ▼
PC 接收 USB HID 报告 → 系统输入事件
```

---

## 技术实现原理

### 1. BLE HID Host（蓝牙端）

**HID over GATT Profile (HOGP)**

BLE HID 设备遵循 HID over GATT Profile 规范，通过以下 GATT 服务实现：

- **HID Service (UUID 0x1812)**：包含 Report Map（报告描述符）、Report（数据通道）、Protocol Mode（协议模式）等特征值
- **Device Information Service (UUID 0x180A)**：制造商、型号等
- **Battery Service (UUID 0x180F)**：电池电量

ESP32 使用 `esp_hid` 组件封装了 GATTC 发现和 HID 报告解析流程：
1. `esp_hid_scan()` 扫描 BLE 设备，通过 HID UUID / Appearance 过滤
2. `esp_hidh_dev_open()` 发起 GATT 连接，自动发现 HID Service 和 Report 特征值
3. 订阅 Report 特征值的 Notification，接收实时 HID 输入数据
4. `ESP_HIDH_INPUT_EVENT` 回调中获取解析后的报告数据

**关键问题与修复**

Bluedroid 的 `ble_hidh.c` 存在三个需要修复的问题（修改 ESP-IDF 源码）：

| 问题 | 原因 | 修复 |
|------|------|------|
| 设备无法进入 Report Mode | Bluedroid 不主动设置 Protocol Mode 特征值 (0x2A4E) | 添加 `esp_ble_gattc_write_char()` 写入 0x01 (Report Mode) |
| Boot Mode 设备无数据 | Bluedroid 仅订阅 Report Mode 的 Notification | 移除协议模式过滤器，订阅所有 Report 特征值 |
| `write_char()` 无回调 | `WRITE_CHAR_EVT` 事件不调用 `SEND_CB()` | 添加条件回调触发 |

**BLE HID 数据格式**

> **重要**：BLE HID Notification 数据**不包含 Report ID 字节**，数据直接从报告内容开始。
> - 键盘报告：`modifier(1) + reserved(1) + keycode[6]` = 8 字节
> - 鼠标报告：`buttons(1) + x(1) + y(1) + wheel(1)` = 4 字节

这与 USB HID 不同（USB HID 报告在 Composite Device 中需要 Report ID 前缀）。

### 2. USB HID Device（USB 端）

**复合 HID 设备**

使用 TinyUSB 配置 ESP32-S3 原生 USB OTG 为单个 HID 接口的复合设备：

- **接口**：1 个 HID 接口（非 Boot 协议，使用 Report Protocol）
- **端点**：Interrupt IN, EP1, 16 字节, 1ms 轮询
- **Report ID 区分**：
  - Report ID 1：键盘报告（9 字节 = 1 ID + 8 键盘数据）
  - Report ID 2：鼠标报告（6 字节 = 1 ID + 5 鼠标数据）

**USB 描述符结构**

```
Device Descriptor (VID=0x303A, PID=0x4002, USB 2.0)
  └── Configuration Descriptor (34 bytes)
      ├── Interface Descriptor (HID, 1 endpoint)
      ├── HID Descriptor (HID 1.11, Report Protocol)
      └── Endpoint Descriptor (IN EP1, Interrupt, 16 bytes, 1ms)
```

HID Report Descriptor 使用 TinyUSB 宏生成：
```c
TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD))  // Report ID = 1
TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE))        // Report ID = 2 (5 buttons)
```

**转发流程**

```c
// 键盘
tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifier, keycode[6]);

// 鼠标 (支持 5 按钮: 左/右/中/后退/前进)
tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, x, y, wheel, 0);
```

### 3. BLE Bonding（配对持久化）

**SMP 配对流程**

```
ESP32 (Initiator)              BLE 设备 (Responder)
     │                                │
     │──── Pairing Request ──────────▶│
     │                                │
     │  Just Works (IO Cap = None)    │
     │  Auth Req = Bond               │
     │                                │
     │◀──── Pairing Response ─────────│
     │                                │
     │──── Key Exchange ─────────────▶│
     │◀──── Key Exchange ─────────────│
     │  (LTK, IRK, CSRK, ID)          │
     │                                │
     │════ Encryption Enabled ════════│
     │                                │
     │  密钥存储到 NVS Flash           │
     │  (CONFIG_BT_BLE_SMP_BOND_      │
     │   NVS_FLASH=y)                 │
```

**配对参数**

| 参数 | 值 | 说明 |
|------|-----|------|
| `auth_req` | `ESP_LE_AUTH_BOND` | 启用 Bonding，无 MITM |
| `iocap` | `ESP_IO_CAP_NONE` | 无输入输出 → Just Works 配对 |
| `init_key` | `ENC_KEY \| ID_KEY` | 请求加密密钥 + 身份密钥 |
| `rsp_key` | `ENC_KEY \| ID_KEY` | 响应加密密钥 + 身份密钥 |
| `key_size` | 16 | AES-128 |

**自动重连机制**

开机时 `try_bonded_reconnect()` 执行：
1. 读取 NVS 中的绑定设备列表 (`esp_ble_get_bond_device_list()`)
2. 逐个调用 `esp_hidh_dev_open()` 直接连接（无需扫描）
3. 若绑定设备数超过 `MAX_HID_DEVS`，清除所有旧绑定（防止无效配对累积）

### 4. 自适应扫描策略

BLE 扫描和已连接设备的通信共享同一个 radio，同时进行会导致信号争用。自适应策略：

| 已连接设备数 | 扫描时长 | 间隔等待 | 说明 |
|-------------|---------|---------|------|
| 0 | 15 秒 | 5 秒 | 激进扫描，快速发现设备 |
| 1 ~ 3 | 10 秒 | 10 秒 | 温和扫描，减少 radio 争用 |
| 4 (MAX) | 跳过 | 10 秒 | 已满，不扫描 |

---

## 开发历程

### Phase 1：基础框架搭建

- 搭建 ESP-IDF v6.0.1 项目结构
- 配置 Bluedroid BLE 协议栈（非 NimBLE，因 `esp_hid` 兼容性更好）
- 从 ESP-IDF `esp_hid_host` 示例复制 `esp_hid_gap.c/.h`（这些是示例本地文件，非组件公共头文件）
- 配置 TinyUSB USB HID Device（复合设备：键盘 + 鼠标）
- 实现 FreeRTOS 队列数据通道：BLE 回调 → 队列 → USB 转发任务

**关键发现**：
- `esp_hid_gap.h` / `esp_hid_gap.c` 是示例本地文件，不在 `esp_hid` 组件的公共头文件中
- ESP-IDF v6.0.1 API 变更：`tinyusb_driver_install()` (非 `init`)、需要 `.string_descriptor_count` 字段
- 必须禁用 USB Serial JTAG (`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n`) 释放 GPIO19/20 给 TinyUSB

### Phase 2：鼠标连接修复

**问题**：小米无声鼠标连接后 ~10 秒自动断开 (reason=19)

**排查过程**：
1. 分析断开原因码 `0x13` (PEER_USER_TERM) — 设备主动断开
2. 对比 ESP-IDF 示例和 Bluedroid 源码，发现 Bluedroid 不主动设置 Protocol Mode
3. HID 设备在未收到 Protocol Mode 设置时可能回退到 Boot Mode，而 Bluedroid 不订阅 Boot Mode 的 Notification

**修复**（修改 `ble_hidh.c`）：
1. 连接后主动写入 Protocol Mode 特征值 (0x2A4E = 0x01 Report Mode)
2. 移除协议模式过滤器，同时订阅 Report Mode 和 Boot Mode 的 Notification
3. 修复 `write_char()` 函数：`WRITE_CHAR_EVT` 不触发回调的问题

**结果**：鼠标稳定连接 180 秒以上，每 120 秒解析 4220+ 报告，无断连。

### Phase 3：数据解析修正

**问题**：鼠标按键数据异常（BTN:0xfb 而非 0x01）

**根因**：代码中错误地跳过 Report ID 字节。实际上 BLE HID Notification 数据**不包含 Report ID 字节**，数据直接从报告内容开始。

**修复**：移除 `offset = (report_id != 0) ? 1 : 0` 逻辑，直接从 `data[0]` 开始解析。

### Phase 4：延迟优化

**问题**：每条 HID 报告都输出 `ESP_LOGI` + `ESP_LOG_BUFFER_HEX`，导致严重延迟（3443+ 报告 = 10000+ 行日志/秒）。

**修复**：
- 鼠标：仅在按键状态变化时输出日志（`static uint8_t last_buttons` 对比）
- 键盘：保持正常日志（频率较低）
- 生产环境移除所有 `ESP_LOG_BUFFER_HEX` 调用

**结果**：延迟从无法使用降至无明显感知。

### Phase 5：多设备支持

**目标**：同时连接鼠标和键盘。

**改动**：
- 设备列表从单个改为数组 (`s_devs[MAX_HID_DEVS]`)，支持最多 4 个设备
- 扫描逻辑：连接数未满时继续扫描发现新设备
- 自适应扫描参数：根据已连接设备数调整扫描/等待时长

**结果**：成功同时连接小米鼠标 + LECOO BK001 键盘，输入均正常转发到 PC。

### Phase 6：配对持久化 (Bonding)

**目标**：配对一次后，ESP32 重启时自动重连，无需重新进入配对模式。

**实现**：
- 配置 SMP 安全参数：`ESP_LE_AUTH_BOND` + `ESP_IO_CAP_NONE` (Just Works)
- 启用 NVS 存储：`CONFIG_BT_BLE_SMP_BOND_NVS_FLASH=y`
- 实现 `try_bonded_reconnect()`：开机时从 NVS 读取绑定列表直接连接
- 自动清理：绑定设备数超过 `MAX_HID_DEVS` 时清除旧绑定（防止无效配对累积）

**测试结果**：
| 设备 | 配对 | 自动重连 | 备注 |
|------|------|---------|------|
| LECOO BK001 键盘 | 一次成功 | 可靠 | 地址稳定 (RANDOM, 不随重启变化) |
| 小米无声鼠标 | 一次成功 | 条件可靠 | 使用 RPA 轮换地址，鼠标断电后需重新配对 |

### Phase 7：键盘 USB 转发验证

- 添加 USB 端调试日志（`tud_hid_keyboard_report` 返回值检查）
- 确认 322+ 键盘报告成功转发
- 用户确认键盘输入在 PC 端正常工作

---

## 构建与烧录

### 环境准备

1. 安装 [ESP-IDF v6.0.1](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/)
2. 安装 Python 依赖（仓库辅助脚本及 `idf.py` 所需）：
   ```bash
   pip install -r requirements.txt
   ```
3. 设置环境变量（或使用项目提供的 `run_idf.py`）：
   ```bash
   export IDF_PATH=/path/to/esp-idf
   export IDF_TOOLS_PATH=/path/to/espressif/tools
   ```

### 构建

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build
```

或使用项目提供的包装器（需修改其中的路径为你的本地 ESP-IDF 安装路径）：
```bash
python run_idf.py build
```

### 烧录

将 ESP32-S3 的 UART/USB 口连接到电脑：
```bash
idf.py -p COM3 flash monitor
```

### 串口监控

项目提供了不触发 DTR/RTS 复位的串口监控脚本（避免 ESP32-S3 因 FTDI 复位而重启）：
```bash
python monitor_serial.py
```

---

## 使用说明

### 首次使用

1. 将 ESP32-S3 的两个 USB 口都连接到电脑
   - UART/USB 口：用于供电和串口日志
   - OTG/USB 口：用于 HID 输出（电脑会识别为新键盘 + 鼠标）
2. 将蓝牙键盘/鼠标置于配对模式
3. ESP32 会自动扫描并连接，首次连接时完成 SMP 配对
4. 配对成功后，键盘/鼠标输入即可在电脑上使用

### 日常使用

- **重启 ESP32**：已配对的设备会自动重连（无需重新进入配对模式）
  - 键盘：地址稳定，可靠自动重连
  - 鼠标：若未断电则自动重连；若断电后重新开启，可能因 RPA 地址变更需要重新配对
- **添加新设备**：将新设备置于配对模式，ESP32 会在下次扫描周期自动发现并连接
- **移除设备**：擦除 NVS (`idf.py erase-flash`) 可清除所有配对信息

### 修改最大连接数

编辑 `main/ble_hid_host.c`：
```c
#define MAX_HID_DEVS 4  // 修改为你需要的数量
```

---

## 已知问题与限制

| 问题 | 状态 | 说明 |
|------|------|------|
| 鼠标中键 USB 转发无效 | 已挂起 | BLE 数据确认正确 (buttons bit 2 = 0x04)，USB 描述符支持 5 按钮，但 PC 端无响应。根因待查。 |
| RPA 设备断电后无法自动重连 | 已知限制 | 小米鼠标使用轮换随机地址 (RPA)，断电后地址变更，NVS 中存储的地址失效。需重新配对。 |
| ESP32-S3 不支持 BT Classic | 硬件限制 | 仅支持 BLE 设备，无法连接传统蓝牙键盘/鼠标 (如 2.4G 蓝牙) |
| Bluedroid 源码修改 | 已修复 | 需修改 ESP-IDF 源码 `ble_hidh.c` (Protocol Mode + Boot Mode + write_char)，详见架构设计章节 |

---

## 配置说明

### sdkconfig.defaults 关键配置

```ini
# BLE (Bluedroid, 非 NimBLE)
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_BLE_ENABLED=y
CONFIG_BT_SMP_ENABLE=y              # SMP 配对
CONFIG_BT_BLE_SMP_ENABLE=y
CONFIG_BT_BLE_SMP_BOND_NVS_FLASH=y  # 配对密钥存储到 NVS
CONFIG_BT_GATTC_ENABLE=y            # GATT Client (HID Host 需要)

# USB - 必须禁用 USB Serial JTAG 释放 GPIO19/20
CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n
CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=n

# TinyUSB HID
CONFIG_TINYUSB_HID_COUNT=1          # 1 个 HID 接口 (复合设备)

# FreeRTOS
CONFIG_FREERTOS_HZ=1000             # 1kHz tick (1ms 精度)

# Flash
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
```

### 分区表

| 分区 | 类型 | 大小 | 说明 |
|------|------|------|------|
| nvs | data | 24KB | BLE 配对密钥存储 |
| phy_init | data | 4KB | RF 校准数据 |
| factory | app | 2MB | 应用固件 |

---

## 许可证

本项目代码遵循 MIT 许可证。`esp_hid_gap.c` 和 `esp_hid_gap.h` 来源于 ESP-IDF 示例，遵循其原始许可证 (Unlicense OR CC0-1.0)。

## 致谢

- [Espressif ESP-IDF](https://github.com/espressif/esp-idf) — 固件开发框架
- [TinyUSB](https://github.com/hathach/tinyusb) — USB 设备栈
- ESP-IDF `esp_hid_host` 示例 — HID Host 参考实现
