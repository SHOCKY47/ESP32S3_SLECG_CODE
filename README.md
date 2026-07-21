# ESP32S3 SL-ECG

> **一颗前端，一颗主控，两条链路，一条心跳。**
> 基于 ESP32-S3 与 ADS1291 的单导联干电极心电采集系统，支持 UART / BLE 实时传输、桌面端双画布、自动录波与 R-R 心率分析。

```text
 干电极 + RLD             ESP32-S3                      PC 上位机
┌────────────┐  SPI/DRDY  ┌──────────────┐  UART / BLE  ┌────────────────┐
│  ADS1291   │ ─────────► │ 250 SPS ECG  │ ───────────► │ 原始 / 优化波形 │
│  24-bit AFE│            │ 协议与状态机  │              │ 回放 / 心率分析 │
└────────────┘            └──────────────┘              └────────────────┘
```

---

## 项目状态

当前版本已经完成从电极前端到电脑显示的完整数据链路：

| 模块 | 当前状态 |
|------|----------|
| ADS1291 SPI 初始化与寄存器校验 | ✅ ID `0x52` 与关键配置严格读回 |
| PWDN / START / DRDY 控制 | ✅ PWDN 保持高电平，DRDY 二值事件同步 |
| 单导联采样 | ✅ 250 SPS，25 点/包，约 10 包/秒 |
| 嵌入式滤波 | ✅ 0.4 Hz 高通、50 Hz 陷波、40 Hz 二阶 Butterworth 低通 |
| UART0 二进制传输 | ✅ 115200 8N1，采集阶段 ECG-only |
| BLE GATT 传输 | ✅ Notify 数据、START / STOP / STATUS 控制 |
| PC 实时显示 | ✅ 原始与优化双画布，5 秒窗口，冻结与历史拖动 |
| 自动录波与回放 | ✅ `testdata/YYYYMMDD_HHMM.csv` |
| R 峰与心率 | ✅ Pan–Tompkins 思路检测，最近有效 RR 中位数 |
| 中英文与主题 | ✅ 中文 / English，黑绿深色 / 白橙浅色 |

> 本项目用于论文实验与工程原型验证，不是经过医疗器械认证的诊断设备。

---

## 快速开始

### 1. 构建与烧录固件

环境要求：ESP-IDF v5.5.x，目标芯片 ESP32-S3。

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.wchusbserial10 -b 921600 flash
```

日常只修改应用代码时，可以只烧录应用分区：

```bash
idf.py -p /dev/cu.wchusbserial10 -b 921600 app-flash
```

如果 USB-UART 在 921600 baud 下不稳定，改用 460800。

### 2. 安装并启动上位机

```bash
cd host_app
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python main.py
```

Windows 激活虚拟环境：

```powershell
.venv\Scripts\activate
```

已经创建好虚拟环境时，一句命令启动：

```bash
cd host_app && .venv/bin/python main.py
```

---

## 设备交互

设备使用颜色表示传输模式，亮灭方式表示采集状态。

| 指示灯 | 模式 | 常亮 | 闪烁 |
|--------|------|------|------|
| 绿灯 GPIO41 | UART | 已进入 UART，未采集 | 正在采集并发送 ECG |
| 蓝灯 GPIO42 | BLE | 已进入 BLE，未采集 | 正在采集并 Notify ECG |

| 操作 | 行为 |
|------|------|
| 单击 GPIO18 | 开始 / 停止当前模式的采集 |
| 长按 3 秒 | UART ↔ BLE；采集中会先停止 |

UART 模式由设备按键启停；BLE 模式既可使用设备按键，也可由上位机发送 START / STOP。

---

## 信号处理链

### 嵌入式端

```text
ADS1291 24-bit 原始码
  → 0.4 Hz 一阶 IIR 高通
  → 50 Hz IIR 陷波
  → 40 Hz 二阶 Butterworth 低通
  → 右移压缩为 int16
  → UART / BLE
```

滤波在 [`components/ads129x/ads129x.c`](components/ads129x/ads129x.c) 的读帧路径执行，因此两种传输方式获得一致的数据。

### 上位机端

上位机保留原始回传画布，同时建立独立的显示优化链：

```text
原始 int16
  → 0.5–35 Hz 三阶 Butterworth SOS
  → 前向/反向零相位处理
  → 36 ms Savitzky–Golay 平滑
  → 优化处理后画布
```

心率检测使用 `5–18 Hz → 斜率 → 平方 → 120 ms 积分 → 自适应阈值` 的 QRS 增强流程，再回到优化波形精确定位 R 峰。心率计算为：

```text
HR = 60 / median(recent valid RR intervals)
```

有效范围限定为 30–200 BPM；不足两个可靠 R 峰时显示 `-- BPM`。

---

## 上位机使用说明

### UART

1. 选择“串口 UART”。
2. 选择 `/dev/cu.wchusbserial*` 或对应 COM 端口。
3. 点击连接。
4. 使用设备按键开始采集。

### BLE

1. 长按设备按键切换到蓝灯模式。
2. 选择“蓝牙 BLE”并扫描 `ESP_SLECG`。
3. 连接后点击“开始采集”，或使用设备按键。

### 画布与快捷键

| 操作 | 行为 |
|------|------|
| 点击 Y 轴 | raw / mV 显示切换 |
| 空格键 | 冻结 / 恢复实时窗口；冻结不停止采集和保存 |
| 冻结或停止后拖动横轴 | 查看本次会话历史 |
| 打开回放 | 载入 `testdata/*.csv` 并显示完整记录 |
| `EN / 中文` | 中英文即时切换 |
| `☀ / ☾` | 浅色与深色主题切换 |

每次收到新会话的首个 ECG 包后，上位机会自动创建：

```text
host_app/testdata/YYYYMMDD_HHMM.csv
```

同一分钟的后续会话自动增加 `_01`、`_02` 后缀。该目录属于运行数据，默认不进入 Git。

---

## 协议摘要

UART 与 BLE 共用无 CRC 的轻量帧格式：

```text
A5 5A | TYPE | LEN_L LEN_H | PAYLOAD | 5A A5
```

| TYPE | 名称 | 方向 | 说明 |
|------|------|------|------|
| `0x10` | START_ACQ | Host → Device | BLE 开始采集 |
| `0x11` | STOP_ACQ | Host → Device | BLE 停止采集 |
| `0x12` | REQ_STATUS | Host → Device | 请求设备状态 |
| `0x20` | ECG_DATA | Device → Host | 58 B payload，25 个 int16 样本 |
| `0x30` | DEVICE_STATUS | Device → Host | BLE 状态，1 Hz |
| `0x01 / 0x02` | ACK / NACK | Device → Host | 命令应答 |

完整字段见 [`ble_protocol/README.md`](ble_protocol/README.md) 和 [`ble_protocol/PACKET_FIELD_TABLE.md`](ble_protocol/PACKET_FIELD_TABLE.md)。

UART 采集开始前会完成文本日志输出并关闭全局 ESP_LOG，随后通道只发送 ECG 二进制帧；上位机解析器还会严格校验类型与固定长度，并在残留半帧时自动重新同步。

---

## 工程结构

```text
ESP32S3_SLECG_CODE/
├── main/                       # 固件入口与板级 GPIO
├── components/
│   ├── ads129x/                # ADS1291 SPI、滤波与寄存器控制
│   ├── ble_slecg/              # BLE GATT 服务
│   ├── slecg_proto/            # 通用帧与载荷协议
│   └── slecg_app/              # FSM、采样、按键、LED、传输路由
├── host_app/
│   ├── slecg_host/ecg/         # 缓冲、录制、显示优化、R峰分析
│   ├── slecg_host/protocol/    # Python流式解析器
│   ├── slecg_host/transport/   # UART / BLE传输
│   ├── slecg_host/ui/          # 双画布、主题与中英文UI
│   └── tests/                  # 主机端自动化测试
├── ble_protocol/               # GATT与帧协议文档
└── sdkconfig.defaults          # 可复现的ESP-IDF默认配置
```

### 关键管脚

| GPIO | 功能 |
|------|------|
| 3 | ADS1291 DRDY |
| 46 | ADS1291 DOUT / MISO |
| 9 | ADS1291 SCLK |
| 10 | ADS1291 DIN / MOSI |
| 11 | ADS1291 CS |
| 12 | ADS1291 START |
| 13 | ADS1291 PWDN，采集期间保持高电平 |
| 18 | 用户按键，低电平有效 |
| 41 / 42 | UART绿灯 / BLE蓝灯 |

定义见 [`main/board_pins.h`](main/board_pins.h)。

---

## 测试与故障排查

### 上位机测试

```bash
cd host_app
.venv/bin/pytest tests -q
```

### 常见问题

| 现象 | 检查项 |
|------|--------|
| 串口无法打开 | 关闭 `idf_monitor` 和其他串口程序；一个端口不能同时占用 |
| 已连接但无波形 | 确认设备已经进入采集状态；UART需按设备按键 |
| BLE START 返回错误 4 | 设备已经在采集，属于状态冲突，不是链路故障 |
| `error=1` | SPI / RDATAC状态帧异常；检查初始化寄存器和DRDY统计 |
| 导联状态非零 | 检查两个采集电极与RLD干电极接触 |
| 心率显示 `--` | 至少需要两个可靠R峰；先排除导联脱落和严重运动伪迹 |
| mV绝对值偏差 | 根据实际Vref、PGA和模拟前端重新标定 `EcgConverter` |

运行日志写入 `host_app/logs/`，测试数据写入 `host_app/testdata/`；两者均被 Git 忽略。

---

## 版本与致谢

- 主控：Espressif ESP32-S3
- 模拟前端：Texas Instruments ADS1291
- 固件：ESP-IDF v5.5.x
- 上位机：Python、PyQt6、pyqtgraph、NumPy、SciPy

本仓库服务于 GSH 单导联心电采集论文项目。开发时请保存原始数据、记录滤波参数，并明确区分“实验显示”与“医疗诊断”。

<p align="center">
  <sub>如果绿灯在闪，说明一颗心跳正在被数字化。请对数据负责，也对受试者负责。</sub>
</p>
