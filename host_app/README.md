# SLECG Host — Python 上位机

ESP32S3 SL-ECG 设备的桌面端接收与波形显示程序。支持 **串口 (UART)** 与 **蓝牙 (BLE)** 两种数据来源，解析 SLECG v1.0 协议并实时绘制 500 Hz 心电波形。

## 功能

- 串口 / 蓝牙传输方式切换，端口与设备列表刷新
- 流式帧解析（`A5 5A … 5A A5`），ECG_DATA (`0x20`) 实时曲线
- BLE 模式：START / STOP / REQ_STATUS 远程控制
- DEVICE_STATUS 状态面板（BLE）
- CSV 录制（含 raw、mV、seq、时间戳）
- **点击 Y 轴** 在 raw int16 与 mV 显示之间切换

## 环境要求

- Python 3.10+
- Windows / macOS / Linux
- BLE 功能需系统蓝牙可用（Windows 10+ 内置蓝牙栈）

## 安装

```bash
cd host_app
python -m venv .venv

# Windows
.venv\Scripts\activate

# macOS / Linux
source .venv/bin/activate

pip install -r requirements.txt
```

## 运行

```bash
python main.py
```

## 使用说明

### 串口模式（设备绿灯）

1. 确认设备处于 **UART 模式**（绿灯常亮，未采集）
2. 选择 **串口 (UART)**，刷新并选择 COM 口，点击 **连接**
3. **单击设备按键** 开始/停止采集（上位机无法远程启停）
4. 波形区实时显示；可选 **录制 CSV**

串口参数：**115200 8N1**

### 蓝牙模式（设备蓝灯）

1. 设备 **长按 3 s** 切换到 BLE 模式（蓝灯常亮）
2. 选择 **蓝牙 (BLE)**，点击 **刷新列表**，选择 `ESP_SLECG`
3. 点击 **连接**（自动开启 GATT Notify）
4. 点击 **开始采集** 或发送 START；**停止采集** 发送 STOP
5. **请求状态** 获取 DEVICE_STATUS

GATT 映射：

| UUID | 方向 | 用途 |
|------|------|------|
| `0xFFE0` | Service | SLECG 服务 |
| `0xFFE1` | Host → Device | 下行指令 Write |
| `0xFFE2` | Device → Host | 上行数据 Notify |

## 协议参考

详见仓库 [`ble_protocol/`](../ble_protocol/README.md)。

ECG 包：每帧 25 样本，20 帧/秒，500 Hz。

## mV 换算

默认：`Vref = 4.033 V`，`PGA gain = 1`，`shift=4`；数字滤波：0.2 Hz 高通 + 50 Hz 陷波（冲顶可复现联调版本）。波形 Y 轴按数据自适应。

```
mV ≈ raw × (Vref × 1000) / (gain × 32768)
```

此为近似值，精确标定需结合硬件分压与电极体系。

## 测试

```bash
cd host_app
pytest tests/ -v
```

## 目录结构

```
host_app/
├── main.py                 # 入口
├── slecg_host/
│   ├── protocol/           # 帧解析与载荷
│   ├── transport/          # 串口 / BLE
│   ├── ecg/                # 缓冲、换算、录制
│   └── ui/                 # PyQt6 界面
└── tests/
```

## 日志

启动后日志同时输出到：

- **终端 stderr**（INFO）
- **`host_app/logs/slecg_host_YYYYMMDD.log`**（DEBUG）

连接成功后状态栏会实时显示：`RX 字节数 | 帧数 | ECG包数`。

## 常见问题

| 问题 | 建议 |
|------|------|
| 一直显示「正在连接…」 | **先退出 idf_monitor / 其它串口程序**，再点连接。同一 USB 口不能双开 |
| 已连接但无波形 | 确认设备**绿灯闪烁**（采集中）。常亮=未采集，此时 UART 只有文本日志，没有 ECG 帧 |
| 串口无数据 | 确认 115200；刷新后选 `/dev/cu.wchusbserial*` 或对应 COM |
| 扫不到 BLE | 确认蓝灯常亮；靠近设备；Windows 需开启蓝牙 |
| 曲线平坦 | 检查导联连接；查看 loff 状态 |
| mV 数值偏差 | 在 `EcgConverter` 中调整 Vref/gain |

## 录制文件

CSV 默认保存至 `host_app/recordings/ecg_YYYYMMDD_HHMMSS.csv`。

列：`timestamp_ms, seq, sample_index, raw_int16, mv, loff`
