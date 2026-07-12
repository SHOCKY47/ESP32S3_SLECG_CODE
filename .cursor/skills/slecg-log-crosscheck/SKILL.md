---
name: slecg-log-crosscheck
description: >-
  Cross-compare ESP-IDF monitor and SLECG host_app logs to diagnose ECG/SPI/AFE issues.
  Use when the user asks for 交叉对比, 交叉比对, 比对日志, log crosscheck, or invokes this skill
  in the ESP32S3_SLECG_CODE project. In this repo it means: read all IDE terminals, diff newly
  produced logs, then analyze problems and likely root causes.
---

# SLECG 日志交叉对比

在本工程中，**交叉对比**特指：

1. 查看**所有终端**内容  
2. 比对**新产生的 logs**（固件 monitor + host_app）  
3. 分析出现的问题与可能原因  

用中文简体回复用户。

## 何时执行

用户提到以下任一即执行本流程（不必再确认）：

- 交叉对比 / 交叉比对 / 比对日志  
- log crosscheck / 这个 skill  
- 「两边日志」「固件和上位机日志」对照排查  

## 必做步骤

### 1. 枚举并阅读全部终端

- 打开 Cursor `terminals` 目录，列出全部 `*.txt`  
- **每个终端都要看**（至少读 metadata + 尾部新输出），不要只看某一个  
- 典型角色：
  - `idf_monitor` / 串口监视：固件启动、ADS 寄存器、PWDN、采集诊断  
  - `python main.py` / host：串口 RX hex、`samples[0]`、ECG 包累计  
  - `esptool` flash：确认是否刚烧录成功  

### 2. 收集新产生的文件日志

优先读**最新修改时间**的文件：

| 来源 | 路径 |
|------|------|
| Host | `host_app/logs/slecg_host_*.log` |
| 录波（若有） | `host_app/recordings/ecg_*.csv` |

相对「上一轮已知状态」，只强调**新增**片段（新连接会话、新一次采集、新一次复位启动）。

### 3. 交叉对齐时间线

按同一轮操作对齐（连接 → 启动采集 → 停采）：

| 检查点 | 固件侧关键词 | Host 侧关键词 |
|--------|--------------|---------------|
| 启动/配置 | `寄存器读回` `ID=` `CH1SET=` `CONFIG2=` `PWDN=` | 丢弃的启动文本、`ads129x`/`slecg_fsm` 片段 |
| 采集前 | `采集前` `init_start` `ADS引脚` `DRDY` | ascii 中的 `PWDN(GPIO13)=` |
| 数据 | `样本[` `status=` `raw24=` `ch1=` | `samples[0]=`、RX `hex=[a5 5a 20 ...]` |
| 停止 | `stop后读回` `DRDY活动` | 断开、累计字节 |

必要时从 host RX hex 拼出固件文本行或解析 ECG payload（`seq/ts/n/loff` + int16 LE 样本）。

### 4. 判定问题归类

先判**数据是否真全 0 / 冲顶 / 无包**，再判责任面：

1. **固件 SPI/AFE**：寄存器、PWDN、DRDY、读帧  
2. **传输**：UART 二进制被日志污染、BLE MTU 截断  
3. **上位机**：解析/Y 轴/换算（mV gain/vref）假象  
4. **硬件/电极**：供电、电极、RLD  

双方一致全 0 → 问题在设备侧或传输前；仅 UI 异常而 raw 正常 → 上位机显示/换算。

### 5. 输出格式（固定）

```markdown
## 交叉对比结论
一句话：现在是什么现象 + 责任面（固件/Host/传输/硬件）

## 证据对照表
| 来源 | 关键日志 | 含义 |

## 可能原因（按可能性）
1. ...
2. ...

## 建议下一步
- 可验证的实验或代码改动（短列表）
```

不要空泛复述；每条原因必须挂到具体日志证据。

## 本工程已知雷区（优先核对）

详见 [known-signatures.md](known-signatures.md)。排查时先扫这些签名，再开放联想。

## 约束

- 先读日志再改代码；用户只要分析时不要擅自大改  
- 用户要求修复时，改动对准根因，改完提示重新烧录/重启 host  
- 烧录与 host/monitor 争用同一串口时提醒先关闭占用方  
