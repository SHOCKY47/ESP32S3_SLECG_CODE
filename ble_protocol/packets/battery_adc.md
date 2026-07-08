# BATTERY_ADC 包（TYPE = 0x50）

设备 → 上位机，电池电压与电量上报。

## 1. 概述

| 属性 | 值 |
|------|-----|
| TYPE | `0x50` |
| 方向 | 设备 → 上位机 |
| PAYLOAD 长度 | **5 B**（固定） |
| 帧总长度 | **13 B** |
| 发送频率 | **0.2 Hz**（每 5 s） |
| ADC 管脚 | `BOARD_BAT_ADC_GPIO`（GPIO1） |
| **当前状态** | **协议已定义，ADC 驱动待实现** |

## 2. PAYLOAD 结构

| 偏移 | 字段 | 长度 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | adc_raw | 2 | uint16 LE | ESP32 ADC 原始读数 |
| 2 | voltage_mv | 2 | uint16 LE | 换算后电池电压（mV） |
| 4 | soc_percent | 1 | uint8 | 电量百分比 0~100 |

## 3. 字段详解

### 3.1 adc_raw

- ESP32-S3 ADC oneshot 或 continuous 模式原始值
- 分辨率建议 12-bit（0~4095）
- 来源 GPIO：**GPIO1**（[`board_pins.h`](../../main/board_pins.h)）

### 3.2 voltage_mv

- 经分压网络换算后的电池端电压，单位 mV
- **占位公式**（分压系数待硬件确认）：

```
V_battery_mV = adc_raw × Vref_mV × (R1 + R2) / (R2 × ADC_max)
```

示例（假设 Vref=3300 mV, ADC 12-bit, R1=100k, R2=100k 即 1:1 分压）：

```
voltage_mv = adc_raw × 3300 × 2 / 4095
```

> 硬件确认后应更新 R1/R2 与 Vref，并在固件中固化常量。

### 3.3 soc_percent

- 基于电压-电量曲线估算的 SOC（State of Charge）
- 范围：0~100（%）
- 未校准时填 **`0xFF`**（255），表示无效/未知
- 低电量阈值：由固件定义（如 < 3.3 V 置 STATUS `state bit4 BATTERY_LOW`）

## 4. 完整帧布局

| 帧偏移 | 字段 | 说明 |
|--------|------|------|
| 0 | SYNC | `A5 5A` |
| 2 | TYPE | `50` |
| 3 | LEN | `05 00`（5） |
| 5 | adc_raw | 2 B |
| 7 | voltage_mv | 2 B |
| 9 | soc_percent | 1 B |
| 10 | FOOT | `5A A5` |

## 5. Hex 示例

adc_raw=2048, voltage_mv=3300 (0x0CE4), soc_percent=75%：

```
A5 5A 50 05 00 00 08 E4 0C 4B 5A A5
         │        │     │     │
         │        │     │     └─ soc = 75 (0x4B)
         │        │     └─────── voltage = 3300 mV
         │        └───────────── adc_raw = 2048
         └────────────────────── LEN = 5
```

未校准 SOC 示例（soc_percent=0xFF）：

```
A5 5A 50 05 00 00 08 E4 0C FF 5A A5
```

## 6. 发送条件

- BLE 连接后每 **5 s** 自动发送
- 与 ECG 采集状态无关
- 发送失败时下一周期重试

## 7. 实现 checklist

- [ ] 配置 ESP32 ADC 通道映射 GPIO1
- [ ] 确认分压电阻 R1/R2 与 Vref
- [ ] 实现 `BatteryTask`（5 s 周期）
- [ ] 定义 SOC 查表或线性估算
- [ ] 低电量时置 DEVICE_STATUS `state bit4`
- [ ] 更新本文档中的换算公式

## 8. 上位机显示建议

| soc_percent | 显示 |
|-------------|------|
| 0~100 | 电量图标 + 百分比 |
| 0xFF | 仅显示 voltage_mv，不显示百分比 |

## 9. 相关文档

- [../docs/05_uplink_data_streams.md](../docs/05_uplink_data_streams.md)
- [../packets/device_status.md](device_status.md)（BATTERY_LOW 位）
- [../PACKET_FIELD_TABLE.md](../PACKET_FIELD_TABLE.md)
