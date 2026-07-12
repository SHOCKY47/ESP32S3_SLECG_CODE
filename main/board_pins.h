#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/gpio.h"

/*
 * ESP32S3_SL_ECG 板级管脚定义（IOx = GPIOx）。
 * ADS1291 接线以当前实际硬件为准（2026-07 核对）。
 *
 * ADS1291 ↔ ESP32-S3:
 *   DRDY → GPIO3
 *   DOUT → GPIO46 (MISO，ESP32-S3 上 GPIO46 仅输入，适合 DOUT)
 *   SCLK → GPIO9
 *   DIN  → GPIO10 (MOSI)
 *   CS   → GPIO11
 *   START→ GPIO12  （注意：ESP32-S3 SPI2 默认 SCLK 也是 12，故 ADS SPI 必须用 SPI3）
 *   PWDN → GPIO13  （注意：ESP32-S3 SPI2 默认 MISO 也是 13）
 */

/* 电池电压采集（本阶段不启用） */
#define BOARD_BAT_ADC_GPIO          GPIO_NUM_1

/* ADS1291 控制与 SPI */
#define BOARD_ADS_DRDY_GPIO         GPIO_NUM_3
#define BOARD_ADS_MISO_GPIO         GPIO_NUM_46  /* ADS DOUT */
#define BOARD_ADS_SCLK_GPIO         GPIO_NUM_9
#define BOARD_ADS_MOSI_GPIO         GPIO_NUM_10  /* ADS DIN */
#define BOARD_ADS_CS_GPIO           GPIO_NUM_11
#define BOARD_ADS_START_GPIO        GPIO_NUM_12
#define BOARD_ADS_PWDN_GPIO         GPIO_NUM_13

/* 用户按键：按下时拉低 */
#define BOARD_BTN_GPIO              GPIO_NUM_18

/* 双色指示灯：低电平点亮；绿=有线/UART，蓝=蓝牙/BLE */
#define BOARD_LED_GREEN_GPIO        GPIO_NUM_41
#define BOARD_LED_BLUE_GPIO         GPIO_NUM_42

#define BOARD_LED_ON(gpio)          gpio_set_level((gpio), 0)
#define BOARD_LED_OFF(gpio)         gpio_set_level((gpio), 1)

#endif /* BOARD_PINS_H */
