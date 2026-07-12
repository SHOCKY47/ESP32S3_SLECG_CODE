/*
 * slecg_ecg.c
 * ADS1291 采集任务：按 DRDY/采样周期读帧，每 25 点组包发送。
 * BLE 模式下周期性打印 SPI 原始样本，供 UART idf_monitor 诊断全 0 问题。
 */
#include "slecg_ecg.h"

#include <stdbool.h>
#include <stdio.h>

#include "ads129x.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "slecg_fsm.h"
#include "slecg_proto_payload.h"
#include "slecg_proto_types.h"
#include "slecg_state.h"
#include "slecg_transport.h"

static const char *TAG = "slecg_ecg";

#define SLECG_ECG_TASK_STACK        4096
#define SLECG_ECG_TASK_PRIO         8
/* 250 Hz → 4ms/样本；超时略宽以兼容抖动 */
#define SLECG_DRDY_WAIT_MS          12
#define SLECG_DRDY_HIGH_WAIT_MS     6
/* BLE 调试：前 N 次每样本都打；之后每 PERIOD 次打一条 */
#define SLECG_BLE_DBG_FIRST_N       8
#define SLECG_BLE_DBG_EVERY_N       25

static SemaphoreHandle_t s_drdy_sem;

static uint32_t s_frames_sent;
static uint32_t s_frames_fail;
static uint32_t s_reads_ok;
static uint32_t s_reads_fail;
static uint32_t s_drdy_timeouts;
static uint32_t s_drdy_edge_hits;
static uint32_t s_nonzero_samples;
static int32_t s_last_raw32;
static int16_t s_last_ch1;
static uint8_t s_last_status[3];

static void IRAM_ATTR drdy_isr_handler(void *arg)
{
	BaseType_t wake = pdFALSE;
	(void)arg;
	if (s_drdy_sem != NULL) {
		xSemaphoreGiveFromISR(s_drdy_sem, &wake);
		if (wake == pdTRUE) {
			portYIELD_FROM_ISR();
		}
	}
}

static void drdy_gpio_init(void)
{
	gpio_config_t cfg = {
		.pin_bit_mask = (1ULL << BOARD_ADS_DRDY_GPIO),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_NEGEDGE,
	};
	gpio_config(&cfg);
	{
		esp_err_t isr_err = gpio_install_isr_service(0);
		if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
			ESP_LOGE(TAG, "GPIO ISR 服务安装失败: %s", esp_err_to_name(isr_err));
			return;
		}
	}
	gpio_isr_handler_add(BOARD_ADS_DRDY_GPIO, drdy_isr_handler, NULL);
}

static uint32_t now_ms(void)
{
	return (uint32_t)(esp_timer_get_time() / 1000LL);
}

void slecg_ecg_reset_stats(void)
{
	s_frames_sent = 0;
	s_frames_fail = 0;
	s_reads_ok = 0;
	s_reads_fail = 0;
	s_drdy_timeouts = 0;
	s_drdy_edge_hits = 0;
	s_nonzero_samples = 0;
	s_last_raw32 = 0;
	s_last_ch1 = 0;
	s_last_status[0] = s_last_status[1] = s_last_status[2] = 0;
}

void slecg_ecg_log_stats(void)
{
	ESP_LOGI(TAG,
		 "ECG统计: 读成功=%lu 读失败=%lu 帧发送=%lu 帧失败=%lu "
		 "DRDY边沿=%lu DRDY超时强制读=%lu 非零样本=%lu "
		 "last_status=%02X%02X%02X last_raw32=%ld last_ch1=%d",
		 (unsigned long)s_reads_ok,
		 (unsigned long)s_reads_fail,
		 (unsigned long)s_frames_sent,
		 (unsigned long)s_frames_fail,
		 (unsigned long)s_drdy_edge_hits,
		 (unsigned long)s_drdy_timeouts,
		 (unsigned long)s_nonzero_samples,
		 s_last_status[0], s_last_status[1], s_last_status[2],
		 (long)s_last_raw32,
		 (int)s_last_ch1);
}

/*
 * 等待下一次转换完成（限速，避免空转灌串口）：
 * 1) 优先等 DRDY 下降沿
 * 2) DRDY 已为低（有未读数据）则立即读
 * 3) 超时仍无边沿：按采样周期强制读一次
 */
static void wait_next_sample(bool *from_timeout)
{
	const TickType_t period_ticks = pdMS_TO_TICKS(
		(1000U + ADS129X_SAMPLE_RATE_HZ - 1U) / ADS129X_SAMPLE_RATE_HZ);
	const TickType_t wait_ticks =
		(period_ticks > pdMS_TO_TICKS(SLECG_DRDY_WAIT_MS))
			? period_ticks
			: pdMS_TO_TICKS(SLECG_DRDY_WAIT_MS);

	*from_timeout = false;

	if (xSemaphoreTake(s_drdy_sem, wait_ticks) == pdTRUE) {
		s_drdy_edge_hits++;
		return;
	}

	if (ads129x_is_data_ready()) {
		return;
	}

	s_drdy_timeouts++;
	*from_timeout = true;
}

static void ble_debug_log_sample(const ads129x_sample_t *sample, bool from_timeout)
{
	const slecg_runtime_t *rt = slecg_fsm_get_runtime();
	char why[32];

	if (rt == NULL || rt->transport != SLECG_TRANSPORT_BLE) {
		return;
	}

	if (s_reads_ok <= SLECG_BLE_DBG_FIRST_N ||
	    (s_reads_ok % SLECG_BLE_DBG_EVERY_N) == 0) {
		snprintf(why, sizeof(why), "n=%lu%s",
			 (unsigned long)s_reads_ok,
			 from_timeout ? " TO" : "");
		ads129x_log_sample(sample, why);
	}

	/* 每约 1 秒一条汇总（125Hz → 每 125 点） */
	if ((s_reads_ok % ADS129X_SAMPLE_RATE_HZ) == 0) {
		ESP_LOGI(TAG,
			 "BLE秒汇总: reads=%lu edges=%lu timeouts=%lu nonzero=%lu "
			 "last_raw32=%ld last_ch1=%d sent=%lu fail=%lu",
			 (unsigned long)s_reads_ok,
			 (unsigned long)s_drdy_edge_hits,
			 (unsigned long)s_drdy_timeouts,
			 (unsigned long)s_nonzero_samples,
			 (long)s_last_raw32,
			 (int)s_last_ch1,
			 (unsigned long)s_frames_sent,
			 (unsigned long)s_frames_fail);
	}
}

static void ecg_task(void *arg)
{
	int16_t sample_buf[SLECG_ECG_SAMPLES_PER_PKT];
	uint8_t sample_count = 0;
	uint8_t last_loff = 0;
	uint8_t frame[SLECG_ECG_FRAME_SIZE];
	uint32_t first_ts = 0;
	TickType_t last_read_tick = 0;

	(void)arg;

	while (true) {
		const slecg_runtime_t *rt = slecg_fsm_get_runtime();
		bool from_timeout = false;

		if (rt == NULL || rt->acq != SLECG_ACQ_RUNNING) {
			sample_count = 0;
			first_ts = 0;
			last_read_tick = 0;
			while (xSemaphoreTake(s_drdy_sem, 0) == pdTRUE) {
			}
			vTaskDelay(pdMS_TO_TICKS(20));
			continue;
		}

		wait_next_sample(&from_timeout);

		/* 额外限速：即使 DRDY 卡在低电平，也不超过标称采样率 */
		{
			const TickType_t min_period = pdMS_TO_TICKS(
				(1000U + ADS129X_SAMPLE_RATE_HZ - 1U) / ADS129X_SAMPLE_RATE_HZ);
			TickType_t now = xTaskGetTickCount();
			if (last_read_tick != 0 && (now - last_read_tick) < min_period) {
				vTaskDelay(min_period - (now - last_read_tick));
			}
			last_read_tick = xTaskGetTickCount();
		}

		ads129x_sample_t sample;
		if (ads129x_read_frame(&sample) != 0) {
			s_reads_fail++;
			slecg_fsm_set_error(SLECG_ERR_SPI);
			continue;
		}
		s_reads_ok++;

		/* 保留原始 24bit 扩展值便于停止后诊断（是否真全 0） */
		{
			const uint8_t *r = sample.raw_ch1;
			int32_t v = ((int32_t)r[0] << 16) | ((int32_t)r[1] << 8) | r[2];
			if ((v & 0x00800000) != 0) {
				v |= (int32_t)0xFF000000;
			}
			s_last_raw32 = v;
			s_last_ch1 = sample.ch1_value;
			s_last_status[0] = sample.status[0];
			s_last_status[1] = sample.status[1];
			s_last_status[2] = sample.status[2];
			if (v != 0 || sample.ch1_value != 0) {
				s_nonzero_samples++;
			}
		}

		ble_debug_log_sample(&sample, from_timeout);

		if (sample_count == 0) {
			first_ts = now_ms();
		}
		sample_buf[sample_count] = sample.ch1_value;
		last_loff = sample.loff_status;
		sample_count++;

		if (sample_count < SLECG_ECG_SAMPLES_PER_PKT) {
			continue;
		}

		/* 仅在发送成功后再消耗序号，避免 TX 失败时上位机误计“丢包” */
		const uint16_t seq = slecg_fsm_peek_ecg_seq();
		slecg_ecg_payload_in_t pkt = {
			.seq = seq,
			.ts_ms = first_ts,
			.loff = last_loff,
			.samples = sample_buf,
			.n_samples = SLECG_ECG_SAMPLES_PER_PKT,
		};

		size_t frame_len = slecg_proto_build_ecg_frame(&pkt, frame, sizeof(frame));
		sample_count = 0;
		first_ts = 0;

		if (frame_len == 0) {
			s_frames_fail++;
			continue;
		}

		rt = slecg_fsm_get_runtime();
		if (rt == NULL || rt->acq != SLECG_ACQ_RUNNING) {
			continue;
		}

		if (slecg_transport_send_data(rt, frame, frame_len) != ESP_OK) {
			s_frames_fail++;
			slecg_fsm_set_error(SLECG_ERR_TX_QUEUE_FULL);
			if (rt->transport == SLECG_TRANSPORT_BLE) {
				ESP_LOGW(TAG, "BLE Notify 发送失败 seq=%u", (unsigned)seq);
			}
		} else {
			(void)slecg_fsm_next_ecg_seq();
			s_frames_sent++;
		}
	}
}

void slecg_ecg_start(void)
{
	s_drdy_sem = xSemaphoreCreateCounting(8, 0);
	if (s_drdy_sem == NULL) {
		ESP_LOGE(TAG, "DRDY 信号量创建失败");
		return;
	}

	slecg_ecg_reset_stats();
	drdy_gpio_init();
	xTaskCreate(ecg_task, "slecg_ecg", SLECG_ECG_TASK_STACK, NULL, SLECG_ECG_TASK_PRIO, NULL);
	ESP_LOGI(TAG, "ECG 任务已启动 (DRDY=GPIO%d, rate=%u Hz)",
		 BOARD_ADS_DRDY_GPIO, (unsigned)ADS129X_SAMPLE_RATE_HZ);
}
