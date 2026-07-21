/*
 * ADS129x driver interface for ESP-IDF (ESP32S3).
 *
 * 这个头文件作为 ADS129x 系列驱动接口入口，公开 API 统一使用 ads129x_*
 * 命名，不承载应用层策略。
 *
 * 驱动边界：
 * - 负责 SPI opcode、寄存器配置、START/PWDN/RESET GPIO 和 RDATAC 数据帧读取。
 * - 只暴露 DRDY GPIO 描述符，不在驱动内部注册中断、创建信号量或启动线程。
 * - 不负责线程创建、RTT 输出、波形显示、低通滤波、基线处理或物理单位换算。
 * - 可选执行驱动内一阶高通；关闭时 raw_ch* 保留 ADS129x 原始 24-bit 字节，
 *   开启时 raw_ch* 会回写为高通后的 24-bit 二进制补码字节。
 * - 只做必要的数据整理：把通道 24-bit two's complement 原始码符号扩展为
 *   int32_t，并按固定右移量压缩成便于上位机快速处理的 int16_t。
 *
 * 为什么这样分离：
 * - DRDY 可以由 GPIO 中断、轮询、外部同步源或 DMA/专用采样线程消费，这属于板级策略。
 * - 不同项目可能有不同线程优先级、队列深度、丢帧策略和低功耗策略，不应固化在芯片驱动里。
 * - 驱动保持“可配置、可启动、可读帧、可停止”的最小能力，更容易复用到通用板子。
 *
 * 芯片型号通过本头文件中的 ADS129X_CHIP 选择。
 * 当前默认 ADS1291；若后续换成 ADS1292/ADS1292R，手动修改 ADS129X_CHIP 默认宏即可。
 */

#ifndef ADS129X_H
#define ADS129X_H

#include <stdbool.h>
#include <stdint.h>

#include "board_pins.h"
#include "driver/spi_master.h"

/*
 * ESP-IDF 平台绑定。
 *
 * 板级 GPIO 定义见 board_pins.h（main/ 目录，由组件 CMakeLists 加入 include 路径）。
 * SPI 使用 SPI2_HOST，寄存器阶段 500kHz，RDATAC 阶段 1MHz，Mode 1。
 */
#define ADS129X_SPI_HOST             SPI2_HOST
#define ADS129X_SPI_FREQ_REG         500000U
/* 数据阶段原先 8MHz，在部分板上易导致无效/全0；先降到 1MHz 保证可靠 */
#define ADS129X_SPI_FREQ_DATA        1000000U

/*
 * Chip Selection
 *
 * 用于编译期选择芯片型号：
 * - ADS129X_ADS1291: 1 channel ECG/EMG, RDATAC frame length: STATUS(3 bytes) + CH1(3 bytes) = 6 bytes.
 * - ADS129X_ADS1292: 2 channels ECG, RDATAC frame length: STATUS(3 bytes) + CH1(3 bytes) + CH2(3 bytes) = 9 bytes.
 * - ADS129X_ADS1292R: 1 channel ECG + 1 channel Respiration impedance, RDATAC frame length: STATUS(3 bytes) + CH1(3 bytes) + CH2(3 bytes) = 9 bytes.
 */

#define ADS129X_ADS1291              1
#define ADS129X_ADS1292              2
#define ADS129X_ADS1292R             3

/*
 * 工程当前 ADS129x 芯片种类和采样率。
 *
 * 芯片种类自动映射到对应的配置宏，采样率自动映射到对应的采样宏。
 * 通过包含 ads129x.h 头文件来设置上层应用的采样率。
 */

#define ADS129X_CHIP                 ADS129X_ADS1291
#define ADS129X_SAMPLE_RATE_HZ       250U

/*
 * 芯片型号判定宏，用于条件编译
 */
#define ADS129X_IS_ADS1291           (ADS129X_CHIP == ADS129X_ADS1291)
#define ADS129X_IS_ADS1292           (ADS129X_CHIP == ADS129X_ADS1292)
#define ADS129X_IS_ADS1292R          (ADS129X_CHIP == ADS129X_ADS1292R)

#if !ADS129X_IS_ADS1291 && !ADS129X_IS_ADS1292 && !ADS129X_IS_ADS1292R
#error "Unsupported ADS129X_CHIP. Use ADS129X_ADS1291, ADS129X_ADS1292 or ADS129X_ADS1292R."
#endif


/*
 * 由芯片型号派生的能力宏和帧长度。
 *
 * ADS129x RDATAC/RDATA 数据帧固定从 24-bit STATUS 开始，后面跟每个通道 24-bit 数据：
 * - ADS1291：CH1，frame = STATUS[3 bytes] + CH1[3 bytes] = 6 bytes。
 * - ADS1292：CH1 + CH2，frame = STATUS[3 bytes] + CH1[3 bytes] + CH2[3 bytes] = 9 bytes。
 * - ADS1292R：CH1 + CH2，frame = STATUS[3 bytes] + CH1[3 bytes] + CH2[3 bytes] = 9 bytes, with Respiration Impedance.
 */

#define ADS129X_HAS_CH2              (!ADS129X_IS_ADS1291)
#define ADS129X_HAS_RESPIRATION      ADS129X_IS_ADS1292R
#define ADS129X_CHANNEL_COUNT        (ADS129X_HAS_CH2 ? 2 : 1)
#define ADS129X_STATUS_SIZE          3
#define ADS129X_CHANNEL_SIZE         3
#define ADS129X_FRAME_SIZE           (ADS129X_STATUS_SIZE + (ADS129X_CHANNEL_COUNT * ADS129X_CHANNEL_SIZE))


/*
 * ADS129x 寄存器地址（RREG/WREG 使用 5-bit register address）。
 */

#define ADS129X_REG_ID             0x00    /* ID Control Register, 用于 ID 识别芯片型号 */
#define ADS129X_REG_CONFIG1        0x01    /* Configuration Register 1, 连续/单次转换与采样率 */
#define ADS129X_REG_CONFIG2        0x02    /* Configuration Register 2, 参考缓冲、参考电压、测试信号、时钟输出、lead-off comparator */
#define ADS129X_REG_LOFF           0x03    /* Lead-Off Control Register, lead-off 检测主配置 */
#define ADS129X_REG_CH1SET         0x04    /* Channel 1 Settings, CH1 power-down、PGA gain、输入 mux */
#define ADS129X_REG_CH2SET         0x05    /* Channel 2 Settings, CH2 power-down、PGA gain、输入 mux；ADS1291 需 power-down + input short */
#define ADS129X_REG_RLD_SENS       0x06    /* Right Leg Drive Sense Selection, RLD buffer 与 CH1/CH2 P/N 共模反馈选择 */
#define ADS129X_REG_LOFF_SENS      0x07    /* Lead-Off Sense Selection, 各输入端是否参与 lead-off 检测 */
#define ADS129X_REG_LOFF_STAT      0x08    /* Lead-Off Status, lead-off 状态，当前只随 STATUS 原始字节保留 */
#define ADS129X_REG_RESP1          0x09    /* Respiration Control Register 1, respiration 控制，ADS1291/ADS1292 使用非呼吸安全默认值 */
#define ADS129X_REG_RESP2          0x0A    /* Respiration Control Register 2, respiration/RLDREF 控制，ADS1292R 使用呼吸阻抗相关默认值 */
#define ADS129X_REG_GPIO           0x0B    /* GPIO Control/Data Register, GPIO 控制写入和读取数据 */


/*
 * ADS129x SPI opcode。
 *
 * 注意：ADS129x 上电后可能处于 RDATAC 连续读数据模式。数据手册说明 RDATAC
 * 模式下 RREG 命令将被忽略 或是 寄存器访问和数据流冲突，所以读写寄存器前必须先发 SDATAC。
 * RREG/WREG 命令只使用低 5 bit 地址，因此文件中会用 ADS129X_REGISTER_MASK 做地址掩码。
 * 例如：ADS129X_CMD_RREG | (ADS129X_REG_CONFIG1 & ADS129X_REGISTER_MASK)。
 */

#define ADS129X_REGISTER_MASK         0x1F

/* System Commands */
#define ADS129X_CMD_WAKEUP           0x02   /* 唤醒 ADS129x，退出待机模式 */
#define ADS129X_CMD_STANDBY          0x04   /* 将 ADS129x 进入低功耗模式(待机模式) */
#define ADS129X_CMD_RESET            0x06   /* 复位 ADS129x，将寄存器重置为默认值 */
#define ADS129X_CMD_START            0x08   /* 开始数据采集 */
#define ADS129X_CMD_STOP             0x0A   /* 停止数据采集 */
#define ADS129X_CMD_OFFSET           0x1A   /* 通道偏移校准命令，用于消除通道的偏移，发出该命令之前，必须将RESP2寄存器的 CALIB_ON 位设置为 1 ，每次PGA增益发生变化的时候，都需要发出该命令*/

/* Data Read Commands */
#define ADS129X_CMD_RDATAC           0x10   /* 进入连续读数据模式，开启数据转换 */
#define ADS129X_CMD_SDATAC           0x11   /* 退出连续读数据模式，停止数据转换 */
#define ADS129X_CMD_RDATA            0x12   /* 读取数据 */

/* Register Read/Write Commands. */
#define ADS129X_CMD_RREG             0x20   /* 读取寄存器 */
#define ADS129X_CMD_WREG             0x40   /* 写入寄存器 */


/*
 * ADS129x 支持的寄存器配置值。
 * 可选值配置值，可直接写入对应的寄存器，不决定当前芯片实际使用哪一组默认配置。
 */

/*
 * ID：ID Control Register, Read-Only，地址 0x00。
 *
 * 该寄存器在芯片出厂时写入，用于表示器件系列和具体型号。驱动只读取它做日志确认，
 * 不把它作为运行期配置项，也不会向 ID 寄存器写入任何值。
 * 
 * 0x50：ADS1191，仅做占位
 * 0x51：ADS1192，仅做占位
 * 0x52：ADS1291
 * 0x53：ADS1292
 * 0x73：ADS1292R
 * 
 * ID 寄存器位图：
 * +-----+-----+-----+---+---+---+-----+-----+
 * | bit7| bit6| bit5| 4 | 3 | 2 | bit1| bit0|
 * +-----+-----+-----+---+---+---+-----+-----+
 * |REV7 |REV6 |REV5 | 1 | 0 | 0 |REV1 |REV0 |
 * +-----+-----+-----+---+---+---+-----+-----+
 *
 * Bits[7:5] REV_ID[7:5]：
 * +-----+----------------+
 * | 010 | ADS1x9x device |
 * | 011 | ADS1292R device|
 * +-----+----------------+
 *
 * Bit 4：
 * +------+----------+
 * | bit4 | Reads 1  |
 * +------+----------+
 *
 * Bits[3:2]：
 * +-----------+----------+
 * | bits[3:2] | Reads 0  |
 * +-----------+----------+
 *
 * Bits[1:0] REV_ID[1:0]：
 * +----+--------------------+
 * | 00 | ADS1191            |
 * | 01 | ADS1192            |
 * | 10 | ADS1291            |
 * | 11 | ADS1292/ADS1292R   |
 * +----+--------------------+
 *
 * 只读，不可写入 ID 寄存器。
 */

#define ADS129X_ID_ADS1191           0x50   /* 0101 0000：ADS1191，仅做占位 */
#define ADS129X_ID_ADS1192           0x51   /* 0101 0001：ADS1192，仅做占位 */
#define ADS129X_ID_ADS1291           0x52   /* 0101 0010：ADS1291 */
#define ADS129X_ID_ADS1292           0x53   /* 0101 0011：ADS1292 */
#define ADS129X_ID_ADS1292R          0x73   /* 0111 0011：ADS1292R */

#define ADS129X_ID_FROM_CHIP(chip) \
	(((chip) == ADS129X_ADS1291) ? ADS129X_ID_ADS1291 : \
	 ((chip) == ADS129X_ADS1292) ? ADS129X_ID_ADS1292 : \
	 ADS129X_ID_ADS1292R)


/* 
 * CONFIG1：Configuration Register 1，地址 0x01，用于配置 ADC 采样率。
 * 
 * 0x00：125 SPS
 * 0x01：250 SPS
 * 0x02：500 SPS
 * 0x03：1000 SPS
 * 0x04：2000 SPS
 * 0x05：4000 SPS
 * 0x06：8000 SPS
 * 
 * CONFIG1 位图：
 * +-----------+---+---+---+---+-----+-----+-----+
 * |    bit7   | 6 | 5 | 4 | 3 | bit2| bit1| bit0|
 * +-----------+---+---+---+---+-----+-----+-----+
 * |SINGLE_SHOT| 0 | 0 | 0 | 0 | DR2 | DR1 | DR0 |
 * +-----------+---+---+---+---+-----+-----+-----+
 *
 * Bit 7 SINGLE_SHOT：Single-shot conversion
 * +---+----------------------------+
 * | 0 | 连续转换模式，默认         |
 * | 1 | 单次转换模式               |
 * +---+----------------------------+
 *
 * Bits[6:3]：
 * +----------+----------------+
 * | bits[6:3]| 必须写 0       |
 * +----------+----------------+
 *
 * Bits[2:0] DR[2:0]：Channel oversampling ratio
 * 这 3 bit 同时决定 channel 1 和 channel 2 的过采样率与输出数据率。
 * +--------+--------------------+----------+
 * | DR[2:0]| Oversampling Ratio | Data Rate|
 * +--------+--------------------+----------+
 * | 000    | fMOD / 1024        | 125 SPS  |
 * | 001    | fMOD / 512         | 250 SPS  |
 * | 010    | fMOD / 256         | 500 SPS  |
 * | 011    | fMOD / 128         | 1 kSPS   |
 * | 100    | fMOD / 64          | 2 kSPS   |
 * | 101    | fMOD / 32          | 4 kSPS   |
 * | 110    | fMOD / 16          | 8 kSPS   |
 * | 111    | Do not use         | 禁用值   |
 * +--------+--------------------+----------+
 *
 * 当前宏只定义 DR[2:0]，bit7 保持 0，即默认连续转换模式。
 */

#define ADS129X_CONFIG1_125SPS       0x00   /* 125 SPS */
#define ADS129X_CONFIG1_250SPS       0x01   /* 250 SPS */
#define ADS129X_CONFIG1_500SPS       0x02   /* 500 SPS（可选） */
#define ADS129X_CONFIG1_1000SPS      0x03   /* 1000 SPS */
#define ADS129X_CONFIG1_2000SPS      0x04   /* 2000 SPS */
#define ADS129X_CONFIG1_4000SPS      0x05   /* 4000 SPS */
#define ADS129X_CONFIG1_8000SPS      0x06   /* 8000 SPS */

#define ADS129X_CONFIG1_FROM_SAMPLE_RATE(sample_rate_hz) \
	(((sample_rate_hz) == 125U) ? ADS129X_CONFIG1_125SPS : \
	 ((sample_rate_hz) == 250U) ? ADS129X_CONFIG1_250SPS : \
	 ((sample_rate_hz) == 500U) ? ADS129X_CONFIG1_500SPS : \
	 ((sample_rate_hz) == 1000U) ? ADS129X_CONFIG1_1000SPS : \
	 ((sample_rate_hz) == 2000U) ? ADS129X_CONFIG1_2000SPS : \
	 ((sample_rate_hz) == 4000U) ? ADS129X_CONFIG1_4000SPS : \
	 ADS129X_CONFIG1_8000SPS)

#if (ADS129X_SAMPLE_RATE_HZ != 125U) && \
	(ADS129X_SAMPLE_RATE_HZ != 250U) && \
	(ADS129X_SAMPLE_RATE_HZ != 500U) && \
	(ADS129X_SAMPLE_RATE_HZ != 1000U) && \
	(ADS129X_SAMPLE_RATE_HZ != 2000U) && \
	(ADS129X_SAMPLE_RATE_HZ != 4000U) && \
	(ADS129X_SAMPLE_RATE_HZ != 8000U)
#error "Unsupported ADS129X_SAMPLE_RATE_HZ. Use 125, 250, 500, 1000, 2000, 4000 or 8000."
#endif

/*
 * CONFIG2：Configuration Register 2，地址 0x02，用于配置测试信号、时钟、参考电压和 LOFF buffer。
 * 
 * 常用可写组合值：
 * 0xA0：内部参考缓冲开启，2.42V 参考
 * 0xB0：内部参考缓冲开启，4.033V 参考
 * 0xA2：内部参考缓冲开启，2.42V 参考，内部测试信号开启，直流
 * 0xA3：内部参考缓冲开启，2.42V 参考，内部测试信号开启，1Hz 方波
 * 0xB2：内部参考缓冲开启，4.033V 参考，内部测试信号开启，直流
 * 0xB3：内部参考缓冲开启，4.033V 参考，内部测试信号开启，1Hz 方波
 * 
 * CONFIG2 位图：
 * +------+---------------+------------+---------+--------+---+----------+-----------+
 * | bit7 |     bit6      |    bit5    |  bit4   |  bit3  | 2 |   bit1   |   bit0    |
 * +------+---------------+------------+---------+--------+---+----------+-----------+
 * |  1   | PDB_LOFF_COMP | PDB_REFBUF | VREF_4V | CLK_EN | 0 | INT_TEST | TEST_FREQ |
 * +------+---------------+------------+---------+--------+---+----------+-----------+
 *
 * Bit 7：
 * +---+----------------+
 * | 1 | 必须写 1       |
 * +---+----------------+
 *
 * Bit 6 PDB_LOFF_COMP：导联脱落比较器电源控制
 * +---+----------------------------+
 * | 0 | 导联脱落比较器关闭，默认   |
 * | 1 | 导联脱落比较器开启         |
 * +---+----------------------------+
 *
 * Bit 5 PDB_REFBUF：内部参考缓冲电源控制
 * +---+----------------------------+
 * | 0 | 内部参考缓冲关闭，默认     |
 * | 1 | 内部参考缓冲开启           |
 * +---+----------------------------+
 *
 * Bit 4 VREF_4V：参考电压选择
 * +---+----------------------------+
 * | 0 | 2.42V 参考，默认           |
 * | 1 | 4.033V 参考                |
 * +---+----------------------------+
 *
 * Bit 3 CLK_EN：内部振荡器是否输出到 CLK 引脚
 * +---+----------------------------+
 * | 0 | 时钟输出关闭，默认         |
 * | 1 | 时钟输出开启               |
 * +---+----------------------------+
 *
 * Bit 2：
 * +---+----------------+
 * | 0 | 必须写 0       |
 * +---+----------------+
 *
 * Bit 1 INT_TEST：内部测试信号
 * +---+--------------------------------------------+
 * | 0 | 关闭，默认                                 |
 * | 1 | 开启，幅值 = +/- (VREFP - VREFN) / 2400    |
 * +---+--------------------------------------------+
 *
 * Bit 0 TEST_FREQ：测试信号频率
 * +---+----------------------------+
 * | 0 | 直流，默认                 |
 * | 1 | 1 Hz 方波                  |
 * +---+----------------------------+
 */

#define ADS129X_CONFIG2_FIXED_1                          0x80 /* bit7 必须写 1 */

#define ADS129X_CONFIG2_PDB_LOFF_COMP_DISABLED           0x00 /* bit6=0：导联脱落比较器关闭，默认 */
#define ADS129X_CONFIG2_PDB_LOFF_COMP_ENABLED            0x40 /* bit6=1：导联脱落比较器开启 */

#define ADS129X_CONFIG2_PDB_REFBUF_DISABLED              0x00 /* bit5=0：内部参考缓冲关闭，默认 */
#define ADS129X_CONFIG2_PDB_REFBUF_ENABLED               0x20 /* bit5=1：内部参考缓冲开启 */

#define ADS129X_CONFIG2_VREF_2V4                         0x00 /* bit4=0：2.42V 参考 */
#define ADS129X_CONFIG2_VREF_4V                          0x10 /* bit4=1：4.033V 参考 */

#define ADS129X_CONFIG2_CLK_DISABLED                     0x00 /* bit3=0：时钟输出关闭，默认 */
#define ADS129X_CONFIG2_CLK_ENABLED                      0x08 /* bit3=1：时钟输出开启 */

#define ADS129X_CONFIG2_INT_TEST_DISABLED                0x00 /* bit1=0：内部测试信号关闭，默认 */
#define ADS129X_CONFIG2_INT_TEST_ENABLED                 0x02 /* bit1=1：内部测试信号开启 */

#define ADS129X_CONFIG2_TEST_FREQ_DC                     0x00 /* bit0=0：直流，默认 */
#define ADS129X_CONFIG2_TEST_FREQ_1HZ                    0x01 /* bit0=1：1Hz 方波 */

#define ADS129X_CONFIG2_INTERNAL_REF_2V4                 (ADS129X_CONFIG2_FIXED_1 | ADS129X_CONFIG2_PDB_REFBUF_ENABLED | ADS129X_CONFIG2_VREF_2V4) /* 0xA0：内部参考缓冲开启，2.42V 参考 */
#define ADS129X_CONFIG2_INTERNAL_REF_4V                  (ADS129X_CONFIG2_FIXED_1 | ADS129X_CONFIG2_PDB_REFBUF_ENABLED | ADS129X_CONFIG2_VREF_4V) /* 0xB0：内部参考缓冲开启，4.033V 参考 */
#define ADS129X_CONFIG2_TEST_SIGNAL_2V4_DC               (ADS129X_CONFIG2_INTERNAL_REF_2V4 | ADS129X_CONFIG2_INT_TEST_ENABLED | ADS129X_CONFIG2_TEST_FREQ_DC) /* 0xA2：2.42V 参考，内部测试信号开启，直流 */
#define ADS129X_CONFIG2_TEST_SIGNAL_2V4_1HZ              (ADS129X_CONFIG2_INTERNAL_REF_2V4 | ADS129X_CONFIG2_INT_TEST_ENABLED | ADS129X_CONFIG2_TEST_FREQ_1HZ) /* 0xA3：2.42V 参考，内部测试信号开启，1Hz 方波 */
#define ADS129X_CONFIG2_TEST_SIGNAL_4V_DC                (ADS129X_CONFIG2_INTERNAL_REF_4V | ADS129X_CONFIG2_INT_TEST_ENABLED | ADS129X_CONFIG2_TEST_FREQ_DC) /* 0xB2：4.033V 参考，内部测试信号开启，直流 */
#define ADS129X_CONFIG2_TEST_SIGNAL_4V_1HZ               (ADS129X_CONFIG2_INTERNAL_REF_4V | ADS129X_CONFIG2_INT_TEST_ENABLED | ADS129X_CONFIG2_TEST_FREQ_1HZ) /* 0xB3：4.033V 参考，内部测试信号开启，1Hz 方波 */


/*
 * LOFF：Lead-Off Control Register，地址 0x03，用于配置导联脱落检测。
 *
 * 默认导联脱落配置：0x10
 *
 * LOFF 位图：
 * +----------+----------+----------+---+------------+------------+---+-----------+
 * |   bit7   |   bit6   |   bit5   | 4 |    bit3    |    bit2    | 1 |   bit0    |
 * +----------+----------+----------+---+------------+------------+---+-----------+
 * | COMP_TH2 | COMP_TH1 | COMP_TH0 | 1 | ILEAD_OFF1 | ILEAD_OFF0 | 0 | FLEAD_OFF |
 * +----------+----------+----------+---+------------+------------+---+-----------+
 *
 * Bits[7:5] COMP_TH[2:0]：导联脱落比较器阈值
 * +--------+----------------+----------------+
 * | COMP_TH| 正端比较器阈值 | 负端比较器阈值 |
 * +--------+----------------+----------------+
 * | 000    | 95%，默认      | 5%，默认       |
 * | 001    | 92.5%          | 7.5%           |
 * | 010    | 90%            | 10%            |
 * | 011    | 87.5%          | 12.5%          |
 * | 100    | 85%            | 15%            |
 * | 101    | 80%            | 20%            |
 * | 110    | 75%            | 25%            |
 * | 111    | 70%            | 30%            |
 * +--------+----------------+----------------+
 *
 * Bit 4：
 * +---+----------------+
 * | 1 | 必须写 1       |
 * +---+----------------+
 *
 * Bits[3:2] ILEAD_OFF[1:0]：导联脱落检测电流大小
 * +-----------+----------+
 * | ILEAD_OFF | 电流大小 |
 * +-----------+----------+
 * | 00        | 6 nA，默认 |
 * | 01        | 22 nA      |
 * | 10        | 6 uA       |
 * | 11        | 22 uA      |
 * +-----------+----------+
 *
 * Bit 1：
 * +---+----------------+
 * | 0 | 必须写 0       |
 * +---+----------------+
 *
 * Bit 0 FLEAD_OFF：导联脱落检测频率
 * +---+----------------------------------------+
 * | 0 | 直流导联脱落检测，默认                 |
 * | 1 | 交流导联脱落检测，频率为 fDR / 4       |
 * +---+----------------------------------------+
 */

#define ADS129X_LOFF_FIXED_1                         0x10 /* bit4 必须写 1，bit1 必须写 0 */

#define ADS129X_LOFF_COMP_TH_95_5                    0x00 /* bits[7:5]=000：正端 95%，负端 5%，默认 */
#define ADS129X_LOFF_COMP_TH_92_5_7_5                0x20 /* bits[7:5]=001：正端 92.5%，负端 7.5% */
#define ADS129X_LOFF_COMP_TH_90_10                   0x40 /* bits[7:5]=010：正端 90%，负端 10% */
#define ADS129X_LOFF_COMP_TH_87_5_12_5               0x60 /* bits[7:5]=011：正端 87.5%，负端 12.5% */
#define ADS129X_LOFF_COMP_TH_85_15                   0x80 /* bits[7:5]=100：正端 85%，负端 15% */
#define ADS129X_LOFF_COMP_TH_80_20                   0xA0 /* bits[7:5]=101：正端 80%，负端 20% */
#define ADS129X_LOFF_COMP_TH_75_25                   0xC0 /* bits[7:5]=110：正端 75%，负端 25% */
#define ADS129X_LOFF_COMP_TH_70_30                   0xE0 /* bits[7:5]=111：正端 70%，负端 30% */

#define ADS129X_LOFF_CURRENT_6NA                     0x00 /* bits[3:2]=00：6nA，默认 */
#define ADS129X_LOFF_CURRENT_22NA                    0x04 /* bits[3:2]=01：22nA */
#define ADS129X_LOFF_CURRENT_6UA                     0x08 /* bits[3:2]=10：6uA */
#define ADS129X_LOFF_CURRENT_22UA                    0x0C /* bits[3:2]=11：22uA */

#define ADS129X_LOFF_FREQ_DC                         0x00 /* bit0=0：直流导联脱落检测，默认 */
#define ADS129X_LOFF_FREQ_AC                         0x01 /* bit0=1：交流导联脱落检测，频率为 fDR/4 */

#define ADS129X_LOFF_CONFIG(threshold, current, freq) (ADS129X_LOFF_FIXED_1 | (threshold) | (current) | (freq))

#define ADS129X_LOFF_DC(threshold, current)           ADS129X_LOFF_CONFIG((threshold), (current), ADS129X_LOFF_FREQ_DC)
#define ADS129X_LOFF_AC(threshold, current)           ADS129X_LOFF_CONFIG((threshold), (current), ADS129X_LOFF_FREQ_AC)

/*
 * CH1SET：Channel 1 Settings，地址 0x04，用于配置 CH1 电源模式、PGA 增益和输入多路复用。
 *
 * CH1SET 位图：
 * +------+------+------+------+------+------+------+------+
 * | bit7 | bit6 | bit5 | bit4 | bit3 | bit2 | bit1 | bit0 |
 * +------+------+------+------+------+------+------+------+
 * | PD1  |GAIN1_2|GAIN1_1|GAIN1_0|MUX1_3|MUX1_2|MUX1_1|MUX1_0|
 * +------+------+------+------+------+------+------+------+
 *
 * Bit 7 PD1：Channel 1 power-down
 * +---+----------------------------+
 * | 0 | CH1 正常工作，默认         |
 * | 1 | CH1 掉电                   |
 * +---+----------------------------+
 *
 * Bits[6:4] GAIN1[2:0]：CH1 PGA 增益
 * +---------+----------+
 * | GAIN1   | PGA 增益 |
 * +---------+----------+
 * | 000     | 6，默认  |
 * | 001     | 1        |
 * | 010     | 2        |
 * | 011     | 3        |
 * | 100     | 4        |
 * | 101     | 8        |
 * | 110     | 12       |
 * +---------+----------+
 *
 * Bits[3:0] MUX1[3:0]：CH1 输入选择
 * +---------+---------------------------------------------+
 * | MUX1    | 输入选择                                    |
 * +---------+---------------------------------------------+
 * | 0000    | 正常电极输入，默认                          |
 * | 0001    | 输入短路，用于 offset 测量                   |
 * | 0010    | RLD_MEASURE                                 |
 * | 0011    | MVDD，电源测量，CH1 为 0.5(AVDD + AVSS)     |
 * | 0100    | 温度传感器                                  |
 * | 0101    | 测试信号                                    |
 * | 0110    | RLD_DRP，正输入连接到 RLDIN                 |
 * | 0111    | RLD_DRM，负输入连接到 RLDIN                 |
 * | 1000    | RLD_DRPM，正负输入都连接到 RLDIN            |
 * | 1001    | IN3P 和 IN3N 接入 CH1 输入                  |
 * | 1010    | 保留                                        |
 * +---------+---------------------------------------------+
 *
 * 注意：
 * - CH1 掉电时，必须把 MUX1[3:0] 设置为 0001，即输入短路。
 * - 用 MVDD 测量电源时，为避免 PGA 饱和，增益必须设置为 1。
 */

#define ADS129X_CH1SET_POWER_ON                      0x00 /* bit7=0：CH1 正常工作，默认 */
#define ADS129X_CH1SET_POWER_DOWN                    0x80 /* bit7=1：CH1 掉电 */

#define ADS129X_CH1SET_GAIN_6                        0x00 /* bits[6:4]=000：CH1 PGA gain = 6，默认 */
#define ADS129X_CH1SET_GAIN_1                        0x10 /* bits[6:4]=001：CH1 PGA gain = 1 */
#define ADS129X_CH1SET_GAIN_2                        0x20 /* bits[6:4]=010：CH1 PGA gain = 2 */
#define ADS129X_CH1SET_GAIN_3                        0x30 /* bits[6:4]=011：CH1 PGA gain = 3 */
#define ADS129X_CH1SET_GAIN_4                        0x40 /* bits[6:4]=100：CH1 PGA gain = 4 */
#define ADS129X_CH1SET_GAIN_8                        0x50 /* bits[6:4]=101：CH1 PGA gain = 8 */
#define ADS129X_CH1SET_GAIN_12                       0x60 /* bits[6:4]=110：CH1 PGA gain = 12 */

#define ADS129X_CH1SET_MUX_NORMAL                    0x00 /* bits[3:0]=0000：CH1 正常电极输入，默认 */
#define ADS129X_CH1SET_MUX_SHORTED                   0x01 /* bits[3:0]=0001：CH1 输入短路，用于 offset 测量 */
#define ADS129X_CH1SET_MUX_RLD_MEASURE               0x02 /* bits[3:0]=0010：CH1 RLD_MEASURE */
#define ADS129X_CH1SET_MUX_MVDD                      0x03 /* bits[3:0]=0011：CH1 MVDD 电源测量 */
#define ADS129X_CH1SET_MUX_TEMPERATURE               0x04 /* bits[3:0]=0100：CH1 温度传感器 */
#define ADS129X_CH1SET_MUX_TEST_SIGNAL               0x05 /* bits[3:0]=0101：CH1 测试信号 */
#define ADS129X_CH1SET_MUX_RLD_DRP                   0x06 /* bits[3:0]=0110：CH1 正输入连接到 RLDIN */
#define ADS129X_CH1SET_MUX_RLD_DRM                   0x07 /* bits[3:0]=0111：CH1 负输入连接到 RLDIN */
#define ADS129X_CH1SET_MUX_RLD_DRPM                  0x08 /* bits[3:0]=1000：CH1 正负输入都连接到 RLDIN */
#define ADS129X_CH1SET_MUX_IN3                       0x09 /* bits[3:0]=1001：IN3P 和 IN3N 接入 CH1 输入 */
#define ADS129X_CH1SET_MUX_RESERVED                  0x0A /* bits[3:0]=1010：保留 */

#define ADS129X_CH1SET_NORMAL(gain)                  (ADS129X_CH1SET_POWER_ON | (gain) | ADS129X_CH1SET_MUX_NORMAL)
#define ADS129X_CH1SET_TEST_SIGNAL(gain)             (ADS129X_CH1SET_POWER_ON | (gain) | ADS129X_CH1SET_MUX_TEST_SIGNAL)
#define ADS129X_CH1SET_NOISE_MEASURE(gain)           (ADS129X_CH1SET_POWER_ON | (gain) | ADS129X_CH1SET_MUX_SHORTED)
#define ADS129X_CH1SET_POWERDOWN_SHORT               (ADS129X_CH1SET_POWER_DOWN | ADS129X_CH1SET_MUX_SHORTED)


/*
 * CH2SET：Channel 2 Settings，地址 0x05，用于配置 CH2 电源模式、PGA 增益和输入多路复用。
 *
 * CH2SET 位图：
 * +------+------+------+------+------+------+------+------+
 * | bit7 | bit6 | bit5 | bit4 | bit3 | bit2 | bit1 | bit0 |
 * +------+------+------+------+------+------+------+------+
 * | PD2  |GAIN2_2|GAIN2_1|GAIN2_0|MUX2_3|MUX2_2|MUX2_1|MUX2_0|
 * +------+------+------+------+------+------+------+------+
 *
 * Bit 7 PD2：Channel 2 power-down
 * +---+----------------------------+
 * | 0 | CH2 正常工作，默认         |
 * | 1 | CH2 掉电                   |
 * +---+----------------------------+
 *
 * Bits[6:4] GAIN2[2:0]：CH2 PGA 增益
 * +---------+----------+
 * | GAIN2   | PGA 增益 |
 * +---------+----------+
 * | 000     | 6，默认  |
 * | 001     | 1        |
 * | 010     | 2        |
 * | 011     | 3        |
 * | 100     | 4        |
 * | 101     | 8        |
 * | 110     | 12       |
 * +---------+----------+
 *
 * Bits[3:0] MUX2[3:0]：CH2 输入选择
 * +---------+---------------------------------------------+
 * | MUX2    | 输入选择                                    |
 * +---------+---------------------------------------------+
 * | 0000    | 正常电极输入，默认                          |
 * | 0001    | 输入短路，用于 offset 测量                   |
 * | 0010    | RLD_MEASURE                                 |
 * | 0011    | VDD / 2，电源测量                            |
 * | 0100    | 温度传感器                                  |
 * | 0101    | 测试信号                                    |
 * | 0110    | RLD_DRP，正输入连接到 RLDIN                 |
 * | 0111    | RLD_DRM，负输入连接到 RLDIN                 |
 * | 1000    | RLD_DRPM，正负输入都连接到 RLDIN            |
 * | 1001    | IN3P 和 IN3N 接入 CH2 输入                  |
 * | 1010    | 保留                                        |
 * +---------+---------------------------------------------+
 *
 * 注意：
 * - CH2 掉电时，必须把 MUX2[3:0] 设置为 0001，即输入短路。
 * - ADS1291 没有 CH2，CH2SET 必须使用 ADS129X_CH2SET_POWERDOWN_SHORT。
 */

#define ADS129X_CH2SET_POWER_ON                      0x00 /* bit7=0：CH2 正常工作，默认 */
#define ADS129X_CH2SET_POWER_DOWN                    0x80 /* bit7=1：CH2 掉电 */

#define ADS129X_CH2SET_GAIN_6                        0x00 /* bits[6:4]=000：CH2 PGA gain = 6，默认 */
#define ADS129X_CH2SET_GAIN_1                        0x10 /* bits[6:4]=001：CH2 PGA gain = 1 */
#define ADS129X_CH2SET_GAIN_2                        0x20 /* bits[6:4]=010：CH2 PGA gain = 2 */
#define ADS129X_CH2SET_GAIN_3                        0x30 /* bits[6:4]=011：CH2 PGA gain = 3 */
#define ADS129X_CH2SET_GAIN_4                        0x40 /* bits[6:4]=100：CH2 PGA gain = 4 */
#define ADS129X_CH2SET_GAIN_8                        0x50 /* bits[6:4]=101：CH2 PGA gain = 8 */
#define ADS129X_CH2SET_GAIN_12                       0x60 /* bits[6:4]=110：CH2 PGA gain = 12 */

#define ADS129X_CH2SET_MUX_NORMAL                    0x00 /* bits[3:0]=0000：CH2 正常电极输入，默认 */
#define ADS129X_CH2SET_MUX_SHORTED                   0x01 /* bits[3:0]=0001：CH2 输入短路，用于 offset 测量 */
#define ADS129X_CH2SET_MUX_RLD_MEASURE               0x02 /* bits[3:0]=0010：CH2 RLD_MEASURE */
#define ADS129X_CH2SET_MUX_MVDD                      0x03 /* bits[3:0]=0011：CH2 VDD/2 电源测量 */
#define ADS129X_CH2SET_MUX_TEMPERATURE               0x04 /* bits[3:0]=0100：CH2 温度传感器 */
#define ADS129X_CH2SET_MUX_TEST_SIGNAL               0x05 /* bits[3:0]=0101：CH2 测试信号 */
#define ADS129X_CH2SET_MUX_RLD_DRP                   0x06 /* bits[3:0]=0110：CH2 正输入连接到 RLDIN */
#define ADS129X_CH2SET_MUX_RLD_DRM                   0x07 /* bits[3:0]=0111：CH2 负输入连接到 RLDIN */
#define ADS129X_CH2SET_MUX_RLD_DRPM                  0x08 /* bits[3:0]=1000：CH2 正负输入都连接到 RLDIN */
#define ADS129X_CH2SET_MUX_IN3                       0x09 /* bits[3:0]=1001：IN3P 和 IN3N 接入 CH2 输入 */
#define ADS129X_CH2SET_MUX_RESERVED                  0x0A /* bits[3:0]=1010：保留 */

#define ADS129X_CH2SET_NORMAL(gain)                  (ADS129X_CH2SET_POWER_ON | (gain) | ADS129X_CH2SET_MUX_NORMAL)
#define ADS129X_CH2SET_TEST_SIGNAL(gain)             (ADS129X_CH2SET_POWER_ON | (gain) | ADS129X_CH2SET_MUX_TEST_SIGNAL)
#define ADS129X_CH2SET_NOISE_MEASURE(gain)           (ADS129X_CH2SET_POWER_ON | (gain) | ADS129X_CH2SET_MUX_SHORTED)
#define ADS129X_CH2SET_POWERDOWN_SHORT               (ADS129X_CH2SET_POWER_DOWN | ADS129X_CH2SET_MUX_SHORTED)


/*
 * RLD_SENS：Right Leg Drive Sense Selection，地址 0x06。
 *
 * 该寄存器选择每个通道的正/负输入是否参与右腿驱动 RLD 共模反馈，并控制 RLD buffer
 * 以及 RLD 导联脱落检测功能。
 *
 * RLD_SENS 位图：
 * +-------+-------+---------+---------------+-------+-------+-------+-------+
 * | bit7  | bit6  |  bit5   |     bit4      | bit3  | bit2  | bit1  | bit0  |
 * +-------+-------+---------+---------------+-------+-------+-------+-------+
 * | CHOP1 | CHOP0 | PDB_RLD | RLD_LOFF_SENS | RLD2N | RLD2P | RLD1N | RLD1P |
 * +-------+-------+---------+---------------+-------+-------+-------+-------+
 *
 * Bits[7:6] CHOP[1:0]：PGA chopping 频率
 * +------+-----------+
 * | CHOP | 频率      |
 * +------+-----------+
 * | 00   | fMOD / 16 |
 * | 01   | 保留      |
 * | 10   | fMOD / 2  |
 * | 11   | fMOD / 4  |
 * +------+-----------+
 *
 * Bit 5 PDB_RLD：RLD buffer 电源控制
 * +---+----------------------------+
 * | 0 | RLD buffer 关闭，默认      |
 * | 1 | RLD buffer 开启            |
 * +---+----------------------------+
 *
 * Bit 4 RLD_LOFF_SENS：RLD 导联脱落检测
 * +---+----------------------------+
 * | 0 | RLD 导联脱落检测关闭，默认 |
 * | 1 | RLD 导联脱落检测开启       |
 * +---+----------------------------+
 *
 * Bit 3 RLD2N：CH2 负输入是否参与 RLD
 * +---+----------------------------+
 * | 0 | 不连接，默认               |
 * | 1 | RLD 连接到 IN2N            |
 * +---+----------------------------+
 *
 * Bit 2 RLD2P：CH2 正输入是否参与 RLD
 * +---+----------------------------+
 * | 0 | 不连接，默认               |
 * | 1 | RLD 连接到 IN2P            |
 * +---+----------------------------+
 *
 * Bit 1 RLD1N：CH1 负输入是否参与 RLD
 * +---+----------------------------+
 * | 0 | 不连接，默认               |
 * | 1 | RLD 连接到 IN1N            |
 * +---+----------------------------+
 *
 * Bit 0 RLD1P：CH1 正输入是否参与 RLD
 * +---+----------------------------+
 * | 0 | 不连接，默认               |
 * | 1 | RLD 连接到 IN1P            |
 * +---+----------------------------+
 */

#define ADS129X_RLD_CHOP_FMOD_DIV16                  0x00 /* bits[7:6]=00：PGA chop 频率 fMOD/16 */
#define ADS129X_RLD_CHOP_RESERVED                    0x40 /* bits[7:6]=01：保留 */
#define ADS129X_RLD_CHOP_FMOD_DIV2                   0x80 /* bits[7:6]=10：PGA chop 频率 fMOD/2 */
#define ADS129X_RLD_CHOP_FMOD_DIV4                   0xC0 /* bits[7:6]=11：PGA chop 频率 fMOD/4 */

#define ADS129X_RLD_BUFFER_DISABLED                  0x00 /* bit5=0：RLD buffer 关闭，默认 */
#define ADS129X_RLD_BUFFER_ENABLED                   0x20 /* bit5=1：RLD buffer 开启 */

#define ADS129X_RLD_LOFF_SENS_DISABLED               0x00 /* bit4=0：RLD 导联脱落检测关闭，默认 */
#define ADS129X_RLD_LOFF_SENS_ENABLED                0x10 /* bit4=1：RLD 导联脱落检测开启 */

#define ADS129X_RLD_CH2N_DISABLED                    0x00 /* bit3=0：IN2N 不参与 RLD，默认 */
#define ADS129X_RLD_CH2N_ENABLED                     0x08 /* bit3=1：IN2N 参与 RLD */
#define ADS129X_RLD_CH2P_DISABLED                    0x00 /* bit2=0：IN2P 不参与 RLD，默认 */
#define ADS129X_RLD_CH2P_ENABLED                     0x04 /* bit2=1：IN2P 参与 RLD */
#define ADS129X_RLD_CH1N_DISABLED                    0x00 /* bit1=0：IN1N 不参与 RLD，默认 */
#define ADS129X_RLD_CH1N_ENABLED                     0x02 /* bit1=1：IN1N 参与 RLD */
#define ADS129X_RLD_CH1P_DISABLED                    0x00 /* bit0=0：IN1P 不参与 RLD，默认 */
#define ADS129X_RLD_CH1P_ENABLED                     0x01 /* bit0=1：IN1P 参与 RLD */

#define ADS129X_RLD_SENS_DISABLED                    (ADS129X_RLD_BUFFER_DISABLED) /* 0x00：RLD buffer 关闭，所有输入不参与 RLD 共模计算 */
#define ADS129X_RLD_SENS_CH1_RLD                     (ADS129X_RLD_BUFFER_ENABLED | ADS129X_RLD_CH1P_ENABLED | ADS129X_RLD_CH1N_ENABLED) /* 0x23：开启 RLD，CH1P/CH1N 参与 RLD 共模计算 */
#define ADS129X_RLD_SENS_CH2_RLD                     (ADS129X_RLD_BUFFER_ENABLED | ADS129X_RLD_CH2P_ENABLED | ADS129X_RLD_CH2N_ENABLED) /* 0x2C：开启 RLD，CH2P/CH2N 参与 RLD 共模计算 */
#define ADS129X_RLD_SENS_ALL_RLD                     (ADS129X_RLD_BUFFER_ENABLED | ADS129X_RLD_CH1P_ENABLED | ADS129X_RLD_CH1N_ENABLED | ADS129X_RLD_CH2P_ENABLED | ADS129X_RLD_CH2N_ENABLED) /* 0x2F：开启 RLD，CH1/CH2 P/N 全部参与 RLD 共模计算 */


/*
 * LOFF_SENS：Lead-Off Sense Selection，地址 0x07。
 *
 * 该寄存器选择每个通道的正/负输入是否参与导联脱落检测，并可反转各通道导联脱落检测电流方向。
 * 注意：只有对应 LOFF_SENS 位开启时，LOFF_STAT 中相应状态位才有意义。
 *
 * LOFF_SENS 位图：
 * +---+---+-------+-------+--------+--------+--------+--------+
 * | 7 | 6 | bit5  | bit4  |  bit3  |  bit2  |  bit1  |  bit0  |
 * +---+---+-------+-------+--------+--------+--------+--------+
 * | 0 | 0 | FLIP2 | FLIP1 | LOFF2N | LOFF2P | LOFF1N | LOFF1P |
 * +---+---+-------+-------+--------+--------+--------+--------+
 *
 * Bits[7:6]：
 * +-----------+----------------+
 * | bits[7:6] | 必须写 0       |
 * +-----------+----------------+
 *
 * Bit 5 FLIP2：CH2 导联脱落检测电流方向选择
 * +---+----------------------------+
 * | 0 | 关闭，默认                 |
 * | 1 | 开启                       |
 * +---+----------------------------+
 *
 * Bit 4 FLIP1：CH1 导联脱落检测电流方向选择
 * +---+----------------------------+
 * | 0 | 关闭，默认                 |
 * | 1 | 开启                       |
 * +---+----------------------------+
 *
 * Bit 3 LOFF2N：CH2 负输入是否参与导联脱落检测
 * +---+----------------------------+
 * | 0 | 关闭，默认                 |
 * | 1 | 开启                       |
 * +---+----------------------------+
 *
 * Bit 2 LOFF2P：CH2 正输入是否参与导联脱落检测
 * +---+----------------------------+
 * | 0 | 关闭，默认                 |
 * | 1 | 开启                       |
 * +---+----------------------------+
 *
 * Bit 1 LOFF1N：CH1 负输入是否参与导联脱落检测
 * +---+----------------------------+
 * | 0 | 关闭，默认                 |
 * | 1 | 开启                       |
 * +---+----------------------------+
 *
 * Bit 0 LOFF1P：CH1 正输入是否参与导联脱落检测
 * +---+----------------------------+
 * | 0 | 关闭，默认                 |
 * | 1 | 开启                       |
 * +---+----------------------------+
 */

#define ADS129X_LOFF_SENS_RESERVED_0                 0x00 /* bits[7:6] 必须写 0 */

#define ADS129X_LOFF_SENS_FLIP2_DISABLED             0x00 /* bit5=0：CH2 电流方向反转关闭，默认 */
#define ADS129X_LOFF_SENS_FLIP2_ENABLED              0x20 /* bit5=1：CH2 电流方向反转开启 */
#define ADS129X_LOFF_SENS_FLIP1_DISABLED             0x00 /* bit4=0：CH1 电流方向反转关闭，默认 */
#define ADS129X_LOFF_SENS_FLIP1_ENABLED              0x10 /* bit4=1：CH1 电流方向反转开启 */

#define ADS129X_LOFF_SENS_CH2N_DISABLED              0x00 /* bit3=0：CH2N 不参与导联脱落检测，默认 */
#define ADS129X_LOFF_SENS_CH2N_ENABLED               0x08 /* bit3=1：CH2N 参与导联脱落检测 */
#define ADS129X_LOFF_SENS_CH2P_DISABLED              0x00 /* bit2=0：CH2P 不参与导联脱落检测，默认 */
#define ADS129X_LOFF_SENS_CH2P_ENABLED               0x04 /* bit2=1：CH2P 参与导联脱落检测 */
#define ADS129X_LOFF_SENS_CH1N_DISABLED              0x00 /* bit1=0：CH1N 不参与导联脱落检测，默认 */
#define ADS129X_LOFF_SENS_CH1N_ENABLED               0x02 /* bit1=1：CH1N 参与导联脱落检测 */
#define ADS129X_LOFF_SENS_CH1P_DISABLED              0x00 /* bit0=0：CH1P 不参与导联脱落检测，默认 */
#define ADS129X_LOFF_SENS_CH1P_ENABLED               0x01 /* bit0=1：CH1P 参与导联脱落检测 */

#define ADS129X_LOFF_SENS_DISABLED                   0x00 /* 0x00：所有输入端不参与导联脱落检测 */
#define ADS129X_LOFF_SENS_CH1_PN                     (ADS129X_LOFF_SENS_CH1P_ENABLED | ADS129X_LOFF_SENS_CH1N_ENABLED) /* 0x03：CH1P/CH1N 参与导联脱落检测 */
#define ADS129X_LOFF_SENS_CH2_PN                     (ADS129X_LOFF_SENS_CH2P_ENABLED | ADS129X_LOFF_SENS_CH2N_ENABLED) /* 0x0C：CH2P/CH2N 参与导联脱落检测 */
#define ADS129X_LOFF_SENS_ALL_INPUTS                 (ADS129X_LOFF_SENS_CH1_PN | ADS129X_LOFF_SENS_CH2_PN) /* 0x0F：CH1/CH2 P/N 全部参与导联脱落检测 */


/*
 * LOFF_STAT：Lead-Off Status，地址 0x08。
 *
 * 该寄存器保存各输入端和 RLD 的导联脱落状态。只有对应 LOFF_SENS 位开启时，
 * 相关 LOFF_STAT 状态位才有意义；若 LOFF_SENS bits[3:0] 全为 0，则应忽略这些状态位。
 * 状态位中 0 表示连接正常，1 表示未连接。
 *
 * LOFF_STAT 位图：
 * +---+---------+---+----------+----------+----------+----------+----------+
 * | 7 |  bit6   | 5 |   bit4   |   bit3   |   bit2   |   bit1   |   bit0   |
 * +---+---------+---+----------+----------+----------+----------+----------+
 * | 0 | CLK_DIV | 0 | RLD_STAT | IN2N_OFF | IN2P_OFF | IN1N_OFF | IN1P_OFF |
 * +---+---------+---+----------+----------+----------+----------+----------+
 *
 * Bit 7：
 * +---+----------------+
 * | 0 | 必须为 0       |
 * +---+----------------+
 *
 * Bit 6 CLK_DIV：时钟分频选择
 * +---+-----------------------------------------------+
 * | 0 | fMOD = fCLK / 4，默认，用于 fCLK = 512kHz    |
 * | 1 | fMOD = fCLK / 16，用于 fCLK = 2.048MHz       |
 * +---+-----------------------------------------------+
 *
 * Bit 5：
 * +---+----------------+
 * | 0 | 必须为 0       |
 * +---+----------------+
 *
 * Bit 4 RLD_STAT：RLD 导联脱落状态，只读
 * +---+----------------------------+
 * | 0 | RLD 已连接，默认           |
 * | 1 | RLD 未连接                 |
 * +---+----------------------------+
 *
 * Bit 3 IN2N_OFF：CH2 负输入电极状态，只读
 * +---+----------------------------+
 * | 0 | 已连接，默认               |
 * | 1 | 未连接                     |
 * +---+----------------------------+
 *
 * Bit 2 IN2P_OFF：CH2 正输入电极状态，只读
 * +---+----------------------------+
 * | 0 | 已连接，默认               |
 * | 1 | 未连接                     |
 * +---+----------------------------+
 *
 * Bit 1 IN1N_OFF：CH1 负输入电极状态，只读
 * +---+----------------------------+
 * | 0 | 已连接，默认               |
 * | 1 | 未连接                     |
 * +---+----------------------------+
 *
 * Bit 0 IN1P_OFF：CH1 正输入电极状态，只读
 * +---+----------------------------+
 * | 0 | 已连接，默认               |
 * | 1 | 未连接                     |
 * +---+----------------------------+
 */

#define ADS129X_LOFF_STAT_FIXED_0                    0x00 /* bit7/bit5 固定为 0 */
#define ADS129X_LOFF_STAT_CLK_DIV_FCLK_4             0x00 /* bit6=0：fMOD = fCLK/4，默认 */
#define ADS129X_LOFF_STAT_CLK_DIV_FCLK_16            0x40 /* bit6=1：fMOD = fCLK/16 */

#define ADS129X_LOFF_STAT_RLD_CONNECTED              0x00 /* bit4=0：RLD 已连接，默认 */
#define ADS129X_LOFF_STAT_RLD_OFF                    0x10 /* bit4=1：RLD 未连接 */
#define ADS129X_LOFF_STAT_IN2N_CONNECTED             0x00 /* bit3=0：IN2N 已连接，默认 */
#define ADS129X_LOFF_STAT_IN2N_OFF                   0x08 /* bit3=1：IN2N 未连接 */
#define ADS129X_LOFF_STAT_IN2P_CONNECTED             0x00 /* bit2=0：IN2P 已连接，默认 */
#define ADS129X_LOFF_STAT_IN2P_OFF                   0x04 /* bit2=1：IN2P 未连接 */
#define ADS129X_LOFF_STAT_IN1N_CONNECTED             0x00 /* bit1=0：IN1N 已连接，默认 */
#define ADS129X_LOFF_STAT_IN1N_OFF                   0x02 /* bit1=1：IN1N 未连接 */
#define ADS129X_LOFF_STAT_IN1P_CONNECTED             0x00 /* bit0=0：IN1P 已连接，默认 */
#define ADS129X_LOFF_STAT_IN1P_OFF                   0x01 /* bit0=1：IN1P 未连接 */

#define ADS129X_LOFF_STAT_INPUT_MASK                 0x0F /* IN2N/IN2P/IN1N/IN1P 状态掩码 */
#define ADS129X_LOFF_STAT_ALL_CONNECTED              0x00 /* 所有已启用检测的输入均连接 */


/*
 * RESP1：Respiration Control Register 1，地址 0x09。
 *
 * 该寄存器控制呼吸阻抗测量功能，仅适用于 ADS1292R。对于 ADS1291 和 ADS1292，
 * 必须向 RESP1 写入 0x02。
 *
 * RESP1 位图：
 * +----------------+--------------+----------+----------+----------+----------+---+-----------+
 * |      bit7      |     bit6     |   bit5   |   bit4   |   bit3   |   bit2   | 1 |   bit0    |
 * +----------------+--------------+----------+----------+----------+----------+---+-----------+
 * | RESP_DEMOD_EN1 | RESP_MOD_EN  | RESP_PH3| RESP_PH2| RESP_PH1| RESP_PH0| 1 | RESP_CTRL |
 * +----------------+--------------+----------+----------+----------+----------+---+-----------+
 *
 * Bit 7 RESP_DEMOD_EN1：呼吸解调电路控制
 * +---+----------------------------+
 * | 0 | 解调电路关闭，默认         |
 * | 1 | 解调电路开启               |
 * +---+----------------------------+
 *
 * Bit 6 RESP_MOD_EN：呼吸调制电路控制
 * +---+----------------------------+
 * | 0 | 调制电路关闭，默认         |
 * | 1 | 调制电路开启               |
 * +---+----------------------------+
 *
 * Bits[5:2] RESP_PH[3:0]：呼吸解调相位
 * +----------+----------------+----------------+
 * | RESP_PH  | RESP_CLK=32kHz | RESP_CLK=64kHz |
 * +----------+----------------+----------------+
 * | 0000     | 0°，默认       | 0°，默认       |
 * | 0001     | 11.25°         | 22.5°          |
 * | 0010     | 22.5°          | 45°            |
 * | 0011     | 33.75°         | 67.5°          |
 * | 0100     | 45°            | 90°            |
 * | 0101     | 56.25°         | 112.5°         |
 * | 0110     | 67.5°          | 135°           |
 * | 0111     | 78.75°         | 157.5°         |
 * | 1000     | 90°            | 不可用         |
 * | 1001     | 101.25°        | 不可用         |
 * | 1010     | 112.5°         | 不可用         |
 * | 1011     | 123.75°        | 不可用         |
 * | 1100     | 135°           | 不可用         |
 * | 1101     | 146.25°        | 不可用         |
 * | 1110     | 157.5°         | 不可用         |
 * | 1111     | 168.75°        | 不可用         |
 * +----------+----------------+----------------+
 *
 * 注意：RESP_CLK = 64kHz 时，RESP_PH3 bit 会被忽略。
 *
 * Bit 1：
 * +---+----------------+
 * | 1 | 必须写 1       |
 * +---+----------------+
 *
 * Bit 0 RESP_CTRL：呼吸控制时钟选择
 * +---+--------------------------------+
 * | 0 | 内部呼吸功能使用内部时钟       |
 * | 1 | 内部呼吸功能使用外部时钟       |
 * +---+--------------------------------+
 */

#define ADS129X_RESP1_DEMOD_DISABLED                 0x00 /* bit7=0：呼吸解调电路关闭，默认 */
#define ADS129X_RESP1_DEMOD_ENABLED                  0x80 /* bit7=1：呼吸解调电路开启 */
#define ADS129X_RESP1_MOD_DISABLED                   0x00 /* bit6=0：呼吸调制电路关闭，默认 */
#define ADS129X_RESP1_MOD_ENABLED                    0x40 /* bit6=1：呼吸调制电路开启 */

#define ADS129X_RESP1_PHASE_0                        0x00 /* bits[5:2]=0000：32kHz/64kHz 均为 0° */
#define ADS129X_RESP1_PHASE_11_25                    0x04 /* bits[5:2]=0001：32kHz=11.25°，64kHz=22.5° */
#define ADS129X_RESP1_PHASE_22_5                     0x08 /* bits[5:2]=0010：32kHz=22.5°，64kHz=45° */
#define ADS129X_RESP1_PHASE_33_75                    0x0C /* bits[5:2]=0011：32kHz=33.75°，64kHz=67.5° */
#define ADS129X_RESP1_PHASE_45                       0x10 /* bits[5:2]=0100：32kHz=45°，64kHz=90° */
#define ADS129X_RESP1_PHASE_56_25                    0x14 /* bits[5:2]=0101：32kHz=56.25°，64kHz=112.5° */
#define ADS129X_RESP1_PHASE_67_5                     0x18 /* bits[5:2]=0110：32kHz=67.5°，64kHz=135° */
#define ADS129X_RESP1_PHASE_78_75                    0x1C /* bits[5:2]=0111：32kHz=78.75°，64kHz=157.5° */
#define ADS129X_RESP1_PHASE_90                       0x20 /* bits[5:2]=1000：32kHz=90°，64kHz 不可用 */
#define ADS129X_RESP1_PHASE_101_25                   0x24 /* bits[5:2]=1001：32kHz=101.25°，64kHz 不可用 */
#define ADS129X_RESP1_PHASE_112_5                    0x28 /* bits[5:2]=1010：32kHz=112.5°，64kHz 不可用 */
#define ADS129X_RESP1_PHASE_123_75                   0x2C /* bits[5:2]=1011：32kHz=123.75°，64kHz 不可用 */
#define ADS129X_RESP1_PHASE_135                      0x30 /* bits[5:2]=1100：32kHz=135°，64kHz 不可用 */
#define ADS129X_RESP1_PHASE_146_25                   0x34 /* bits[5:2]=1101：32kHz=146.25°，64kHz 不可用 */
#define ADS129X_RESP1_PHASE_157_5                    0x38 /* bits[5:2]=1110：32kHz=157.5°，64kHz 不可用 */
#define ADS129X_RESP1_PHASE_168_75                   0x3C /* bits[5:2]=1111：32kHz=168.75°，64kHz 不可用 */

#define ADS129X_RESP1_FIXED_1                        0x02 /* bit1 必须写 1 */
#define ADS129X_RESP1_CTRL_INTERNAL_CLOCK            0x00 /* bit0=0：内部呼吸功能使用内部时钟 */
#define ADS129X_RESP1_CTRL_EXTERNAL_CLOCK            0x01 /* bit0=1：内部呼吸功能使用外部时钟 */

#define ADS129X_RESP1_NON_RESP                       ADS129X_RESP1_FIXED_1 /* 0x02：ADS1291/ADS1292 必须写入 */
#define ADS129X_RESP1_ADS1292R_RESP                  (ADS129X_RESP1_DEMOD_ENABLED | ADS129X_RESP1_MOD_ENABLED | ADS129X_RESP1_PHASE_112_5 | ADS129X_RESP1_FIXED_1) /* 0xEA：ADS1292R 呼吸功能默认配置 */


/*
 * RESP2：Respiration Control Register 2，地址 0x0A。
 *
 * 该寄存器控制呼吸频率、RLDREF 来源和 offset calibration。
 *
 * RESP2 位图：
 * +----------+---+---+---+---+-----------+------------+---+
 * |   bit7   | 6 | 5 | 4 | 3 |   bit2    |    bit1    | 0 |
 * +----------+---+---+---+---+-----------+------------+---+
 * | CALIB_ON | 0 | 0 | 0 | 0 | RESP_FREQ | RLDREF_INT | 1 |
 * +----------+---+---+---+---+-----------+------------+---+
 *
 * Bit 7 CALIB_ON：offset calibration 控制
 * +---+----------------------------+
 * | 0 | 关闭，默认                 |
 * | 1 | 开启                       |
 * +---+----------------------------+
 *
 * Bits[6:3]：
 * +-----------+----------------+
 * | bits[6:3] | 必须写 0       |
 * +-----------+----------------+
 *
 * Bit 2 RESP_FREQ：呼吸控制频率，仅 ADS1292R 使用
 * +---+--------------------------------------------+
 * | 0 | 32kHz，默认                                |
 * | 1 | 64kHz；ADS1291/ADS1292 必须写 1            |
 * +---+--------------------------------------------+
 *
 * Bit 1 RLDREF_INT：RLDREF 来源
 * +---+--------------------------------------------+
 * | 0 | RLDREF 外部输入                            |
 * | 1 | 内部产生 RLDREF = (AVDD - AVSS) / 2，默认  |
 * +---+--------------------------------------------+
 *
 * Bit 0：
 * +---+----------------+
 * | 1 | 必须写 1       |
 * +---+----------------+
 */

#define ADS129X_RESP2_CALIB_OFF                    0x00 /* bit7=0：offset calibration 关闭，默认 */
#define ADS129X_RESP2_CALIB_ON                     0x80 /* bit7=1：offset calibration 开启 */
#define ADS129X_RESP2_RESERVED_0                   0x00 /* bits[6:3] 必须写 0 */

#define ADS129X_RESP2_FREQ_32K                     0x00 /* bit2=0：RESP 32kHz，默认，ADS1292R 使用 */
#define ADS129X_RESP2_FREQ_64K                     0x04 /* bit2=1：RESP 64kHz，ADS1291/ADS1292 必须写 1 */

#define ADS129X_RESP2_RLDREF_EXTERNAL              0x00 /* bit1=0：RLDREF 外部输入 */
#define ADS129X_RESP2_RLDREF_INTERNAL_BIT          0x02 /* bit1=1：内部 RLDREF，默认 */
#define ADS129X_RESP2_FIXED_1                      0x01 /* bit0 必须写 1 */

#define ADS129X_RESP2_RLDREF_INTERNAL              (ADS129X_RESP2_FREQ_64K | ADS129X_RESP2_RLDREF_INTERNAL_BIT | ADS129X_RESP2_FIXED_1) /* 0x07：ADS1291/ADS1292 安全默认值 */
#define ADS129X_RESP2_RLDREF_INTERNAL_RESP_32K     (ADS129X_RESP2_FREQ_32K | ADS129X_RESP2_RLDREF_INTERNAL_BIT | ADS129X_RESP2_FIXED_1) /* 0x03：ADS1292R 内部 RLDREF，RESP 32kHz */


/*
 * GPIO：General-Purpose I/O Register，地址 0x0B。
 *
 * 该寄存器控制 ADS129x 内部 GPIO1/GPIO2 的方向和数据。某些 respiration 模式下 GPIO 不可用。
 *
 * GPIO 位图：
 * +---+---+---+---+--------+--------+--------+--------+
 * | 7 | 6 | 5 | 4 |  bit3  |  bit2  |  bit1  |  bit0  |
 * +---+---+---+---+--------+--------+--------+--------+
 * | 0 | 0 | 0 | 0 | GPIOC2 | GPIOC1 | GPIOD2 | GPIOD1 |
 * +---+---+---+---+--------+--------+--------+--------+
 *
 * Bits[7:4]：
 * +-----------+----------------+
 * | bits[7:4] | 必须写 0       |
 * +-----------+----------------+
 *
 * Bits[3:2] GPIOC[2:1]：GPIO1/GPIO2 方向控制
 * +----------+----------------+
 * | GPIOCx   | 方向           |
 * +----------+----------------+
 * | 0        | 输出           |
 * | 1        | 输入，默认     |
 * +----------+----------------+
 *
 * Bits[1:0] GPIOD[2:1]：GPIO1/GPIO2 数据
 * +----------+------------------------------------------------+
 * | GPIODx   | 含义                                           |
 * +----------+------------------------------------------------+
 * | 0/1      | 输出模式下写输出值；输入模式下读外部引脚状态   |
 * +----------+------------------------------------------------+
 */

#define ADS129X_GPIO_RESERVED_0                    0x00 /* bits[7:4] 必须写 0 */

#define ADS129X_GPIO2_OUTPUT                       0x00 /* bit3=0：GPIO2 输出 */
#define ADS129X_GPIO2_INPUT                        0x08 /* bit3=1：GPIO2 输入，默认 */
#define ADS129X_GPIO1_OUTPUT                       0x00 /* bit2=0：GPIO1 输出 */
#define ADS129X_GPIO1_INPUT                        0x04 /* bit2=1：GPIO1 输入，默认 */

#define ADS129X_GPIO2_DATA_LOW                     0x00 /* bit1=0：GPIO2 输出低/输入读低 */
#define ADS129X_GPIO2_DATA_HIGH                    0x02 /* bit1=1：GPIO2 输出高/输入读高 */
#define ADS129X_GPIO1_DATA_LOW                     0x00 /* bit0=0：GPIO1 输出低/输入读低 */
#define ADS129X_GPIO1_DATA_HIGH                    0x01 /* bit0=1：GPIO1 输出高/输入读高 */

#define ADS129X_GPIO_DEFAULT_VALUE                 (ADS129X_GPIO2_INPUT | ADS129X_GPIO1_INPUT) /* 0x0C：GPIO1/2 默认输入 */

/*
 * 默认寄存器表：
 * - ID:        由 ADS129X_CHIP 映射。
 * - CONFIG1:   由 ADS129X_SAMPLE_RATE_HZ 决定。
 * - CONFIG2:   0xF0，内部参考缓冲开启，4.033V 参考，导联脱落比较器开启。
 * - LOFF:      0x54，直流导联脱落检测，电流 22nA，阈值 90%/10%。
 * - CH1SET:    0x10，CH1 正常输入，PGA gain = 1（饱和排查用最低增益）。
 * - CH2SET:    0x81，当 ADS129X_CHIP 为 ADS1291 时，power-down + input short。
 * - RLD_SENS:  0x33，开启 RLD 和 RLD 脱落检测，CH1P/CH1N 参与 RLD 共模计算。
 * - LOFF_SENS: 0x03，CH1P/CH1N 参与导联脱落检测。
 * - LOFF_STAT: 0x00，默认所有已启用检测的输入均连接。
 * - RESP1:     0x02，当 ADS129X_CHIP 为 ADS1291/ADS1292 时，必须写入的非呼吸模式配置。
 * - RESP2:     0x07，内部 RLDREF，当 ADS129X_CHIP 为 ADS1291/ADS1292 时，必须将 RESP_FREQ 写 1。
 * - GPIO:      0x0C，GPIO1/2 默认输入。
 */

#define ADS129X_ID_DEFAULT                        ADS129X_ID_FROM_CHIP(ADS129X_CHIP)
#define ADS129X_CONFIG1_DEFAULT                   ADS129X_CONFIG1_FROM_SAMPLE_RATE(ADS129X_SAMPLE_RATE_HZ)
#define ADS129X_CONFIG2_DEFAULT                   (ADS129X_CONFIG2_INTERNAL_REF_4V | ADS129X_CONFIG2_PDB_LOFF_COMP_ENABLED)
#define ADS129X_LOFF_DEFAULT                      ADS129X_LOFF_DC(ADS129X_LOFF_COMP_TH_90_10, ADS129X_LOFF_CURRENT_22NA)
#define ADS129X_CH1SET_DEFAULT                    ADS129X_CH1SET_NORMAL(ADS129X_CH1SET_GAIN_1)
#define ADS129X_CH2SET_DEFAULT                    ADS129X_CH2SET_POWERDOWN_SHORT
#define ADS129X_RLD_SENS_DEFAULT                  (ADS129X_RLD_LOFF_SENS_ENABLED | ADS129X_RLD_SENS_CH1_RLD)
#define ADS129X_LOFF_SENS_DEFAULT                 ADS129X_LOFF_SENS_CH1_PN
#define ADS129X_LOFF_STAT_DEFAULT                 ADS129X_LOFF_STAT_ALL_CONNECTED
#define ADS129X_RESP1_DEFAULT                     ADS129X_RESP1_NON_RESP
#define ADS129X_RESP2_DEFAULT                     ADS129X_RESP2_RLDREF_INTERNAL
#define ADS129X_GPIO_DEFAULT                      ADS129X_GPIO_DEFAULT_VALUE

/*
 * 驱动内一阶高通滤波开关。
 *
 * 置 1：驱动读帧后先对通道 24-bit ADC 码做一阶 IIR 高通，再生成 int16 输出，
 *       并把 raw_ch* 回写成高通后的 24-bit 二进制补码字节。
 * 置 0：驱动只做符号扩展、右移压缩和限幅，raw_ch* 保留芯片原始 24-bit 字节。
 */
#ifndef ADS129X_HIGHPASS_ENABLE
#define ADS129X_HIGHPASS_ENABLE                  1
#endif

/*
 * 高通截止频率，单位 Hz。
 *
 * 精度和 QX_Driver/ads129x.c 保持一致：初始化或采样率变化时把 alpha 换算为 Q31，
 * 每个采样点仍使用定点整数运算。
 */
#ifndef ADS129X_HIGHPASS_CUTOFF_HZ
#define ADS129X_HIGHPASS_CUTOFF_HZ               0.4f
#endif

/* 二阶 Butterworth 数字低通，适用于监护型单导联 ECG。 */
#ifndef ADS129X_LOWPASS_ENABLE
#define ADS129X_LOWPASS_ENABLE                   1
#endif

#ifndef ADS129X_LOWPASS_CUTOFF_HZ
#define ADS129X_LOWPASS_CUTOFF_HZ                40.0f
#endif

/*
 * 50 Hz 工频陷波（数字 IIR）。ADS1291 芯片无内置 notch，在驱动读帧路径中实现。
 * 处理顺序：高通 → 陷波 → 低通 → 右移压缩为 int16。
 */
#ifndef ADS129X_NOTCH_ENABLE
#define ADS129X_NOTCH_ENABLE                     1
#endif

#ifndef ADS129X_NOTCH_FREQ_HZ
#define ADS129X_NOTCH_FREQ_HZ                    50.0f
#endif

/* 陷波极点半径 (0~1)，越接近 1 越窄；0.95 兼顾抑制与波形失真 */
#ifndef ADS129X_NOTCH_R
#define ADS129X_NOTCH_R                          0.95f
#endif

/*
 * 24-bit ADC 码输出压缩位数。
 *
 * 驱动内部先把 24-bit two's complement 扩展到 int32_t；
 * 24-bit 转 16 bit 默认右移位数为 8，根据需求可调整为 4。
 * 若高通开启，则先做驱动内高通，再算术右移该位数并限幅到 int16_t。
 * 若高通关闭，则直接算术右移该位数并限幅到 int16_t。
 */
#define ADS129X_RAW_OUTPUT_SHIFT                  4

/*
 * 单帧采样数据。
 *
 * status 保留 ADS129x 原始状态字节；raw_ch* 为 24-bit 字节。
 * ch*_value 是驱动做完符号扩展、右移和 int16 限幅后的最小可用采样值。
 */
typedef struct {
  uint8_t status[ADS129X_STATUS_SIZE];
  uint8_t raw_ch1[ADS129X_CHANNEL_SIZE];
  int16_t ch1_value;
#if ADS129X_HAS_CH2
  uint8_t raw_ch2[ADS129X_CHANNEL_SIZE];
  int16_t ch2_value;
#endif
  uint8_t loff_status;
} ads129x_sample_t;

/*
 * 基础生命周期接口。
 *
 * ads129x_init() 只完成 SPI/GPIO、复位和默认寄存器配置。连续采样由应用层在
 * DRDY 中断、采样线程和消息队列准备完成后显式启动。
 */

/* 初始化 SPI/GPIO、复位 ADS129x、写默认寄存器并读回寄存器打印确认。 */
int ads129x_init(void);

/* 初始化后首次开始采集：切到高速 SPI，START 后等待 10ms，再进入 RDATAC。 */
int ads129x_init_start(void);

/* 运行期开始采集：切到高速 SPI，START 后等待命令保护间隔，再进入 RDATAC。 */
int ads129x_start(void);

/* 发送 STOP + SDATAC，并拉低 START。停止后寄存器可重新访问。 */
int ads129x_stop(void);

/*
 * 在 DRDY 就绪后读取一帧 RDATAC 数据。
 *
 * 调用方负责保证读取时机来自 DRDY active edge、轮询 ready 状态或等价同步源。
 * 驱动内部只发送 dummy 0x00 提供 SCLK，不发送 RDATA opcode。
 */
int ads129x_read_frame(ads129x_sample_t *sample);

/* 检查 DRDY 是否就绪；返回 true 表示逻辑 active，也就是有一帧数据可读。*/
bool ads129x_is_data_ready(void);

/*
 * 低层命令与寄存器访问接口。
 *
 * 这些接口主要供驱动内部和硬件调试使用。正常业务代码优先使用下方 set_* 配置接口；
 * 若直接调用 wreg()，调用方必须确认芯片已经退出 RDATAC。
 *
 * rreg() 内部会先发送 SDATAC 退出连续读模式；wreg() 不会主动发送 SDATAC。
 * send_command() 会对非 START/STOP 命令做 DRDY inactive 避让和命令保护延时。
 */
void ads129x_wakeup(void);
void ads129x_standby(void);
void ads129x_reset(void);
int ads129x_send_command(uint8_t cmd);
int ads129x_rreg(uint8_t start_addr, uint8_t count, uint8_t *out_buf);
int ads129x_wreg(uint8_t start_addr, uint8_t count, const uint8_t *in_buf);

/*
 * 运行期寄存器配置接口。
 *
 * 这些函数用于连续采样期间调整采样率、PGA 增益和 RLD 配置。函数内部会先退出
 * RDATAC，完成读改写后重新进入 RDATAC。
 *
 * set_sampling_rate() 会同步重置驱动内高通状态，避免采样率变化后沿用旧 alpha。
 * set_pga_gain() 的 gain 参数使用已经移位到 CHxSET bit6..4 的 ADS129X_CHxSET_GAIN_* 宏。
 * set_rld() 只更新 RLD_SENS 低 6 位，保留 CHOP[1:0]。
 * set_vref() 保留 CONFIG2 中测试信号、时钟输出和 lead-off comparator 等其它位。
 */
int ads129x_set_sampling_rate(uint8_t rate);
int ads129x_set_pga_gain(uint8_t ch1_gain, uint8_t ch2_gain);
int ads129x_set_rld(uint8_t enable, uint8_t rld_inputs, uint8_t loff_sense);
int ads129x_set_vref(uint8_t vref_4v);
int ads129x_configure_default(void);

/*
 * 返回 DRDY GPIO 编号。
 *
 * 驱动只在 ads129x_init() 中把 DRDY 配成输入，不注册中断、不创建 semaphore。
 * 应用层可以基于该引脚注册 GPIO 中断、轮询 ads129x_is_data_ready()，或接入自己的
 * 采样线程调度策略。
 */
int ads129x_drdy_pin(void);

/*
 * 调试辅助（BLE 模式下 UART 仍可给 idf_monitor 打日志）：
 * - log_pins：GPIO 电平与当前 SPI 时钟
 * - log_registers：SDATAC 后读回全部寄存器（采集中请勿调用）
 * - log_drdy_activity：在 window_ms 内轮询 DRDY，统计高低与边沿
 * - log_sample：打印一帧 status/raw24/int16
 */
void ads129x_log_pins(const char *why);
int ads129x_log_registers(const char *why);
void ads129x_log_drdy_activity(uint32_t window_ms);
void ads129x_log_sample(const ads129x_sample_t *sample, const char *why);

#endif /* ADS129X_H */
