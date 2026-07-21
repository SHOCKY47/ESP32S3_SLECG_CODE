# DEVICE_STATUS 包（TYPE = 0x30）

设备 → 上位机，周期性上报设备运行状态。

## 1. 概述

| 属性 | 值 |
|------|-----|
| TYPE | `0x30` |
| 方向 | 设备 → 上位机 |
| PAYLOAD 长度 | **12 B**（固定） |
| 帧总长度 | **20 B** |
| 发送频率 | **1 Hz**（周期）+ REQ_STATUS 触发 |

## 2. PAYLOAD 结构

| 偏移 | 字段 | 长度 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | state | 1 | uint8 | 状态位掩码 |
| 1 | error_code | 1 | uint8 | 当前错误码 |
| 2 | sample_rate_hz | 2 | uint16 LE | ECG 采样率 |
| 4 | ecg_seq | 2 | uint16 LE | 最近发出的 ECG 包序号 |
| 6 | uptime_ms | 4 | uint32 LE | 设备运行时间（ms） |
| 10 | fw_version | 2 | uint16 LE | 固件版本 |

## 3. 字段详解

### 3.1 state（位掩码）

| Bit | 名称 | 1 = | 0 = |
|-----|------|-----|-----|
| 0 | ACQUIRING | 正在采集 ECG | 未采集 |
| 1 | BLE_CONNECTED | BLE 已连接 | 未连接 |
| 2 | ADS129X_READY | ADS129x 初始化成功 | 未就绪 |
| 3 | IMU_READY | LSM6DS3 就绪（预留） | 未就绪 / 未启用 |
| 4~7 | — | 保留 | — |

示例：`state = 0x07` → 采集中 + 已连接 + ADS129x 就绪。

### 3.2 error_code

| 值 | 含义 |
|----|------|
| `0x00` | 无错误 |
| `0x01` | SPI / ADS129x 通信失败 |
| `0x02` | DRDY 超时 |
| `0x03` | BLE 发送队列满 |
| `0x04` | 状态冲突 |
| `0x05` | 参数无效 |
| 其他 | 保留 |

### 3.3 sample_rate_hz

- 当前 ECG 采样率，固定 **500**
- 字节序列：`F4 01`

### 3.4 ecg_seq

- 最近一次成功 Notify 的 ECG_DATA 包 seq
- 未采集时为 0 或保持上次值

### 3.5 uptime_ms

- 自 `app_main` 启动以来的毫秒数

### 3.6 fw_version

- 编码：`major << 8 | minor`
- 示例：`0x0100` = v1.0，字节序列 `00 01`

## 4. 完整帧布局

| 帧偏移 | 字段 | 说明 |
|--------|------|------|
| 0 | SYNC | `A5 5A` |
| 2 | TYPE | `30` |
| 3 | LEN | `0C 00`（12） |
| 5 | state | 1 B |
| 6 | error_code | 1 B |
| 7 | sample_rate_hz | 2 B |
| 9 | ecg_seq | 2 B |
| 11 | uptime_ms | 4 B |
| 15 | fw_version | 2 B |
| 17 | FOOT | `5A A5` |

## 5. Hex 示例

采集中、已连接、ADS129x 就绪、无错误、250 Hz、ecg_seq=42、uptime=60000、v1.0：

```
state      = 0x07 (bit0+1+2)
error      = 0x00
rate       = 0x01F4 → F4 01
ecg_seq    = 0x002A → 2A 00
uptime     = 0x0000EA60 → 60 EA 00 00
fw_version = 0x0100 → 00 01
```

完整帧：

```
A5 5A 30 0C 00 07 00 F4 01 2A 00 60 EA 00 00 00 01 5A A5
```

## 6. 上位机使用建议

- UI 指示灯：根据 state 位显示连接/采集/导联状态
- 错误弹窗：error_code ≠ 0 时提示用户
- 版本检查：fw_version 与上位机兼容表比对

## 7. 相关文档

- [../docs/04_downlink_commands.md](../docs/04_downlink_commands.md)（REQ_STATUS）
- [../docs/05_uplink_data_streams.md](../docs/05_uplink_data_streams.md)
- [../PACKET_FIELD_TABLE.md](../PACKET_FIELD_TABLE.md)
