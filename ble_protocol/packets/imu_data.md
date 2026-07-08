# IMU_DATA 包（TYPE = 0x40）

设备 → 上位机，六轴 IMU 数据包。**当前阶段预留，固件不发送。**

## 1. 概述

| 属性 | 值 |
|------|-----|
| TYPE | `0x40` |
| 方向 | 设备 → 上位机 |
| PAYLOAD 长度 | **20 B**（固定） |
| 帧总长度 | **28 B** |
| 规划频率 | **50 Hz**（启用后） |
| 传感器 | LSM6DS3TR |
| **当前状态** | **未启用，协议格式已定义** |

## 2. 设计目的

- 为后续运动伪迹分析、姿态补偿、活动识别预留通道
- 上位机解析器应识别 TYPE `0x40` 并正确解析，即使当前无数据
- 与 ECG 共用时间基准（`ts_ms`），便于离线对齐

## 3. PAYLOAD 结构

| 偏移 | 字段 | 长度 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | seq | 2 | uint16 LE | IMU 包序号 |
| 2 | ts_ms | 4 | uint32 LE | 采样时间戳（ms） |
| 6 | accel_x | 2 | int16 LE | 加速度 X |
| 8 | accel_y | 2 | int16 LE | 加速度 Y |
| 10 | accel_z | 2 | int16 LE | 加速度 Z |
| 12 | gyro_x | 2 | int16 LE | 角速度 X |
| 14 | gyro_y | 2 | int16 LE | 角速度 Y |
| 16 | gyro_z | 2 | int16 LE | 角速度 Z |
| 18 | temp_raw | 2 | int16 LE | 温度传感器 raw 值 |

## 4. 字段详解

### 4.1 seq / ts_ms

- 独立于 ECG_DATA 的 seq 计数
- ts_ms 与 ECG 使用同一时钟源

### 4.2 accel_x/y/z

- 来源：LSM6DS3TR 加速度计输出
- **单位待定**（驱动实现时确定）：
  - 方案 A：raw LSB（±2g / ±4g / ±8g / ±16g 取决于量程）
  - 方案 B：mg（毫 g）
- 建议在驱动就绪后更新本文档并注明量程与换算公式

### 4.3 gyro_x/y/z

- 来源：LSM6DS3TR 陀螺仪输出
- **单位待定**：
  - 方案 A：raw LSB（dps 取决于量程）
  - 方案 B：mdps（毫度/秒）

### 4.4 temp_raw

- LSM6DS3 内置温度传感器 raw 值
- 换算公式（datasheet）：`T(°C) = temp_raw / 256 + 25`

## 5. 规划配置（待驱动实现）

| 参数 | 建议值 |
|------|--------|
| 接口 | I2C 或 SPI（管脚待 board_pins.h 补充） |
| ODR | 52 Hz 或 104 Hz（取 50 Hz /report 近似） |
| 加速度量程 | ±4g |
| 陀螺仪量程 | ±500 dps |
| 与 ECG 关系 | 独立任务，时间戳对齐 |

## 6. 完整帧布局

| 帧偏移 | 字段 | 说明 |
|--------|------|------|
| 0 | SYNC | `A5 5A` |
| 2 | TYPE | `40` |
| 3 | LEN | `14 00`（20） |
| 5 | seq | 2 B |
| 7 | ts_ms | 4 B |
| 11 | accel_x/y/z | 6 B |
| 17 | gyro_x/y/z | 6 B |
| 23 | temp_raw | 2 B |
| 25 | FOOT | `5A A5` |

## 7. Hex 示例（占位）

seq=0, ts_ms=1000, 加速度/陀螺仪/温度均为 0：

```
A5 5A 40 14 00 00 00 E8 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 5A A5
```

## 8. START_ACQ flags 关联

`START_ACQ` 的 `flags bit0 (IMU_SYNC)` 预留给未来 IMU 与 ECG 同步启动。当前阶段忽略此位。

## 9. 启用 checklist

- [ ] 在 `board_pins.h` 添加 LSM6DS3 I2C/SPI 管脚
- [ ] 实现 LSM6DS3 驱动组件
- [ ] 创建 IMU_Task（50 Hz）
- [ ] 更新 DEVICE_STATUS `state bit3 (IMU_READY)`
- [ ] 确认 accel/gyro 单位并更新本文档
- [ ] 带宽复验（见 02_throughput_analysis.md）

## 10. 相关文档

- [../docs/05_uplink_data_streams.md](../docs/05_uplink_data_streams.md)
- [../docs/02_throughput_analysis.md](../docs/02_throughput_analysis.md)
- [../PACKET_FIELD_TABLE.md](../PACKET_FIELD_TABLE.md)
