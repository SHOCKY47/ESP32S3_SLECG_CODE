# SLECG 蓝牙传输协议

ESP32S3 单导心电（SL-ECG）设备与上位机之间的应用层二进制通信协议。

| 项目 | 值 |
|------|-----|
| 协议版本 | v1.0 |
| 文档日期 | 2026-07-09 |
| 目标平台 | ESP32S3 + ESP-IDF Bluedroid BLE |
| 设备名 | `ESP_SLECG` |
| ECG 采样率 | 500 Hz（ADS1291） |

## 设计原则

- **帧头 + 帧尾** 定界，便于流式解析
- **无 CRC 校验**（按项目要求）
- 小端字节序（Little-Endian），与 ESP32 原生一致
- 一帧 = 一次 GATT Write 或 Notify 载荷
- IMU（LSM6DS3TR）与电池 ADC 格式预留，当前固件可不发送

## GATT 映射

| UUID | 方向 | 属性 | 用途 |
|------|------|------|------|
| `0xFFE0` | — | Primary Service | SLECG 自定义服务 |
| `0xFFE1` | 上位机 → 设备 | Write / Write Without Response | 下行指令（START/STOP 等） |
| `0xFFE2` | 设备 → 上位机 | Notify（需写 CCCD） | 上行数据与应答 |

对应代码：[`components/ble_slecg/ble_slecg.h`](../components/ble_slecg/ble_slecg.h)

连接参数（现有实现）：连接间隔 20~40 ms，MTU 517，Notify 最大有效载荷约 514 B。

## 包类型一览

| TYPE | 方向 | 名称 |
|------|------|------|
| `0x01` | 设备 → 上位机 | ACK |
| `0x02` | 设备 → 上位机 | NACK |
| `0x10` | 上位机 → 设备 | START_ACQ |
| `0x11` | 上位机 → 设备 | STOP_ACQ |
| `0x12` | 上位机 → 设备 | REQ_STATUS |
| `0x20` | 设备 → 上位机 | ECG_DATA |
| `0x30` | 设备 → 上位机 | DEVICE_STATUS |
| `0x40` | 设备 → 上位机 | IMU_DATA（预留） |
| `0x50` | 设备 → 上位机 | BATTERY_ADC |
| `0x7F` | 双向 | 保留 |

## 通用帧格式

```
+------+------+------+-------+----------+------+------+
| A5   | 5A   | TYPE | LEN   | PAYLOAD  | 5A   | A5   |
| SYNC |      |  1B  | 2B LE | N bytes  | FOOT |      |
+------+------+------+-------+----------+------+------+
```

最小帧长 8 B（空 PAYLOAD）。详见 [docs/03_frame_envelope.md](docs/03_frame_envelope.md)。

## 文档阅读顺序

1. [docs/01_architecture.md](docs/01_architecture.md) — 系统架构、采集状态机、任务划分
2. [docs/02_throughput_analysis.md](docs/02_throughput_analysis.md) — 500 Hz 数据量与 BLE 带宽规划
3. [docs/03_frame_envelope.md](docs/03_frame_envelope.md) — 帧头帧尾、字节序、解析策略
4. [docs/04_downlink_commands.md](docs/04_downlink_commands.md) — 上位机指令与时序
5. [docs/05_uplink_data_streams.md](docs/05_uplink_data_streams.md) — 上行数据流与发送频率

### 分包详解

| 文档 | 包类型 |
|------|--------|
| [packets/ecg_data.md](packets/ecg_data.md) | `0x20` ECG_DATA |
| [packets/device_status.md](packets/device_status.md) | `0x30` DEVICE_STATUS |
| [packets/imu_data.md](packets/imu_data.md) | `0x40` IMU_DATA（预留） |
| [packets/battery_adc.md](packets/battery_adc.md) | `0x50` BATTERY_ADC |

### 字段汇总表

- **[PACKET_FIELD_TABLE.md](PACKET_FIELD_TABLE.md)** — 所有包、所有字段的完整表格（含帧级偏移）

## 典型交互流程

```
上位机                          设备
  |                               |
  |--- 连接 BLE，开启 Notify ---->|
  |<-- DEVICE_STATUS (1 Hz) ------|
  |<-- BATTERY_ADC (每 5 s) ------|
  |                               |
  |--- START_ACQ (0x10) --------->|
  |<-- ACK (0x01) ----------------|
  |<-- ECG_DATA (20 包/s) --------|
  |<-- DEVICE_STATUS -------------|
  |                               |
  |--- STOP_ACQ (0x11) ----------->|
  |<-- ACK (0x01) ----------------|
  |<-- ECG_DATA 停止 -------------|
```

## 与现有代码的关系

| 现有符号 | 协议用途 |
|----------|----------|
| `BLE_SLECG_CHAR_CMD_RX_UUID 0xFFE1` | 下行通道 |
| `BLE_SLECG_CHAR_CMD_TX_UUID 0xFFE2` | 上行通道 |
| `BLE_SLECG_TX_MAX_LEN 512` | 单帧最大长度约束 |
| `ADS129X_SAMPLE_RATE_HZ 500` | ECG 组包频率推导 |
| `ads129x_sample_t.ch1_value` | ECG samples 字段来源 |
| `BOARD_BAT_ADC_GPIO GPIO1` | 电池 ADC 硬件来源 |

后续实现阶段可在 `components/` 下新增 C 语言协议头文件与解析器；本文件夹仅包含协议规范文档。
