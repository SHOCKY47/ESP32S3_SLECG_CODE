# SLECG 已知日志签名

交叉对比时优先匹配下列模式（来自实机排障经验）。

## SPI / 控制脚

| 签名 | 含义 |
|------|------|
| `ID=0x00` + 寄存器全 `00` | RREG 失败或芯片未就绪；短帧+DMA 也曾导致假全 0 |
| `ID=0x52` 后立刻 `PWDN=0` | SPI 重建设备抢走 GPIO；SPI2 默认 MISO=13/SCLK=12 与 PWDN/START 冲突 |
| 采集前 `PWDN(GPIO13)=0` + 样本全 0 | ADS `/PWDN` 低电平掉电，DOUT 无效 |
| `status=C0 xx xx` 且 raw 非 0 | RDATAC 链路基本正常 |
| `status` 全 FF / 全 00 且无变化 | 引脚/供电/SPI 主机选错或未出复位 |

## 配置 / 模拟前端

| 签名 | 含义 |
|------|------|
| `CONFIG2=0xB0`（VREF_4V）但 AVDD=3.3V | 应用 2.42V 参考；4.033V 仅适合 ~5V AVDD |
| `CONFIG2=0xA0` | 内部 2.42V 参考（3.3V 板正确） |
| `CH1SET=0x10` | PGA gain=1；`0x00` 为 gain=6 |
| 软件写了 gain=1 但读回失败 | 芯片可能仍停留电默认增益 |
| `LOFF_SENS≠0` + 大直流/易饱和 | DC 导联脱落电流推偏电极 |

## Host / 传输

| 签名 | 含义 |
|------|------|
| `samples[0]=0` 且 hex 样本区全 `00` | 设备送出真 0，不是解析错误 |
| raw 很大但「冲顶」 | 检查 Y 轴是否曾固定 ±5000（显示假冲顶） |
| 帧间隔异常 / seq 跳号很多 | 过采样洪泛或 TX 失败仍占序号（视固件版本） |
| BLE `attribute value too long, truncated to 20` | Notify MTU 截断 ECG 帧 |
| 连接了 `cu.debug-console` 收 0 字节 | 连错口；应用 `wchusbserial` |

## 对照口诀

1. **配置 OK + PWDN=0 + 数据 0** → 先修控制脚/SPI host，再谈滤波增益  
2. **PWDN=1 + 数据乱大** → Vref/增益/LOFF/电极/RLD  
3. **固件 raw 有数、Host 全 0** → 查串口争用、帧解析、是否丢弃了二进制  
4. **Host raw 有数、波形难看** → 查显示量程与 mV 换算（vref/gain/shift）  
