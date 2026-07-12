/**
 * @file ads129x.c
 * @brief ADS1291/ADS1292/ADS1292R analog front-end driver for ESP-IDF (ESP32S3).
 */

#include "ads129x.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"

static const char *TAG = "ads129x";

#define ADS129X_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) ((void)(x))
#endif

#define ADS129X_REGISTER_COUNT      12

#define ADS129X_POWER_SETTLE_MS           200
#define ADS129X_RESET_PULSE_MS            20
#define ADS129X_RESET_SETTLE_MS           50
#define ADS129X_REFBUF_SETTLE_MS          150
#define ADS129X_START_INIT_SETTLE_MS      10
#define ADS129X_RESET_COMMAND_SETTLE_MS   100
#define ADS129X_INIT_ID_RETRY             3

#define ADS129X_START_GUARD_US            1000
#define ADS129X_RDATAC_GUARD_US           10
#define ADS129X_COMMAND_PRE_GUARD_US      100
#define ADS129X_COMMAND_GUARD_US          1000
#define ADS129X_DRDY_HIGH_TIMEOUT_CYCLE   1000

#define ADS129X_HP_ALPHA_Q 31

typedef struct {
	int32_t prev_in;
	int32_t prev_out;
} ads129x_hp_state_t;

typedef struct {
	float x1;
	float x2;
	float y1;
	float y2;
} ads129x_notch_state_t;

typedef struct {
	float prev_out;
} ads129x_lp_state_t;

static spi_device_handle_t s_spi_dev;
static bool s_spi_bus_initialized;
static uint32_t s_spi_freq_hz;

static ads129x_hp_state_t s_hp_ch1 = { 0, 0 };
#if ADS129X_HAS_CH2
static ads129x_hp_state_t s_hp_ch2 = { 0, 0 };
#endif
static int32_t s_hp_alpha_q31;

static ads129x_notch_state_t s_notch_ch1;
#if ADS129X_HAS_CH2
static ads129x_notch_state_t s_notch_ch2;
#endif
static float s_notch_b0;
static float s_notch_b1;
static float s_notch_b2;
static float s_notch_a1;
static float s_notch_a2;

static ads129x_lp_state_t s_lp_ch1;
#if ADS129X_HAS_CH2
static ads129x_lp_state_t s_lp_ch2;
#endif
static float s_lp_alpha;

static void ads129x_delay_ms(uint32_t ms)
{
	vTaskDelay(pdMS_TO_TICKS(ms));
}

static void ads129x_delay_us(uint32_t us)
{
	esp_rom_delay_us(us);
}

static void ads129x_force_gpio_out(gpio_num_t pin, int level)
{
	gpio_hold_dis(pin);
	esp_rom_gpio_connect_out_signal((uint32_t)pin, SIG_GPIO_OUT_IDX, false, false);
	(void)gpio_set_direction(pin, GPIO_MODE_OUTPUT);
	(void)gpio_pulldown_dis(pin);
	(void)gpio_pullup_dis(pin);
	gpio_sleep_sel_dis(pin);
	gpio_set_level(pin, level);
}

static void ads129x_pwdn_set_active(void)
{
	ads129x_force_gpio_out(BOARD_ADS_PWDN_GPIO, 0);
}

static void ads129x_pwdn_set_inactive(void)
{
	ads129x_force_gpio_out(BOARD_ADS_PWDN_GPIO, 1);
}

static void ads129x_start_set_inactive(void)
{
	ads129x_force_gpio_out(BOARD_ADS_START_GPIO, 0);
}

/* 防止 light-sleep / modem-sleep 自动改写 ADS 控制脚（否则 PWDN 会被拉低导致读数全 0） */
static void ads129x_disable_gpio_sleep_switch(void)
{
	const gpio_num_t pins[] = {
		BOARD_ADS_DRDY_GPIO,
		BOARD_ADS_MISO_GPIO,
		BOARD_ADS_SCLK_GPIO,
		BOARD_ADS_MOSI_GPIO,
		BOARD_ADS_CS_GPIO,
		BOARD_ADS_START_GPIO,
		BOARD_ADS_PWDN_GPIO,
	};

	for (size_t i = 0; i < ADS129X_ARRAY_SIZE(pins); ++i) {
		gpio_sleep_sel_dis(pins[i]);
	}
}

static void ads129x_reclaim_ctrl_pins(void)
{
	ads129x_force_gpio_out(BOARD_ADS_START_GPIO, 0);
	ads129x_force_gpio_out(BOARD_ADS_PWDN_GPIO, 1); /* /PWDN 高 = 运行 */
}

static int ads129x_spi_set_frequency(uint32_t freq_hz)
{
	esp_err_t err;

	/* REG/DATA 同频时禁止 remove/add，否则易把控制脚搅乱 */
	if ((s_spi_dev != NULL) && (s_spi_freq_hz == freq_hz)) {
		return 0;
	}

	if (s_spi_dev != NULL) {
		err = spi_bus_remove_device(s_spi_dev);
		if (err != ESP_OK) {
			return -EIO;
		}
		s_spi_dev = NULL;
	}

	spi_device_interface_config_t devcfg = {
		.clock_speed_hz = freq_hz,
		.mode = 1, /* CPOL=0, CPHA=1，ADS129x 要求 */
		.spics_io_num = BOARD_ADS_CS_GPIO,
		.queue_size = 1,
		.cs_ena_pretrans = 4,
		.cs_ena_posttrans = 4,
	};

	err = spi_bus_add_device(ADS129X_SPI_HOST, &devcfg, &s_spi_dev);
	if (err != ESP_OK) {
		return -EIO;
	}

	s_spi_freq_hz = freq_hz;
	ads129x_reclaim_ctrl_pins();
	return 0;
}

static int ads129x_spi_transceive(const uint8_t *tx_buf, uint8_t *rx_buf, size_t len)
{
	uint8_t discard[32];
	spi_transaction_t trans = { 0 };

	if (s_spi_dev == NULL) {
		return -ENODEV;
	}
	if ((len == 0U) || (len > sizeof(discard))) {
		return -EINVAL;
	}

	trans.length = len * 8U;
	trans.rxlength = len * 8U;
	trans.tx_buffer = tx_buf;
	/* 短传输 + DMA 时 NULL rx 容易读到全 0；始终提供 RX 缓冲并用 polling */
	trans.rx_buffer = (rx_buf != NULL) ? rx_buf : discard;

	if (spi_device_polling_transmit(s_spi_dev, &trans) != ESP_OK) {
		return -EIO;
	}

	return 0;
}

static int ads129x_spi_write(const uint8_t *tx_buf, size_t len)
{
	return ads129x_spi_transceive(tx_buf, NULL, len);
}

static int ads129x_wait_drdy_inactive(void);
static uint32_t ads129x_sample_rate_from_config1(uint8_t config1_rate);

static void ads129x_highpass_reset(uint32_t sample_rate_hz);
static int32_t ads129x_highpass_step(ads129x_hp_state_t *state, int32_t input);
static void ads129x_notch_reset(uint32_t sample_rate_hz);
static int32_t ads129x_notch_step(ads129x_notch_state_t *state, int32_t input);
static void ads129x_lowpass_reset(uint32_t sample_rate_hz);
static int32_t ads129x_lowpass_step(ads129x_lp_state_t *state, int32_t input);
static int32_t ads129x_clamp24(int32_t v);
static int32_t ads129x_apply_filters(int32_t raw32,
				     ads129x_hp_state_t *hp,
				     ads129x_notch_state_t *notch,
				     ads129x_lp_state_t *lp);

static int32_t ads129x_raw24_to_int32(const uint8_t raw[ADS129X_CHANNEL_SIZE]);
static void ads129x_int32_to_24bit_bytes(int32_t value, uint8_t out[ADS129X_CHANNEL_SIZE]);
static int16_t ads129x_int32_to_int16(int32_t value);

static uint8_t ads129x_loff_from_status(const uint8_t status[ADS129X_CHANNEL_SIZE]);

int ads129x_init(void)
{
	uint8_t regs[ADS129X_REGISTER_COUNT] = { 0 };
	int ret;
	esp_err_t err;

#if ADS129X_IS_ADS1291
	ESP_LOGI(TAG, "ADS129x: ADS1291 (1ch)");
#elif ADS129X_IS_ADS1292R
	ESP_LOGI(TAG, "ADS129x: ADS1292R (2ch + resp)");
#else
	ESP_LOGI(TAG, "ADS129x: ADS1292 (2ch)");
#endif

	/* 先固定控制脚，避免 SPI 初始化期间 PWDN/START 浮空 */
	gpio_config_t gpio_out = {
		.pin_bit_mask = (1ULL << BOARD_ADS_START_GPIO) | (1ULL << BOARD_ADS_PWDN_GPIO),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	err = gpio_config(&gpio_out);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "GPIO output configure failed: %s", esp_err_to_name(err));
		return -EIO;
	}

	gpio_config_t gpio_in = {
		.pin_bit_mask = (1ULL << BOARD_ADS_DRDY_GPIO),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	err = gpio_config(&gpio_in);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "GPIO input configure failed: %s", esp_err_to_name(err));
		return -EIO;
	}

	ads129x_start_set_inactive();
	ads129x_pwdn_set_active(); /* 先保持复位，再起 SPI */
	ads129x_disable_gpio_sleep_switch();

	if (!s_spi_bus_initialized) {
		spi_bus_config_t buscfg = {
			.mosi_io_num = BOARD_ADS_MOSI_GPIO,
			.miso_io_num = BOARD_ADS_MISO_GPIO,
			.sclk_io_num = BOARD_ADS_SCLK_GPIO,
			.quadwp_io_num = -1,
			.quadhd_io_num = -1,
			.max_transfer_sz = 32,
			.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
		};

		/* 短帧寄存器读写禁用 DMA，避免 MISO 读回全 0 */
		err = spi_bus_initialize(ADS129X_SPI_HOST, &buscfg, SPI_DMA_DISABLED);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
			return -EIO;
		}
		s_spi_bus_initialized = true;
	}

	ret = ads129x_spi_set_frequency(ADS129X_SPI_FREQ_REG);
	if (ret < 0) {
		ESP_LOGE(TAG, "SPI device init failed at reg speed");
		return ret;
	}

	/* spi_set_frequency 会 reclaim 并把 PWDN 拉高；这里再做一次标准复位脉冲 */
	ads129x_pwdn_set_active();
	ads129x_delay_ms(ADS129X_RESET_PULSE_MS);
	ads129x_pwdn_set_inactive();
	ads129x_delay_ms(ADS129X_RESET_SETTLE_MS);
	(void)ads129x_send_command(ADS129X_CMD_RESET);
	ads129x_delay_ms(ADS129X_RESET_COMMAND_SETTLE_MS);

	for (int attempt = 1; attempt <= ADS129X_INIT_ID_RETRY; ++attempt) {
		ret = ads129x_configure_default();
		if (ret < 0) {
			ESP_LOGE(TAG, "register configure failed (try %d): %d", attempt, ret);
			return ret;
		}

		ret = ads129x_rreg(ADS129X_REG_ID, sizeof(regs), regs);
		if (ret < 0) {
			ESP_LOGW(TAG, "register dump failed (try %d): %d", attempt, ret);
		} else {
			ESP_LOGI(TAG,
				 "寄存器读回(try %d): ID=0x%02X CONFIG1=0x%02X CONFIG2=0x%02X "
				 "LOFF=0x%02X CH1SET=0x%02X CH2SET=0x%02X RLD=0x%02X LOFF_SENS=0x%02X",
				 attempt,
				 regs[0], regs[1], regs[2], regs[3],
				 regs[4], regs[5], regs[6], regs[7]);
			ESP_LOG_BUFFER_HEX_LEVEL(TAG, regs, sizeof(regs), ESP_LOG_INFO);

			if ((regs[0] == ADS129X_ID_DEFAULT) &&
			    (regs[4] == ADS129X_CH1SET_DEFAULT)) {
				break;
			}

			ESP_LOGW(TAG,
				 "ADS 配置未生效: ID=0x%02X(期望0x%02X) CH1SET=0x%02X(期望0x%02X)",
				 regs[0], ADS129X_ID_DEFAULT,
				 regs[4], ADS129X_CH1SET_DEFAULT);
		}

		if (attempt == ADS129X_INIT_ID_RETRY) {
			/*
			 * 历史现象：启动期 RREG 偶发全 0，但后续 RDATAC 仍可出数。
			 * 不再硬失败阻断采集；写寄存器仍已执行，仅标记配置未验证。
			 */
			ESP_LOGW(TAG,
				 "ADS1291 寄存器读回校验失败，继续启动（请检查 DOUT/GPIO46 与供电）");
			break;
		}

		ads129x_pwdn_set_active();
		ads129x_delay_ms(ADS129X_RESET_PULSE_MS);
		ads129x_pwdn_set_inactive();
		ads129x_delay_ms(ADS129X_RESET_SETTLE_MS);
		(void)ads129x_send_command(ADS129X_CMD_RESET);
		ads129x_delay_ms(ADS129X_RESET_COMMAND_SETTLE_MS);
	}

	ads129x_highpass_reset(ads129x_sample_rate_from_config1(ADS129X_CONFIG1_DEFAULT));
	ads129x_notch_reset(ads129x_sample_rate_from_config1(ADS129X_CONFIG1_DEFAULT));
	ads129x_lowpass_reset(ads129x_sample_rate_from_config1(ADS129X_CONFIG1_DEFAULT));

	ESP_LOGI(TAG,
		 "AFE: rate=%u Hz CH1SET=0x%02X(读回0x%02X) HP=%d(%.2fHz) Notch=%d(%.0fHz) "
		 "LP=%d(%.0fHz) RLD=0x%02X LOFF_SENS=0x%02X shift=%u",
		 (unsigned)ADS129X_SAMPLE_RATE_HZ,
		 ADS129X_CH1SET_DEFAULT, regs[4],
		 ADS129X_HIGHPASS_ENABLE, (double)ADS129X_HIGHPASS_CUTOFF_HZ,
		 ADS129X_NOTCH_ENABLE, (double)ADS129X_NOTCH_FREQ_HZ,
		 ADS129X_LOWPASS_ENABLE, (double)ADS129X_LOWPASS_CUTOFF_HZ,
		 ADS129X_RLD_SENS_DEFAULT,
		 ADS129X_LOFF_SENS_DEFAULT,
		 (unsigned)ADS129X_RAW_OUTPUT_SHIFT);

	ret = ads129x_spi_set_frequency(ADS129X_SPI_FREQ_DATA);
	if (ret < 0) {
		ESP_LOGE(TAG, "SPI switch to data speed failed");
		return ret;
	}
	ads129x_reclaim_ctrl_pins();
	ads129x_disable_gpio_sleep_switch();
	ESP_LOGI(TAG, "ADS129x SPI host=%d freq=%u Hz, PWDN=%d START=%d",
		 (int)ADS129X_SPI_HOST,
		 (unsigned)s_spi_freq_hz,
		 gpio_get_level(BOARD_ADS_PWDN_GPIO),
		 gpio_get_level(BOARD_ADS_START_GPIO));
	/*
	 * 若寄存器已校验通过，不要因 get_level 仍为 0 而硬失败：
	 * 本板存在读回与真实驱动不一致的情况；ID=0x52 已证明芯片可通信。
	 */
	if (gpio_get_level(BOARD_ADS_PWDN_GPIO) == 0) {
		if (regs[0] == ADS129X_ID_DEFAULT) {
			ESP_LOGW(TAG,
				 "PWDN 读回为低，但 ID 校验已通过，继续启动（请用表笔确认 GPIO13）");
		} else {
			ESP_LOGE(TAG, "PWDN 仍为低且寄存器未校验通过");
			return -EIO;
		}
	}

	return 0;
}

void ads129x_ensure_powered(void)
{
	ads129x_reclaim_ctrl_pins();
	ads129x_disable_gpio_sleep_switch();
}

int ads129x_stop(void)
{
	int ret;

	ESP_LOGI(TAG, "stop: 退出连续转换");
	ads129x_log_pins("stop前");

	ret = ads129x_send_command(ADS129X_CMD_STOP);
	ads129x_delay_ms(1);
	ret |= ads129x_send_command(ADS129X_CMD_SDATAC);
	ads129x_start_set_inactive();

	ads129x_log_pins("stop后");
	(void)ads129x_log_registers("stop后读回");
	ESP_LOGI(TAG, "stop: 完成 ret=%d", ret);
	return ret;
}

int ads129x_read_frame(ads129x_sample_t *sample)
{
	uint8_t tx_buf[ADS129X_FRAME_SIZE] = { 0 };
	uint8_t rx_buf[ADS129X_FRAME_SIZE] = { 0 };
	int ret;
	int32_t ch1_raw32;
#if ADS129X_HAS_CH2
	int32_t ch2_raw32;
#endif

	if (sample == NULL) {
		return -EINVAL;
	}

	ret = ads129x_spi_transceive(tx_buf, rx_buf, sizeof(tx_buf));
	if (ret < 0) {
		return ret;
	}

	memcpy(sample->status, rx_buf, sizeof(sample->status));
	memcpy(sample->raw_ch1, &rx_buf[3], sizeof(sample->raw_ch1));
#if ADS129X_HAS_CH2
	memcpy(sample->raw_ch2, &rx_buf[6], sizeof(sample->raw_ch2));
#endif
	sample->loff_status = ads129x_loff_from_status(sample->status);

	ch1_raw32 = ads129x_raw24_to_int32(sample->raw_ch1);
	ch1_raw32 = ads129x_apply_filters(ch1_raw32, &s_hp_ch1, &s_notch_ch1, &s_lp_ch1);
	if ((ADS129X_HIGHPASS_ENABLE != 0) || (ADS129X_NOTCH_ENABLE != 0) ||
	    (ADS129X_LOWPASS_ENABLE != 0)) {
		ads129x_int32_to_24bit_bytes(ch1_raw32, sample->raw_ch1);
	}
	sample->ch1_value = ads129x_int32_to_int16(ch1_raw32 >> ADS129X_RAW_OUTPUT_SHIFT);
#if ADS129X_HAS_CH2
	ch2_raw32 = ads129x_raw24_to_int32(sample->raw_ch2);
	ch2_raw32 = ads129x_apply_filters(ch2_raw32, &s_hp_ch2, &s_notch_ch2, &s_lp_ch2);
	if ((ADS129X_HIGHPASS_ENABLE != 0) || (ADS129X_NOTCH_ENABLE != 0) ||
	    (ADS129X_LOWPASS_ENABLE != 0)) {
		ads129x_int32_to_24bit_bytes(ch2_raw32, sample->raw_ch2);
	}
	sample->ch2_value = ads129x_int32_to_int16(ch2_raw32 >> ADS129X_RAW_OUTPUT_SHIFT);
#endif

	return 0;
}

int ads129x_drdy_pin(void)
{
	return (int)BOARD_ADS_DRDY_GPIO;
}

bool ads129x_is_data_ready(void)
{
	return gpio_get_level(BOARD_ADS_DRDY_GPIO) == 0;
}

void ads129x_log_pins(const char *why)
{
	ESP_LOGI(TAG,
		 "ADS引脚[%s]: DRDY(GPIO%d)=%d START(GPIO%d)=%d PWDN(GPIO%d)=%d SPI=%lu Hz rate=%u",
		 (why != NULL) ? why : "-",
		 (int)BOARD_ADS_DRDY_GPIO, gpio_get_level(BOARD_ADS_DRDY_GPIO),
		 (int)BOARD_ADS_START_GPIO, gpio_get_level(BOARD_ADS_START_GPIO),
		 (int)BOARD_ADS_PWDN_GPIO, gpio_get_level(BOARD_ADS_PWDN_GPIO),
		 (unsigned long)s_spi_freq_hz,
		 (unsigned)ADS129X_SAMPLE_RATE_HZ);
}

int ads129x_log_registers(const char *why)
{
	uint8_t regs[ADS129X_REGISTER_COUNT] = { 0 };
	int ret;

	ret = ads129x_rreg(ADS129X_REG_ID, ADS129X_REGISTER_COUNT, regs);
	if (ret != 0) {
		ESP_LOGE(TAG, "寄存器读回失败[%s]: %d", (why != NULL) ? why : "-", ret);
		return ret;
	}

	ESP_LOGI(TAG,
		 "寄存器[%s]: ID=0x%02X CONFIG1=0x%02X CONFIG2=0x%02X LOFF=0x%02X "
		 "CH1SET=0x%02X CH2SET=0x%02X RLD=0x%02X LOFF_SENS=0x%02X "
		 "LOFF_STAT=0x%02X RESP1=0x%02X RESP2=0x%02X GPIO=0x%02X",
		 (why != NULL) ? why : "-",
		 regs[0], regs[1], regs[2], regs[3],
		 regs[4], regs[5], regs[6], regs[7],
		 regs[8], regs[9], regs[10], regs[11]);

	if (regs[0] != ADS129X_ID_DEFAULT) {
		ESP_LOGW(TAG, "ID 异常: 读到 0x%02X, 期望 0x%02X (SPI/接线可疑)",
			 regs[0], ADS129X_ID_DEFAULT);
	}
	return 0;
}

void ads129x_log_drdy_activity(uint32_t window_ms)
{
	uint32_t high = 0;
	uint32_t low = 0;
	uint32_t edges = 0;
	int prev = gpio_get_level(BOARD_ADS_DRDY_GPIO);
	int64_t t_end = esp_timer_get_time() + ((int64_t)window_ms * 1000LL);

	while (esp_timer_get_time() < t_end) {
		int lvl = gpio_get_level(BOARD_ADS_DRDY_GPIO);
		if (lvl != 0) {
			high++;
		} else {
			low++;
		}
		if (lvl != prev) {
			edges++;
			prev = lvl;
		}
		esp_rom_delay_us(50);
	}

	ESP_LOGI(TAG,
		 "DRDY活动[%lums]: high=%lu low=%lu edges=%lu ready_now=%d",
		 (unsigned long)window_ms,
		 (unsigned long)high,
		 (unsigned long)low,
		 (unsigned long)edges,
		 ads129x_is_data_ready() ? 1 : 0);
}

void ads129x_log_sample(const ads129x_sample_t *sample, const char *why)
{
	int32_t raw32;

	if (sample == NULL) {
		return;
	}

	raw32 = ads129x_raw24_to_int32(sample->raw_ch1);
	ESP_LOGI(TAG,
		 "样本[%s]: status=%02X %02X %02X raw24=%02X %02X %02X raw32=%ld ch1=%d loff=0x%02X DRDY=%d",
		 (why != NULL) ? why : "-",
		 sample->status[0], sample->status[1], sample->status[2],
		 sample->raw_ch1[0], sample->raw_ch1[1], sample->raw_ch1[2],
		 (long)raw32,
		 (int)sample->ch1_value,
		 sample->loff_status,
		 gpio_get_level(BOARD_ADS_DRDY_GPIO));
}

int ads129x_configure_default(void)
{
	int ret;
	const uint8_t config2_settle = ADS129X_CONFIG2_DEFAULT;
	const uint8_t config[] = {
		ADS129X_CONFIG1_DEFAULT,
		ADS129X_CONFIG2_DEFAULT,
	};
	const uint8_t loff = ADS129X_LOFF_DEFAULT;
	const uint8_t chset[] = {
		ADS129X_CH1SET_DEFAULT,
		ADS129X_CH2SET_DEFAULT,
	};
	const uint8_t rld_sens = ADS129X_RLD_SENS_DEFAULT;
	const uint8_t loff_sens = ADS129X_LOFF_SENS_DEFAULT;
	const uint8_t resp[] = {
		ADS129X_RESP1_DEFAULT,
		ADS129X_RESP2_DEFAULT,
	};
	const uint8_t gpio = ADS129X_GPIO_DEFAULT;

	ret = ads129x_send_command(ADS129X_CMD_SDATAC);
	if (ret < 0) {
		return ret;
	}
	ads129x_delay_us(ADS129X_COMMAND_GUARD_US);

	ret = ads129x_wreg(ADS129X_REG_CONFIG2, 1, &config2_settle);
	if (ret < 0) {
		return ret;
	}
	ads129x_delay_ms(ADS129X_REFBUF_SETTLE_MS);

	ret = ads129x_wreg(ADS129X_REG_CONFIG1, ADS129X_ARRAY_SIZE(config), config);
	if (ret < 0) {
		return ret;
	}

	ret = ads129x_wreg(ADS129X_REG_LOFF, 1, &loff);
	if (ret < 0) {
		return ret;
	}

	ret = ads129x_wreg(ADS129X_REG_CH1SET, ADS129X_ARRAY_SIZE(chset), chset);
	if (ret < 0) {
		return ret;
	}

	ret = ads129x_wreg(ADS129X_REG_RLD_SENS, 1, &rld_sens);
	if (ret < 0) {
		return ret;
	}

	ret = ads129x_wreg(ADS129X_REG_LOFF_SENS, 1, &loff_sens);
	if (ret < 0) {
		return ret;
	}

	ret = ads129x_wreg(ADS129X_REG_RESP1, ADS129X_ARRAY_SIZE(resp), resp);
	if (ret < 0) {
		return ret;
	}

	ret = ads129x_wreg(ADS129X_REG_GPIO, 1, &gpio);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

int ads129x_init_start(void)
{
	int ret;

	ESP_LOGI(TAG, "init_start: 准备首次进入 RDATAC, spi=%lu Hz",
		 (unsigned long)s_spi_freq_hz);
	ads129x_pwdn_set_inactive();
	ads129x_disable_gpio_sleep_switch();
	ads129x_log_pins("init_start前");

	if (s_spi_freq_hz != ADS129X_SPI_FREQ_DATA) {
		ret = ads129x_spi_set_frequency(ADS129X_SPI_FREQ_DATA);
		if (ret < 0) {
			ESP_LOGE(TAG, "init_start: 切数据速率失败");
			return ret;
		}
		ESP_LOGI(TAG, "init_start: SPI 已切到 %lu Hz", (unsigned long)s_spi_freq_hz);
	}

	ads129x_start_set_inactive();

	ret = ads129x_send_command(ADS129X_CMD_START);
	if (ret < 0) {
		ESP_LOGE(TAG, "init_start: START 命令失败 %d", ret);
		return ret;
	}
	ads129x_delay_ms(ADS129X_START_INIT_SETTLE_MS);

	ret = ads129x_send_command(ADS129X_CMD_RDATAC);
	if (ret < 0) {
		ESP_LOGE(TAG, "init_start: RDATAC 失败 %d", ret);
		return ret;
	}
	ads129x_delay_us(ADS129X_RDATAC_GUARD_US);

	ads129x_log_pins("init_start后");
	ads129x_log_drdy_activity(80);
	ESP_LOGI(TAG, "init_start: 完成");
	return 0;
}

int ads129x_start(void)
{
	int ret;

	ESP_LOGI(TAG, "start: 再次进入 RDATAC, spi=%lu Hz",
		 (unsigned long)s_spi_freq_hz);
	ads129x_pwdn_set_inactive();
	ads129x_disable_gpio_sleep_switch();
	ads129x_log_pins("start前");

	if (s_spi_freq_hz != ADS129X_SPI_FREQ_DATA) {
		ret = ads129x_spi_set_frequency(ADS129X_SPI_FREQ_DATA);
		if (ret < 0) {
			ESP_LOGE(TAG, "start: 切数据速率失败");
			return ret;
		}
	}

	ads129x_start_set_inactive();

	ret = ads129x_send_command(ADS129X_CMD_START);
	if (ret < 0) {
		ESP_LOGE(TAG, "start: START 命令失败 %d", ret);
		return ret;
	}
	ads129x_delay_us(ADS129X_START_GUARD_US);

	ret = ads129x_send_command(ADS129X_CMD_RDATAC);
	if (ret < 0) {
		ESP_LOGE(TAG, "start: RDATAC 失败 %d", ret);
		return ret;
	}
	ads129x_delay_us(ADS129X_RDATAC_GUARD_US);

	ads129x_log_pins("start后");
	ads129x_log_drdy_activity(80);
	ESP_LOGI(TAG, "start: 完成");
	return 0;
}

int ads129x_set_sampling_rate(uint8_t rate)
{
	uint8_t config1;
	int ret;

	if (rate > ADS129X_CONFIG1_8000SPS) {
		return -EINVAL;
	}

	ret = ads129x_rreg(ADS129X_REG_CONFIG1, 1, &config1);
	if (ret < 0) {
		return ret;
	}

	config1 = (uint8_t)((config1 & 0xF8U) | (rate & 0x07U));
	ret = ads129x_wreg(ADS129X_REG_CONFIG1, 1, &config1);
	if (ret < 0) {
		return ret;
	}
	ads129x_highpass_reset(ads129x_sample_rate_from_config1(rate));
	ads129x_notch_reset(ads129x_sample_rate_from_config1(rate));
	ads129x_lowpass_reset(ads129x_sample_rate_from_config1(rate));

	ret = ads129x_send_command(ADS129X_CMD_RDATAC);
	if (ret < 0) {
		return ret;
	}
	ads129x_delay_us(ADS129X_RDATAC_GUARD_US);

	return 0;
}

int ads129x_set_pga_gain(uint8_t ch1_gain, uint8_t ch2_gain)
{
	uint8_t chset[2];
	int ret;

	if ((ch1_gain > ADS129X_CH1SET_GAIN_12) || (ch2_gain > ADS129X_CH2SET_GAIN_12)) {
		return -EINVAL;
	}

	ret = ads129x_rreg(ADS129X_REG_CH1SET, ADS129X_ARRAY_SIZE(chset), chset);
	if (ret < 0) {
		return ret;
	}

	chset[0] = (uint8_t)((chset[0] & 0x8FU) | (ch1_gain & 0x70U));
#if ADS129X_HAS_CH2
	chset[1] = (uint8_t)((chset[1] & 0x8FU) | (ch2_gain & 0x70U));
#else
	ARG_UNUSED(ch2_gain);
	chset[1] = ADS129X_CH2SET_POWERDOWN_SHORT;
#endif

	ret = ads129x_wreg(ADS129X_REG_CH1SET, ADS129X_ARRAY_SIZE(chset), chset);
	if (ret < 0) {
		return ret;
	}

	ret = ads129x_send_command(ADS129X_CMD_RDATAC);
	if (ret < 0) {
		return ret;
	}
	ads129x_delay_us(ADS129X_RDATAC_GUARD_US);

	return 0;
}

int ads129x_set_rld(uint8_t enable, uint8_t rld_inputs, uint8_t loff_sense)
{
	uint8_t rld_sens;
	int ret;

	ret = ads129x_rreg(ADS129X_REG_RLD_SENS, 1, &rld_sens);
	if (ret < 0) {
		return ret;
	}

	rld_sens &= 0xC0U;
	if (enable != 0U) {
		rld_sens |= (uint8_t)(ADS129X_RLD_BUFFER_ENABLED | ((loff_sense & 0x01U) << 4) |
				      (rld_inputs & 0x0FU));
	}

	ret = ads129x_wreg(ADS129X_REG_RLD_SENS, 1, &rld_sens);
	if (ret < 0) {
		return ret;
	}

	ret = ads129x_send_command(ADS129X_CMD_RDATAC);
	if (ret < 0) {
		return ret;
	}
	ads129x_delay_us(ADS129X_RDATAC_GUARD_US);

	return 0;
}

int ads129x_set_vref(uint8_t vref_4v)
{
	uint8_t config2;
	int ret;

	ret = ads129x_rreg(ADS129X_REG_CONFIG2, 1, &config2);
	if (ret < 0) {
		return ret;
	}

	config2 |= (uint8_t)(ADS129X_CONFIG2_FIXED_1 | ADS129X_CONFIG2_PDB_REFBUF_ENABLED);
	if (vref_4v != 0U) {
		config2 |= ADS129X_CONFIG2_VREF_4V;
	} else {
		config2 &= (uint8_t)~ADS129X_CONFIG2_VREF_4V;
	}

	ret = ads129x_wreg(ADS129X_REG_CONFIG2, 1, &config2);
	if (ret < 0) {
		return ret;
	}

	ret = ads129x_send_command(ADS129X_CMD_RDATAC);
	if (ret < 0) {
		return ret;
	}
	ads129x_delay_us(ADS129X_RDATAC_GUARD_US);

	return 0;
}

int ads129x_send_command(uint8_t command)
{
	int ret;

	if ((command != ADS129X_CMD_START) && (command != ADS129X_CMD_STOP)) {
		(void)ads129x_wait_drdy_inactive();
		ads129x_delay_us(ADS129X_COMMAND_PRE_GUARD_US);
	}

	ret = ads129x_spi_write(&command, sizeof(command));
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static uint32_t ads129x_sample_rate_from_config1(uint8_t config1_rate)
{
	switch (config1_rate & 0x07U) {
	case ADS129X_CONFIG1_125SPS:
		return 125U;
	case ADS129X_CONFIG1_250SPS:
		return 250U;
	case ADS129X_CONFIG1_500SPS:
		return 500U;
	case ADS129X_CONFIG1_1000SPS:
		return 1000U;
	case ADS129X_CONFIG1_2000SPS:
		return 2000U;
	case ADS129X_CONFIG1_4000SPS:
		return 4000U;
	case ADS129X_CONFIG1_8000SPS:
		return 8000U;
	default:
		return 250U;
	}
}

static void ads129x_highpass_reset(uint32_t sample_rate_hz)
{
	uint64_t fs_uhz = (uint64_t)sample_rate_hz * 1000000ULL;
	uint64_t fc_uhz = (uint64_t)((ADS129X_HIGHPASS_CUTOFF_HZ * 1000000.0f) + 0.5f);
	uint64_t two_pi_fc_uhz = ((fc_uhz * 6283185ULL) + 500000ULL) / 1000000ULL;
	uint64_t denom;

	if (two_pi_fc_uhz == 0ULL) {
		s_hp_alpha_q31 = (int32_t)0x7FFFFFFF;
	} else {
		denom = fs_uhz + two_pi_fc_uhz;
		s_hp_alpha_q31 = (int32_t)(((uint64_t)0x7FFFFFFFULL * fs_uhz + (denom / 2ULL)) / denom);
	}

	s_hp_ch1.prev_in = 0;
	s_hp_ch1.prev_out = 0;
#if ADS129X_HAS_CH2
	s_hp_ch2.prev_in = 0;
	s_hp_ch2.prev_out = 0;
#endif
}

static int32_t ads129x_highpass_step(ads129x_hp_state_t *state, int32_t input)
{
	int64_t diff = (int64_t)state->prev_out + input - state->prev_in;
	int32_t output = (int32_t)(((int64_t)s_hp_alpha_q31 * diff) >> ADS129X_HP_ALPHA_Q);

	state->prev_in = input;
	state->prev_out = output;

	return output;
}

static void ads129x_notch_reset(uint32_t sample_rate_hz)
{
	float fs;
	float w0;
	float c;
	float r;

	if (sample_rate_hz == 0U) {
		sample_rate_hz = ADS129X_SAMPLE_RATE_HZ;
	}
	fs = (float)sample_rate_hz;
	/* 陷波频率须低于 Nyquist */
	if (ADS129X_NOTCH_FREQ_HZ >= (fs * 0.5f)) {
		ESP_LOGW(TAG, "Notch %.1f Hz >= Nyquist(%.1f)，跳过系数更新",
			 (double)ADS129X_NOTCH_FREQ_HZ, (double)(fs * 0.5f));
		return;
	}

	w0 = 2.0f * (float)M_PI * (ADS129X_NOTCH_FREQ_HZ / fs);
	c = cosf(w0);
	r = ADS129X_NOTCH_R;
	if (r < 0.80f) {
		r = 0.80f;
	} else if (r > 0.995f) {
		r = 0.995f;
	}

	s_notch_b0 = 1.0f;
	s_notch_b1 = -2.0f * c;
	s_notch_b2 = 1.0f;
	s_notch_a1 = -2.0f * r * c;
	s_notch_a2 = r * r;

	memset(&s_notch_ch1, 0, sizeof(s_notch_ch1));
#if ADS129X_HAS_CH2
	memset(&s_notch_ch2, 0, sizeof(s_notch_ch2));
#endif

	ESP_LOGI(TAG, "Notch IIR @ %.1f Hz (fs=%u, r=%.3f)",
		 (double)ADS129X_NOTCH_FREQ_HZ, (unsigned)sample_rate_hz, (double)r);
}

static int32_t ads129x_notch_step(ads129x_notch_state_t *state, int32_t input)
{
	float x = (float)input;
	float y = (s_notch_b0 * x) + (s_notch_b1 * state->x1) + (s_notch_b2 * state->x2) -
		  (s_notch_a1 * state->y1) - (s_notch_a2 * state->y2);

	state->x2 = state->x1;
	state->x1 = x;
	state->y2 = state->y1;
	state->y1 = y;

	if (y > 8388607.0f) {
		return 8388607;
	}
	if (y < -8388608.0f) {
		return -8388608;
	}
	return (int32_t)lroundf(y);
}

static int32_t ads129x_clamp24(int32_t v)
{
	if (v > 8388607) {
		return 8388607;
	}
	if (v < -8388608) {
		return -8388608;
	}
	return v;
}

static int32_t ads129x_apply_filters(int32_t raw32,
				     ads129x_hp_state_t *hp,
				     ads129x_notch_state_t *notch,
				     ads129x_lp_state_t *lp)
{
	if (ADS129X_HIGHPASS_ENABLE != 0) {
		raw32 = ads129x_highpass_step(hp, raw32);
	}
	if (ADS129X_NOTCH_ENABLE != 0) {
		raw32 = ads129x_notch_step(notch, raw32);
	}
	if (ADS129X_LOWPASS_ENABLE != 0) {
		raw32 = ads129x_lowpass_step(lp, raw32);
	}
	return ads129x_clamp24(raw32);
}

static void ads129x_lowpass_reset(uint32_t sample_rate_hz)
{
	float fs;
	float fc;
	float x;

	if (sample_rate_hz == 0U) {
		sample_rate_hz = ADS129X_SAMPLE_RATE_HZ;
	}
	fs = (float)sample_rate_hz;
	fc = ADS129X_LOWPASS_CUTOFF_HZ;
	if (fc <= 0.0f) {
		fc = 40.0f;
	}
	if (fc >= (fs * 0.45f)) {
		fc = fs * 0.45f;
		ESP_LOGW(TAG, "Lowpass 截断到 %.1f Hz (Nyquist 限制)", (double)fc);
	}

	/* alpha = 1 - exp(-2*pi*fc/fs) */
	x = -2.0f * (float)M_PI * (fc / fs);
	s_lp_alpha = 1.0f - expf(x);
	if (s_lp_alpha < 0.01f) {
		s_lp_alpha = 0.01f;
	} else if (s_lp_alpha > 0.99f) {
		s_lp_alpha = 0.99f;
	}

	s_lp_ch1.prev_out = 0.0f;
#if ADS129X_HAS_CH2
	s_lp_ch2.prev_out = 0.0f;
#endif

	ESP_LOGI(TAG, "Lowpass IIR @ %.1f Hz (fs=%u, alpha=%.4f)",
		 (double)fc, (unsigned)sample_rate_hz, (double)s_lp_alpha);
}

static int32_t ads129x_lowpass_step(ads129x_lp_state_t *state, int32_t input)
{
	float xin = (float)input;
	float y = state->prev_out + (s_lp_alpha * (xin - state->prev_out));

	state->prev_out = y;
	return ads129x_clamp24((int32_t)lroundf(y));
}

static int ads129x_wait_drdy_inactive(void)
{
	int timeout = ADS129X_DRDY_HIGH_TIMEOUT_CYCLE;

	do {
		if (gpio_get_level(BOARD_ADS_DRDY_GPIO) != 0) {
			return 0;
		}
		timeout--;
	} while (timeout > 0);

	return -ETIMEDOUT;
}

int ads129x_rreg(uint8_t start_addr, uint8_t count, uint8_t *out_buf)
{
	uint8_t tx_buf[2 + ADS129X_REGISTER_COUNT] = { 0 };
	uint8_t rx_buf[2 + ADS129X_REGISTER_COUNT] = { 0 };
	int ret;

	if ((out_buf == NULL) || (count == 0U) || (count > ADS129X_REGISTER_COUNT)) {
		return -EINVAL;
	}

	ret = ads129x_send_command(ADS129X_CMD_SDATAC);
	if (ret < 0) {
		return ret;
	}
	ads129x_delay_us(ADS129X_COMMAND_GUARD_US);

	tx_buf[0] = ADS129X_CMD_RREG | (start_addr & ADS129X_REGISTER_MASK);
	tx_buf[1] = (uint8_t)(count - 1);

	ret = ads129x_spi_transceive(tx_buf, rx_buf, 2 + count);
	if (ret == 0) {
		memcpy(out_buf, &rx_buf[2], count);
	}

	return ret;
}

int ads129x_wreg(uint8_t start_addr, uint8_t count, const uint8_t *in_buf)
{
	uint8_t tx_buf[2 + ADS129X_REGISTER_COUNT];

	if (in_buf == NULL || count == 0 || count > ADS129X_REGISTER_COUNT) {
		return -EINVAL;
	}

	tx_buf[0] = ADS129X_CMD_WREG | (start_addr & ADS129X_REGISTER_MASK);
	tx_buf[1] = count - 1;
	memcpy(&tx_buf[2], in_buf, count);

	return ads129x_spi_write(tx_buf, 2 + count);
}

void ads129x_wakeup(void)
{
	(void)ads129x_send_command(ADS129X_CMD_WAKEUP);
	ads129x_delay_ms(1);
}

void ads129x_standby(void)
{
	(void)ads129x_send_command(ADS129X_CMD_STANDBY);
	ads129x_delay_ms(1);
}

void ads129x_reset(void)
{
	(void)ads129x_send_command(ADS129X_CMD_RESET);
	ads129x_delay_ms(ADS129X_RESET_COMMAND_SETTLE_MS);
}

static int32_t ads129x_raw24_to_int32(const uint8_t raw[3])
{
	int32_t value = ((int32_t)raw[0] << 16) | ((int32_t)raw[1] << 8) | raw[2];

	if ((value & 0x00800000) != 0) {
		value |= 0xFF000000;
	}

	return value;
}

static void ads129x_int32_to_24bit_bytes(int32_t value, uint8_t out[ADS129X_CHANNEL_SIZE])
{
	uint32_t u24;

	if (value > 0x7FFFFF) {
		value = 0x7FFFFF;
	} else if (value < -0x800000) {
		value = -0x800000;
	}

	u24 = (uint32_t)value & 0x00FFFFFFU;
	out[0] = (uint8_t)(u24 >> 16);
	out[1] = (uint8_t)(u24 >> 8);
	out[2] = (uint8_t)u24;
}

static int16_t ads129x_int32_to_int16(int32_t value)
{
	if (value > INT16_MAX) {
		return INT16_MAX;
	}

	if (value < INT16_MIN) {
		return INT16_MIN;
	}

	return (int16_t)value;
}

static uint8_t ads129x_loff_from_status(const uint8_t status[3])
{
	return (uint8_t)(((status[0] & 0x0FU) << 1) | ((status[1] & 0x80U) >> 7));
}
