# SLECG 蓝牙协议 — 字段汇总表

协议版本 v1.0 | 字节序 Little-Endian | 无 CRC | 帧头 `A5 5A` | 帧尾 `5A A5`

---

## 1. 通用帧信封（所有包共用）

| 包名 | TYPE | 字段名 | 偏移(帧) | 偏移(PAYLOAD) | 长度(B) | 类型 | 字节序 | 取值/含义 |
|------|------|--------|----------|---------------|---------|------|--------|-----------|
| *ALL* | * | sync_h | 0 | — | 1 | uint8 | — | 固定 `0xA5` |
| *ALL* | * | sync_l | 1 | — | 1 | uint8 | — | 固定 `0x5A` |
| *ALL* | * | type | 2 | — | 1 | uint8 | — | 包类型，见 §2 |
| *ALL* | * | len_l | 3 | — | 1 | uint8 | LE | PAYLOAD 长度低字节 |
| *ALL* | * | len_h | 4 | — | 1 | uint8 | LE | PAYLOAD 长度高字节 |
| *ALL* | * | payload | 5 | 0 | LEN | — | — | 业务数据，结构见 §2 |
| *ALL* | * | foot_h | 5+LEN | — | 1 | uint8 | — | 固定 `0x5A` |
| *ALL* | * | foot_l | 6+LEN | — | 1 | uint8 | — | 固定 `0xA5` |

**帧总长度** = 7 + LEN 字节。

---

## 2. 按包类型字段表

### 2.1 ACK（0x01）— 设备 → 上位机

| 包名 | TYPE | 字段名 | 偏移(帧) | 偏移(PAYLOAD) | 长度(B) | 类型 | 字节序 | 取值/含义 |
|------|------|--------|----------|---------------|---------|------|--------|-----------|
| ACK | 0x01 | sync | 0 | — | 2 | — | — | `A5 5A` |
| ACK | 0x01 | type | 2 | — | 1 | uint8 | — | `0x01` |
| ACK | 0x01 | len | 3 | — | 2 | uint16 | LE | `0x0002` |
| ACK | 0x01 | orig_type | 5 | 0 | 1 | uint8 | — | 所应答指令 TYPE |
| ACK | 0x01 | result | 6 | 1 | 1 | uint8 | — | `0`=成功 |
| ACK | 0x01 | foot | 7 | — | 2 | — | — | `5A A5` |

**帧长** = 9 B | **PAYLOAD** = 2 B

---

### 2.2 NACK（0x02）— 设备 → 上位机

| 包名 | TYPE | 字段名 | 偏移(帧) | 偏移(PAYLOAD) | 长度(B) | 类型 | 字节序 | 取值/含义 |
|------|------|--------|----------|---------------|---------|------|--------|-----------|
| NACK | 0x02 | sync | 0 | — | 2 | — | — | `A5 5A` |
| NACK | 0x02 | type | 2 | — | 1 | uint8 | — | `0x02` |
| NACK | 0x02 | len | 3 | — | 2 | uint16 | LE | `0x0002` |
| NACK | 0x02 | orig_type | 5 | 0 | 1 | uint8 | — | 所拒绝指令 TYPE |
| NACK | 0x02 | error | 6 | 1 | 1 | uint8 | — | 见错误码表 §3 |
| NACK | 0x02 | foot | 7 | — | 2 | — | — | `5A A5` |

**帧长** = 9 B | **PAYLOAD** = 2 B

---

### 2.3 START_ACQ（0x10）— 上位机 → 设备

| 包名 | TYPE | 字段名 | 偏移(帧) | 偏移(PAYLOAD) | 长度(B) | 类型 | 字节序 | 取值/含义 |
|------|------|--------|----------|---------------|---------|------|--------|-----------|
| START_ACQ | 0x10 | sync | 0 | — | 2 | — | — | `A5 5A` |
| START_ACQ | 0x10 | type | 2 | — | 1 | uint8 | — | `0x10` |
| START_ACQ | 0x10 | len | 3 | — | 2 | uint16 | LE | `0x0002` |
| START_ACQ | 0x10 | mode | 5 | 0 | 1 | uint8 | — | `0x00`=正常单导 ECG |
| START_ACQ | 0x10 | flags | 6 | 1 | 1 | uint8 | — | bit0: IMU_SYNC(预留) |
| START_ACQ | 0x10 | foot | 7 | — | 2 | — | — | `5A A5` |

**帧长** = 9 B | **PAYLOAD** = 2 B

**flags 位定义：**

| Bit | 名称 | 说明 |
|-----|------|------|
| 0 | IMU_SYNC | 启用 IMU 同步（预留，当前忽略） |
| 1~7 | — | 保留，置 0 |

---

### 2.4 STOP_ACQ（0x11）— 上位机 → 设备

| 包名 | TYPE | 字段名 | 偏移(帧) | 偏移(PAYLOAD) | 长度(B) | 类型 | 字节序 | 取值/含义 |
|------|------|--------|----------|---------------|---------|------|--------|-----------|
| STOP_ACQ | 0x11 | sync | 0 | — | 2 | — | — | `A5 5A` |
| STOP_ACQ | 0x11 | type | 2 | — | 1 | uint8 | — | `0x11` |
| STOP_ACQ | 0x11 | len | 3 | — | 2 | uint16 | LE | `0x0001` |
| STOP_ACQ | 0x11 | reserved | 5 | 0 | 1 | uint8 | — | 固定 `0x00` |
| STOP_ACQ | 0x11 | foot | 6 | — | 2 | — | — | `5A A5` |

**帧长** = 8 B | **PAYLOAD** = 1 B

---

### 2.5 REQ_STATUS（0x12）— 上位机 → 设备

| 包名 | TYPE | 字段名 | 偏移(帧) | 偏移(PAYLOAD) | 长度(B) | 类型 | 字节序 | 取值/含义 |
|------|------|--------|----------|---------------|---------|------|--------|-----------|
| REQ_STATUS | 0x12 | sync | 0 | — | 2 | — | — | `A5 5A` |
| REQ_STATUS | 0x12 | type | 2 | — | 1 | uint8 | — | `0x12` |
| REQ_STATUS | 0x12 | len | 3 | — | 2 | uint16 | LE | `0x0000` |
| REQ_STATUS | 0x12 | foot | 5 | — | 2 | — | — | `5A A5` |

**帧长** = 7 B | **PAYLOAD** = 0 B

---

### 2.6 ECG_DATA（0x20）— 设备 → 上位机

| 包名 | TYPE | 字段名 | 偏移(帧) | 偏移(PAYLOAD) | 长度(B) | 类型 | 字节序 | 取值/含义 |
|------|------|--------|----------|---------------|---------|------|--------|-----------|
| ECG_DATA | 0x20 | sync | 0 | — | 2 | — | — | `A5 5A` |
| ECG_DATA | 0x20 | type | 2 | — | 1 | uint8 | — | `0x20` |
| ECG_DATA | 0x20 | len | 3 | — | 2 | uint16 | LE | `0x003A`（58） |
| ECG_DATA | 0x20 | seq | 5 | 0 | 2 | uint16 | LE | 包序号，0~65535 回绕 |
| ECG_DATA | 0x20 | ts_ms | 7 | 2 | 4 | uint32 | LE | 首样本时间戳（ms） |
| ECG_DATA | 0x20 | n_samples | 11 | 6 | 1 | uint8 | — | 固定 `25` |
| ECG_DATA | 0x20 | loff | 12 | 7 | 1 | uint8 | — | 导联脱落状态（末样本） |
| ECG_DATA | 0x20 | samples[0] | 13 | 8 | 2 | int16 | LE | 第 1 个 ECG 样本 |
| ECG_DATA | 0x20 | samples[1] | 15 | 10 | 2 | int16 | LE | 第 2 个 ECG 样本 |
| ECG_DATA | 0x20 | … | … | … | … | int16 | LE | … |
| ECG_DATA | 0x20 | samples[24] | 61 | 58 | 2 | int16 | LE | 第 25 个 ECG 样本 |
| ECG_DATA | 0x20 | foot | 63 | — | 2 | — | — | `5A A5` |

**帧长** = 65 B | **PAYLOAD** = 58 B | **频率** = 10 Hz

**samples 来源：** `ads129x_sample_t.ch1_value`（250 Hz，int16）

---

### 2.7 DEVICE_STATUS（0x30）— 设备 → 上位机

| 包名 | TYPE | 字段名 | 偏移(帧) | 偏移(PAYLOAD) | 长度(B) | 类型 | 字节序 | 取值/含义 |
|------|------|--------|----------|---------------|---------|------|--------|-----------|
| DEVICE_STATUS | 0x30 | sync | 0 | — | 2 | — | — | `A5 5A` |
| DEVICE_STATUS | 0x30 | type | 2 | — | 1 | uint8 | — | `0x30` |
| DEVICE_STATUS | 0x30 | len | 3 | — | 2 | uint16 | LE | `0x000C`（12） |
| DEVICE_STATUS | 0x30 | state | 5 | 0 | 1 | uint8 | — | 状态位掩码，见 §4 |
| DEVICE_STATUS | 0x30 | error_code | 6 | 1 | 1 | uint8 | — | 见错误码表 §3 |
| DEVICE_STATUS | 0x30 | sample_rate_hz | 7 | 2 | 2 | uint16 | LE | 固定 `500` |
| DEVICE_STATUS | 0x30 | ecg_seq | 9 | 4 | 2 | uint16 | LE | 最近 ECG 包序号 |
| DEVICE_STATUS | 0x30 | uptime_ms | 11 | 6 | 4 | uint32 | LE | 运行时间（ms） |
| DEVICE_STATUS | 0x30 | fw_version | 15 | 10 | 2 | uint16 | LE | major<<8\|minor |
| DEVICE_STATUS | 0x30 | foot | 17 | — | 2 | — | — | `5A A5` |

**帧长** = 19 B | **PAYLOAD** = 12 B | **频率** = 1 Hz

---

### 2.8 IMU_DATA（0x40）— 设备 → 上位机（预留）

| 包名 | TYPE | 字段名 | 偏移(帧) | 偏移(PAYLOAD) | 长度(B) | 类型 | 字节序 | 取值/含义 |
|------|------|--------|----------|---------------|---------|------|--------|-----------|
| IMU_DATA | 0x40 | sync | 0 | — | 2 | — | — | `A5 5A` |
| IMU_DATA | 0x40 | type | 2 | — | 1 | uint8 | — | `0x40` |
| IMU_DATA | 0x40 | len | 3 | — | 2 | uint16 | LE | `0x0014`（20） |
| IMU_DATA | 0x40 | seq | 5 | 0 | 2 | uint16 | LE | IMU 包序号 |
| IMU_DATA | 0x40 | ts_ms | 7 | 2 | 4 | uint32 | LE | 采样时间戳（ms） |
| IMU_DATA | 0x40 | accel_x | 11 | 6 | 2 | int16 | LE | 加速度 X（单位待定） |
| IMU_DATA | 0x40 | accel_y | 13 | 8 | 2 | int16 | LE | 加速度 Y |
| IMU_DATA | 0x40 | accel_z | 15 | 10 | 2 | int16 | LE | 加速度 Z |
| IMU_DATA | 0x40 | gyro_x | 17 | 12 | 2 | int16 | LE | 角速度 X（单位待定） |
| IMU_DATA | 0x40 | gyro_y | 19 | 14 | 2 | int16 | LE | 角速度 Y |
| IMU_DATA | 0x40 | gyro_z | 21 | 16 | 2 | int16 | LE | 角速度 Z |
| IMU_DATA | 0x40 | temp_raw | 23 | 18 | 2 | int16 | LE | 温度 raw（LSM6DS3） |
| IMU_DATA | 0x40 | foot | 25 | — | 2 | — | — | `5A A5` |

**帧长** = 27 B | **PAYLOAD** = 20 B | **频率** = 50 Hz（启用后）

> **当前固件不发送。** 传感器：LSM6DS3TR。

---

## 3. 错误码表（error_code / NACK error）

| 值 | 名称 | 含义 |
|----|------|------|
| `0x00` | OK | 无错误 |
| `0x01` | SPI_FAIL | SPI / ADS129x 通信失败 |
| `0x02` | DRDY_TIMEOUT | DRDY 超时 |
| `0x03` | BLE_TX_FULL | BLE 发送队列满 |
| `0x04` | STATE_CONFLICT | 状态冲突（如重复 START） |
| `0x05` | INVALID_PARAM | 参数无效 |

---

## 4. DEVICE_STATUS state 位定义

| Bit | 名称 | 1 = | 0 = |
|-----|------|-----|-----|
| 0 | ACQUIRING | 正在采集 | 未采集 |
| 1 | BLE_CONNECTED | BLE 已连接 | 未连接 |
| 2 | ADS129X_READY | ADS129x 就绪 | 未就绪 |
| 3 | IMU_READY | IMU 就绪（预留） | 未就绪 |
| 4~7 | — | 保留 | — |

---

## 5. TYPE 速查表

| TYPE | 方向 | 名称 | PAYLOAD | 帧长 | 频率 |
|------|------|------|---------|------|------|
| `0x01` | 设备→上位机 | ACK | 2 | 9 | 按需 |
| `0x02` | 设备→上位机 | NACK | 2 | 9 | 按需 |
| `0x10` | 上位机→设备 | START_ACQ | 2 | 9 | 按需 |
| `0x11` | 上位机→设备 | STOP_ACQ | 1 | 8 | 按需 |
| `0x12` | 上位机→设备 | REQ_STATUS | 0 | 7 | 按需 |
| `0x20` | 设备→上位机 | ECG_DATA | 58 | 65 | 10 Hz |
| `0x30` | 设备→上位机 | DEVICE_STATUS | 12 | 19 | 1 Hz |
| `0x40` | 设备→上位机 | IMU_DATA | 20 | 27 | 50 Hz（预留） |
| `0x7F` | 双向 | 保留 | — | — | — |

---

## 6. GATT 映射

| UUID | 属性 | 方向 | 用途 |
|------|------|------|------|
| `0xFFE0` | Service | — | SLECG 服务 |
| `0xFFE1` | Write / Write NR | 上位机→设备 | 下行指令 |
| `0xFFE2` | Notify | 设备→上位机 | 上行数据 |

---

## 7. 帧长与带宽速查

| 包 | 帧长 | × 频率 | 带宽 |
|----|------|--------|------|
| ECG_DATA | 65 B | 10/s | 650 B/s |
| DEVICE_STATUS | 19 B | 1/s | 19 B/s |
| IMU_DATA | 27 B | 50/s | 1350 B/s |
| **合计（含预留IMU）** | — | — | **≈ 2.0 KB/s** |

BLE 可用带宽（保守）：**8~18 KB/s** → 余量充足。

---

## 8. 相关文档

| 文档 | 路径 |
|------|------|
| 协议总览 | [README.md](README.md) |
| 架构 | [docs/01_architecture.md](docs/01_architecture.md) |
| 带宽分析 | [docs/02_throughput_analysis.md](docs/02_throughput_analysis.md) |
| 帧格式 | [docs/03_frame_envelope.md](docs/03_frame_envelope.md) |
| 下行指令 | [docs/04_downlink_commands.md](docs/04_downlink_commands.md) |
| 上行数据流 | [docs/05_uplink_data_streams.md](docs/05_uplink_data_streams.md) |
| ECG 包详解 | [packets/ecg_data.md](packets/ecg_data.md) |
| 状态包详解 | [packets/device_status.md](packets/device_status.md) |
| IMU 包详解 | [packets/imu_data.md](packets/imu_data.md) |
