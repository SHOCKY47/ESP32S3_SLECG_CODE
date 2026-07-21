# 05 — 上行数据流

BLE设备通过`0xFFE2` Notify向上位机发送协议帧；UART模式使用相同帧格式，但采集期间只发送ECG_DATA。

| TYPE | 名称 | 频率 | 帧长 | 条件 |
|------|------|------|------|------|
| `0x01` | ACK | 按需 | 9 B | 命令成功 |
| `0x02` | NACK | 按需 | 9 B | 命令被拒绝 |
| `0x20` | ECG_DATA | 10 Hz | 65 B | RUNNING，每25点 |
| `0x30` | DEVICE_STATUS | 1 Hz | 19 B | BLE连接后 |
| `0x40` | IMU_DATA | 50 Hz | 27 B | 预留，当前不发送 |

## ECG_DATA

- DRDY以250 Hz产生样本。
- 每25点组成一帧，因此每100 ms发送一包。
- `seq`用于检测丢包，uint16回绕。
- `ts_ms`表示本包首样本的设备时间戳。
- `loff`表示导联脱落状态。

详见[ECG_DATA字段](../packets/ecg_data.md)。

## DEVICE_STATUS

- BLE连接后每秒发送一次。
-收到REQ_STATUS时立即额外发送。
- 包含采集、BLE、ADS就绪状态，错误码、采样率、序号、运行时间和固件版本。

详见[DEVICE_STATUS字段](../packets/device_status.md)。

## 发送优先级

```text
ECG_DATA > DEVICE_STATUS > IMU_DATA（预留）
```

ECG发送失败时丢弃当前包并计入发送统计，不阻塞后续DRDY读取；持续错误通过DEVICE_STATUS上报。

## 上位机解析

上位机使用增量缓存搜索`A5 5A`，并按传输模式限制合法TYPE和固定PAYLOAD长度。遇到伪帧头、非法长度或帧尾错误时立即继续搜索下一同步字，从而避免必须断开重连。
