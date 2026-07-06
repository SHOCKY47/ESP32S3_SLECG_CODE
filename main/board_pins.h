#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/gpio.h"

/*
 * ESP32S3_SL_ECG 板级管脚定义（IOx = GPIOx）。
 * 来源：ESP32S3_SL_ECG 管脚分配表.numbers
 */

/* 电池电压采集（本阶段不启用） */
#define BOARD_BAT_ADC_GPIO          GPIO_NUM_1

/* ADS1291 控制与 SPI */
#define BOARD_ADS_DRDY_GPIO         GPIO_NUM_2
#define BOARD_ADS_MOSI_GPIO         GPIO_NUM_3
#define BOARD_ADS_MISO_GPIO         GPIO_NUM_4
#define BOARD_ADS_SCLK_GPIO         GPIO_NUM_5
#define BOARD_ADS_CS_GPIO           GPIO_NUM_6
#define BOARD_ADS_START_GPIO        GPIO_NUM_7
#define BOARD_ADS_PWDN_GPIO         GPIO_NUM_8

/* 指示灯：低电平点亮 */
#define BOARD_LED_GREEN_GPIO        GPIO_NUM_10

#define BOARD_LED_ON(gpio)          gpio_set_level((gpio), 0)
#define BOARD_LED_OFF(gpio)         gpio_set_level((gpio), 1)

#endif /* BOARD_PINS_H */
