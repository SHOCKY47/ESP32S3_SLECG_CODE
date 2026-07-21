# SLECG 通信协议

ESP32S3 SL-ECG设备与上位机共用的应用层二进制协议。UART和BLE使用同一帧格式；BLE通过自定义GATT服务传输。

| 项目 | 当前值 |
|------|--------|
| 协议版本 | v1.0 |
| ECG采样率 | 250 Hz |
| 每包样本 | 25 |
| ECG包率 | 10包/秒 |
| ECG帧长 | 65 B |
| 字节序 | Little-Endian |
| CRC | 无；依赖链路层CRC、帧头帧尾与严格长度校验 |

## GATT映射

| UUID | 方向 | 属性 | 用途 |
|------|------|------|------|
| `0xFFE0` | — | Primary Service | SLECG服务 |
| `0xFFE1` | Host → Device | Write / Write Without Response | START、STOP、STATUS请求 |
| `0xFFE2` | Device → Host | Notify | ECG、设备状态与命令应答 |

## 包类型

| TYPE | 方向 | 名称 | PAYLOAD |
|------|------|------|---------|
| `0x01` | Device → Host | ACK | 2 B |
| `0x02` | Device → Host | NACK | 2 B |
| `0x10` | Host → Device | START_ACQ | 2 B |
| `0x11` | Host → Device | STOP_ACQ | 1 B |
| `0x12` | Host → Device | REQ_STATUS | 0 B |
| `0x20` | Device → Host | ECG_DATA | 58 B |
| `0x30` | Device → Host | DEVICE_STATUS | 12 B |
| `0x40` | Device → Host | IMU_DATA | 20 B，预留 |

## 通用帧

```text
A5 5A | TYPE:1 | LEN:2 LE | PAYLOAD:N | 5A A5
```

固定开销为7 B，空PAYLOAD的最小帧长也是7 B。协议最大帧为512 B。

## 典型BLE流程

```text
Host                                Device
  |---- Connect + enable Notify ----->|
  |<--- DEVICE_STATUS, 1 Hz -----------|
  |---- START_ACQ -------------------->|
  |<--- ACK ---------------------------|
  |<--- ECG_DATA, 10 packets/s --------|
  |---- STOP_ACQ --------------------->|
  |<--- ACK ---------------------------|
```

## 文档索引

1. [系统架构](docs/01_architecture.md)
2. [吞吐量分析](docs/02_throughput_analysis.md)
3. [通用帧格式](docs/03_frame_envelope.md)
4. [下行命令](docs/04_downlink_commands.md)
5. [上行数据流](docs/05_uplink_data_streams.md)
6. [ECG_DATA](packets/ecg_data.md)
7. [DEVICE_STATUS](packets/device_status.md)
8. [字段总表](PACKET_FIELD_TABLE.md)

协议常量实现见 [`components/slecg_proto/slecg_proto_types.h`](../components/slecg_proto/slecg_proto_types.h)，上位机镜像见 [`host_app/slecg_host/protocol/constants.py`](../host_app/slecg_host/protocol/constants.py)。
