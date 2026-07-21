# ECG_DATA 包（TYPE = 0x20）

设备 → 上位机，ECG 采集主数据流。

## 1. 概述

| 属性 | 值 |
|------|-----|
| TYPE | `0x20` |
| 方向 | 设备 → 上位机 |
| PAYLOAD 长度 | **58 B**（固定） |
| 帧总长度 | **65 B** |
| 发送频率 | **10 包/s**（250 Hz ÷ 25 样本/包） |
| 发送条件 | 采集状态 RUNNING |

## 2. PAYLOAD 结构

| 偏移 | 字段 | 长度 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | seq | 2 | uint16 LE | 包序号，每发一包 +1，0~65535 回绕 |
| 2 | ts_ms | 4 | uint32 LE | 本包**首样本**时间戳（设备上电后 ms） |
| 6 | n_samples | 1 | uint8 | 本包样本数，固定 **25** |
| 7 | loff | 1 | uint8 | 导联脱落状态（末样本的 `loff_status`） |
| 8 | samples | 50 | int16[25] LE | 25 个 ECG 样本 |

## 3. 字段详解

### 3.1 seq

- 从 0 开始，每发一包递增 1
- 上位机通过 seq 跳变检测丢包
- uint16 溢出后回绕至 0

### 3.2 ts_ms

- 基于 `esp_timer_get_time() / 1000` 或等效毫秒计数
- 标记本包第一个样本的采样时刻
- 相邻包 ts_ms 差值约为 100 ms

### 3.3 n_samples

- 固定为 **25**
- 预留未来可变包长；当前若 ≠ 25，上位机应告警

### 3.4 loff

- 来源：本包最后一个样本的 `ads129x_sample_t.loff_status`
- 位定义参考 ADS129x 数据手册 STATUS 字节中的导联脱落标志
- 非 0 表示可能存在导联脱落

### 3.5 samples

- 来源：`ads129x_sample_t.ch1_value`（int16）
- 驱动已将 ADS129x 24-bit 原始码符号扩展并右移 4 位
- 25 个样本按时间顺序排列，无字节序转换（已是 LE）

## 4. 完整帧布局（含信封）

| 帧偏移 | 字段 | 值/长度 |
|--------|------|---------|
| 0 | SYNC | `A5 5A` |
| 2 | TYPE | `20` |
| 3 | LEN | `3A 00`（58） |
| 5 | seq | 2 B |
| 7 | ts_ms | 4 B |
| 11 | n_samples | `19`（25） |
| 12 | loff | 1 B |
| 13 | samples[0..24] | 50 B |
| 63 | FOOT | `5A A5` |

## 5. Hex 示例

假设 seq=0, ts_ms=1000 (0x000003E8), n_samples=25, loff=0, 样本全为 0：

```
A5 5A 20 3A 00 00 00 E8 03 00 00 19 00
[50 bytes of zeros for samples]
5A A5
```

展开前几字节：

```
A5 5A 20 3A 00    ← 信封头 + LEN=58
00 00             ← seq=0
E8 03 00 00       ← ts_ms=1000
19                ← n_samples=25
00                ← loff=0
00 00 00 00 ...   ← samples (25 × int16)
5A A5             ← 帧尾
```

## 6. 上位机解析伪代码

```python
def parse_ecg_payload(payload: bytes):
    seq = int.from_bytes(payload[0:2], 'little')
    ts_ms = int.from_bytes(payload[2:6], 'little')
    n = payload[6]
    loff = payload[7]
    samples = []
    for i in range(n):
        off = 8 + i * 2
        samples.append(int.from_bytes(payload[off:off+2], 'little', signed=True))
    return seq, ts_ms, samples, loff
```

## 7. 丢包与同步

| 情况 | 处理建议 |
|------|----------|
| seq 跳号 | 标记丢包区间，插值或留空 |
| ts_ms 不连续 | 以 seq 为准重建时间轴 |
| n_samples ≠ 25 | 按实际 n 解析，记录告警 |

## 8. 数据来源

| 协议字段 | 代码来源 |
|----------|----------|
| samples | `ads129x_sample_t.ch1_value` |
| loff | `ads129x_sample_t.loff_status` |
| 采样率 | `ADS129X_SAMPLE_RATE_HZ = 500` |

## 9. 相关文档

- [../docs/02_throughput_analysis.md](../docs/02_throughput_analysis.md)
- [../docs/05_uplink_data_streams.md](../docs/05_uplink_data_streams.md)
- [../PACKET_FIELD_TABLE.md](../PACKET_FIELD_TABLE.md)
