# 03 — 通用帧格式（帧信封）

所有上下行数据均封装为统一二进制帧，通过 GATT 一次 Write 或 Notify 完整发送。

## 1. 设计约束

- **有帧头、有帧尾**，用于定界与同步
- **无 CRC 校验**（应用层不做冗余校验；依赖 BLE 链路层完整性）
- **小端字节序**（Little-Endian）
- 单帧长度 ≤ **512 B**（`BLE_SLECG_TX_MAX_LEN`）

## 2. 帧结构

```
 偏移   字段      长度    值 / 说明
────────────────────────────────────────────
  0    SYNC_H     1     0xA5
  1    SYNC_L     1     0x5A
  2    TYPE       1     包类型，见 TYPE 表
  3    LEN_L      1     PAYLOAD 长度低字节
  4    LEN_H      1     PAYLOAD 长度高字节
  5    PAYLOAD    LEN   类型相关数据
5+LEN  FOOT_H     1     0x5A
6+LEN  FOOT_L     1     0xA5
────────────────────────────────────────────
总长度 = 7 + LEN 字节
```

### 2.1 字段说明

| 字段 | 说明 |
|------|------|
| **SYNC** | 固定 `A5 5A`，接收端用于帧同步 |
| **TYPE** | 区分包类型与方向 |
| **LEN** | PAYLOAD 字节数，uint16 LE；不含 SYNC/TYPE/LEN/FOOT |
| **PAYLOAD** | 业务数据，结构由 TYPE 决定 |
| **FOOT** | 固定 `5A A5`，与 SYNC 对称，便于双向扫描 |

### 2.2 最小 / 最大帧长

| 情况 | 长度 |
|------|------|
| 最小（空 PAYLOAD） | 7 B |
| 最大（LEN=504） | 511 B |
| 当前最大业务帧（ECG_DATA） | 65 B |

## 3. TYPE 分配表

| TYPE | 方向 | 名称 | PAYLOAD 长 |
|------|------|------|------------|
| `0x01` | 设备 → 上位机 | ACK | 2 |
| `0x02` | 设备 → 上位机 | NACK | 2 |
| `0x10` | 上位机 → 设备 | START_ACQ | 2 |
| `0x11` | 上位机 → 设备 | STOP_ACQ | 1 |
| `0x12` | 上位机 → 设备 | REQ_STATUS | 0 |
| `0x20` | 设备 → 上位机 | ECG_DATA | 58 |
| `0x30` | 设备 → 上位机 | DEVICE_STATUS | 12 |
| `0x40` | 设备 → 上位机 | IMU_DATA | 20 |
| `0x7F` | 双向 | 保留 | — |

## 4. 字节序

所有多字节整数均为 **Little-Endian**：

| 类型 | 示例值 | 字节序列 |
|------|--------|----------|
| uint16 | 500 | `F4 01` |
| uint32 | 1000 | `E8 03 00 00` |
| int16 | -1234 | `2E FB` |

## 5. 接收端解析流程

```
1. 在字节流中搜索 SYNC (A5 5A)
2. 读取 TYPE (offset 2)
3. 读取 LEN (offset 3-4, LE)
4. 校验 LEN ≤ 504（7+LEN ≤ 511）
5. 读取 PAYLOAD (offset 5, 长度 LEN)
6. 校验 FOOT (offset 5+LEN) == 5A A5
7. 按 TYPE 解析 PAYLOAD
8. 若 FOOT 不匹配，丢弃并回到步骤 1
```

### 5.1 粘包 / 半包

- GATT Write/Notify 每次回调应包含 **完整一帧**
- 若出现多帧粘连，按 SYNC 逐帧切分
- 半包缓存至收齐 FOOT 再校验

## 6. 发送端封装流程

```c
// 伪代码
frame[0..1] = {0xA5, 0x5A};
frame[2]    = type;
frame[3..4] = payload_len;  // LE
memcpy(&frame[5], payload, payload_len);
frame[5+len..6+len] = {0x5A, 0xA5};
ble_slecg_send_notify(frame, 8 + payload_len);
```

## 7. 示例：空 PAYLOAD 的 REQ_STATUS 帧

```
A5 5A 12 00 00 5A A5
│  │  │  │  │  │  └─ FOOT_L
│  │  │  │  │  └──── FOOT_H
│  │  │  └──┴─────── LEN = 0
│  │  └──────────── TYPE = REQ_STATUS
│  └─────────────── SYNC_L
└────────────────── SYNC_H
```

总长度 8 B。

## 8. 示例：ACK 帧

PAYLOAD: `orig_type=0x10, result=0x00`

```
A5 5A 01 02 00 10 00 5A A5
         │  │     │  │
         │  │     │  └─ result = 0 (成功)
         │  │     └──── orig_type = START_ACQ
         │  └────────── LEN = 2
         └───────────── TYPE = ACK
```

总长度 10 B。

## 9. 错误处理

| 错误 | 处理 |
|------|------|
| SYNC 未找到 | 继续扫描 |
| LEN > 504 | 丢弃，重新搜索 SYNC |
| FOOT 错误 | 丢弃该帧 |
| 未知 TYPE | 忽略（上行）或 NACK（下行指令无效） |

## 10. 与 CRC 的说明

本协议 **刻意不在应用层添加 CRC16/CRC32**。理由：

- BLE 链路层已有 CRC 保障空中传输完整性
- 简化嵌入式与上位机实现
- 帧头帧尾 + LEN 一致性检查提供基本定界能力

若未来需要在弱环境增强完整性，可在 v2.0 通过新 TYPE 或 flags 扩展，不改变 v1.0 帧格式。
