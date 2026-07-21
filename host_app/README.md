# SLECG Host

> 面向 ESP32S3 SL-ECG 的桌面端监护与实验工具：一边保留原始波形，一边完成显示优化、自动录波、R 峰检测和心率估计。

## 功能

- UART 115200 8N1 与 BLE GATT 双传输
- 严格长度校验的增量帧解析与自动重同步
- 原始 / 优化处理后双画布，横轴联动
- 5 秒实时窗口，空格冻结但不停止接收与保存
- 停止、冻结与回放状态下拖动横轴查看历史
- `0.5–35 Hz` 零相位 Butterworth + Savitzky–Golay 显示优化
- Pan–Tompkins 思路 R 峰检测与 RR 心率估计
- 自动保存 `testdata/YYYYMMDD_HHMM.csv`
- 中文 / English 即时切换
- 黑绿深色 / 白橙浅色主题

## 安装

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Windows：

```powershell
.venv\Scripts\activate
pip install -r requirements.txt
```

## 启动

```bash
.venv/bin/python main.py
```

## 使用

### UART

选择串口设备并连接，然后使用采集设备的实体按键开始或停止。UART模式下上位机只接收数据，不远程启停。

### BLE

扫描并连接 `ESP_SLECG`。连接成功后可使用 START、STOP、STATUS，也可以继续使用设备实体按键。

### 画布

| 操作 | 结果 |
|------|------|
| 点击 Y 轴 | raw / mV 切换 |
| 空格 | 冻结 / 恢复实时显示 |
| 拖动横轴 | 冻结、停止或回放时浏览历史 |
| 打开回放 | 读取 `testdata/*.csv` |
| `EN / 中文` | 切换语言 |
| `☀ / ☾` | 切换主题 |

优化画布上的圆点是检测到的 R 峰；顶部心率取最近有效 RR 间期的中位数。该结果用于实验显示，不作为医疗诊断。

## 数据文件

采集首帧到达时自动开始记录：

```text
testdata/20260722_1430.csv
```

CSV字段：

```text
timestamp_ms,seq,sample_index,raw_int16,mv,loff
```

`testdata/`、`logs/`、`recordings/`均属于运行产物并被Git忽略。

## 测试

```bash
.venv/bin/pytest tests -q
```

测试覆盖协议粘包/拆包、伪帧头恢复、ECG载荷、完整历史缓存、CSV回读、优化滤波和合成ECG心率检测。

## 目录

```text
host_app/
├── main.py
├── requirements.txt
├── slecg_host/
│   ├── ecg/          # 缓冲、换算、记录、优化处理、R峰检测
│   ├── protocol/     # 协议常量、帧解析和载荷
│   ├── transport/    # UART与BLE
│   └── ui/           # 主窗口、双画布、主题和面板
└── tests/
```

完整固件、协议与故障排查说明见仓库根目录 [`README.md`](../README.md)。
