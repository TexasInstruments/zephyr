/*
 * Copyright (c) 2024 Texas Instruments Incorporated
 * Copyright (c) 2026 Conclusive Engineering Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sd/sd.h>
#include <zephyr/sd/sdio.h>

LOG_MODULE_REGISTER(ti_wifi_cc33xx_sdio, LOG_LEVEL_INF);

#define TI_CC33XX_SDIO_FUNC_NUM          2U
#define TI_CC33XX_SDIO_BLOCK_SIZE        128U
#define TI_CC33XX_SDIO_INT_DIS_ALL       0x00U
#define TI_CC33XX_SDIO_EVT_TIMEOUT_MS    5000U
#define TI_CC33XX_SDIO_EVT_INIT_SYNC     BIT(0)
#define TI_CC33XX_SDIO_CCCR_ASYNC_IB_IRQ 0xC0U
#define TI_CC33XX_SDIO_ASYNC_IB_EN       BIT(0)
#define TI_CC33XX_SDIO_ASYNC_IB_MASK     BIT(0)

#define WLAN_NODE DT_NODELABEL(wlan0)

#define TI_CC33XX_SDIO_OOB_IRQ DT_NODE_HAS_PROP(WLAN_NODE, wifi_host_wake_gpios)

struct wlan_irq_context {
	struct gpio_callback gpio_cb;
	void *user_cb;
};

struct ti_cc33xx_sdio_ctx {
	const struct device *sdhc_dev;
	const int bus_width;
#if TI_CC33XX_SDIO_OOB_IRQ
	struct gpio_dt_spec wlan_irq;
#endif
	struct sd_card card;
	struct sdio_func ti_sdio_func;
	struct k_event ti_sdio_init;
	struct k_mutex io_lock;
	struct wlan_irq_context irq_ctx;
};

static struct ti_cc33xx_sdio_ctx dev_ctx = {
	.sdhc_dev = DEVICE_DT_GET(DT_PARENT(WLAN_NODE)),
	.io_lock = Z_MUTEX_INITIALIZER(dev_ctx.io_lock),
	.card.bus_io.bus_width = SDHC_BUS_WIDTH1BIT,
#if TI_CC33XX_SDIO_OOB_IRQ
	.wlan_irq = GPIO_DT_SPEC_GET(WLAN_NODE, wifi_host_wake_gpios),
#endif
	.bus_width = DT_PROP(WLAN_NODE, bus_width),
};

#if TI_CC33XX_SDIO_OOB_IRQ

static void wlan_gpio_init(void)
{
	gpio_pin_configure_dt(&dev_ctx.wlan_irq, GPIO_INPUT);
}

static void wlan_oob_irq_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(pins);
	void (*callback_fn)(void);
	struct wlan_irq_context *ctx;

	ctx = CONTAINER_OF(cb, struct wlan_irq_context, gpio_cb);

	if (ctx->user_cb) {
		callback_fn = ctx->user_cb;
		callback_fn();
	}
}

void wlan_IRQInit(void *cb)
{
	if (!device_is_ready(dev_ctx.wlan_irq.port)) {
		return;
	}

	dev_ctx.irq_ctx.user_cb = cb;

	gpio_pin_configure_dt(&dev_ctx.wlan_irq, GPIO_INPUT);

	gpio_init_callback(&dev_ctx.irq_ctx.gpio_cb, wlan_oob_irq_handler,
			   BIT(dev_ctx.wlan_irq.pin));

	gpio_add_callback(dev_ctx.wlan_irq.port, &dev_ctx.irq_ctx.gpio_cb);
}

void wlan_IRQEnableInt(void)
{
	uintptr_t key;

	key = irq_lock();

	gpio_pin_interrupt_configure_dt(&dev_ctx.wlan_irq, GPIO_INT_EDGE_TO_ACTIVE);

	irq_unlock(key);
}

void wlan_IRQInitBeforeHwInit(void *cb)
{
	k_event_init(&dev_ctx.ti_sdio_init);
	wlan_gpio_init();
	wlan_IRQInit(cb);
	wlan_IRQEnableInt();
}

static void wlan_irq_disable_on_handler(void)
{
}

#else /* in-band SDIO interrupt */

static void wlan_ib_irq_handler(const struct device *dev, int reason, const void *user_data)
{
	const struct wlan_irq_context *ctx = user_data;
	void (*callback_fn)(void);

	ARG_UNUSED(dev);

	if ((reason & TI_CC33XX_SDIO_ASYNC_IB_MASK) && ctx->user_cb) {
		callback_fn = ctx->user_cb;
		callback_fn();
	}
}

void wlan_IRQInit(void *cb)
{
	dev_ctx.irq_ctx.user_cb = cb;
}

void wlan_IRQEnableInt(void)
{
	int ret;

	ret = sdhc_enable_interrupt(dev_ctx.sdhc_dev, wlan_ib_irq_handler,
				    TI_CC33XX_SDIO_ASYNC_IB_MASK, &dev_ctx.irq_ctx);
	if (ret) {
		LOG_ERR("Failed to enable in-band SDIO interrupt (ret: %d)", ret);
	}
}

void wlan_IRQInitBeforeHwInit(void *cb)
{
	k_event_init(&dev_ctx.ti_sdio_init);
	wlan_IRQInit(cb);
}

static void wlan_irq_disable_on_handler(void)
{
	(void)sdhc_disable_interrupt(dev_ctx.sdhc_dev, TI_CC33XX_SDIO_ASYNC_IB_MASK);
}

#endif /* TI_CC33XX_SDIO_OOB_IRQ */

int sdioAdapt_deInit(void)
{
	struct sdhc_io io;
	uint8_t io_en = 0;
	int ret = 0;

	k_mutex_lock(&dev_ctx.io_lock, K_FOREVER);

	k_event_clear(&dev_ctx.ti_sdio_init, TI_CC33XX_SDIO_EVT_INIT_SYNC);
	io = dev_ctx.card.bus_io;

	if (dev_ctx.card.sdhc != NULL) {
		if (dev_ctx.card.func0.card != NULL) {
			if (sdio_read_byte(&dev_ctx.card.func0, SDIO_CCCR_IO_EN, &io_en) == 0) {
				io_en &= ~BIT(TI_CC33XX_SDIO_FUNC_NUM);
				(void)sdio_write_byte(&dev_ctx.card.func0, SDIO_CCCR_IO_EN, io_en);
			}

			(void)sdio_write_byte(&dev_ctx.card.func0, SDIO_CCCR_INT_EN,
					      TI_CC33XX_SDIO_INT_DIS_ALL);
		}

		io.clock = 0;
		io.power_mode = SDHC_POWER_OFF;
		ret = sdhc_set_io(dev_ctx.sdhc_dev, &io);
		if (ret) {
			LOG_ERR("Failed to power off SDHC (ret: %d)", ret);
		}
	}

	dev_ctx.card.status = CARD_UNINITIALIZED;
	memset(&dev_ctx.ti_sdio_func, 0, sizeof(dev_ctx.ti_sdio_func));

	k_mutex_unlock(&dev_ctx.io_lock);

	return ret;
}

int sdioAdapt_Init(void)
{
	int ret;
	struct sdhc_host_props props;
	struct sdhc_io io;
	uint8_t bus_if;
	uint8_t async_ib;

	if (!device_is_ready(dev_ctx.sdhc_dev)) {
		LOG_ERR("SDHC host device is not ready");
		return -ENODEV;
	}

	k_mutex_lock(&dev_ctx.io_lock, K_FOREVER);

	ret = sd_init(dev_ctx.sdhc_dev, &dev_ctx.card);
	if (ret) {
		LOG_ERR("Failed to initialize SD card structure (ret: %d)", ret);
		goto err_init;
	}

	ret = sdhc_get_host_props(dev_ctx.sdhc_dev, &props);
	if (ret) {
		LOG_ERR("Failed to get SDHC host properties (ret: %d)", ret);
		goto err_init;
	}

	ret = sdio_init_func(&dev_ctx.card, &dev_ctx.ti_sdio_func, TI_CC33XX_SDIO_FUNC_NUM);
	if (ret) {
		LOG_ERR("Failed to initialize SDIO function %u (ret: %d)", TI_CC33XX_SDIO_FUNC_NUM,
			ret);
		goto err_init;
	}

	ret = sdio_enable_func(&dev_ctx.ti_sdio_func);
	if (ret) {
		LOG_ERR("Failed to enable SDIO function %u (ret: %d)", TI_CC33XX_SDIO_FUNC_NUM,
			ret);
		goto err_init;
	}

	ret = sdio_write_byte(&dev_ctx.card.func0, SDIO_CCCR_INT_EN, TI_CC33XX_SDIO_INT_DIS_ALL);
	if (ret) {
		LOG_ERR("Failed to disable interrupts in CCCR register (ret: %d)", ret);
		goto err_init;
	}

	ret = sdio_read_byte(&dev_ctx.card.func0, SDIO_CCCR_BUS_IF, &bus_if);
	if (ret) {
		LOG_ERR("Failed to read CCCR Bus Interface Control register (ret: %d)", ret);
		goto err_init;
	}

	io = dev_ctx.card.bus_io;
	io.driver_type = SD_DRIVER_TYPE_B;
	bus_if &= ~SDIO_CCCR_BUS_IF_WIDTH_MASK;
	if (dev_ctx.bus_width == 4) {
		LOG_DBG("SDIO bus width set to 4 bit mode");
		io.bus_width = SDHC_BUS_WIDTH4BIT;
		bus_if |= SDIO_CCCR_BUS_IF_WIDTH_4_BIT;
	} else {
		LOG_DBG("SDIO bus width set to 1 bit mode");
		io.bus_width = SDHC_BUS_WIDTH1BIT;
		bus_if |= SDIO_CCCR_BUS_IF_WIDTH_1_BIT;
	}

	ret = sdio_write_byte(&dev_ctx.card.func0, SDIO_CCCR_BUS_IF, bus_if);
	if (ret) {
		LOG_ERR("Failed to write CCCR Bus Interface Control register (ret: %d)", ret);
		goto err_init;
	}

	ret = sdhc_set_io(dev_ctx.sdhc_dev, &io);
	if (ret) {
		LOG_ERR("Failed to configure MCU SDHC IO (ret: %d)", ret);
		goto err_init;
	}
	dev_ctx.card.bus_io = io;

	ret = sdio_set_block_size(&dev_ctx.ti_sdio_func, TI_CC33XX_SDIO_BLOCK_SIZE);
	if (ret) {
		LOG_ERR("Failed to set SDIO block size to %u bytes (ret: %d)",
			TI_CC33XX_SDIO_BLOCK_SIZE, ret);
		goto err_init;
	}
	LOG_DBG("SDIO Block size set to %u bytes", TI_CC33XX_SDIO_BLOCK_SIZE);

	if (!TI_CC33XX_SDIO_OOB_IRQ) {
		ret = sdio_write_byte(&dev_ctx.card.func0, SDIO_CCCR_INT_EN,
				      BIT(0) | BIT(TI_CC33XX_SDIO_FUNC_NUM));
		if (ret) {
			LOG_ERR("Failed to enable in-band interrupts in CCCR (ret: %d)", ret);
			goto err_init;
		}

		ret = sdio_read_byte(&dev_ctx.card.func0, TI_CC33XX_SDIO_CCCR_ASYNC_IB_IRQ,
				     &async_ib);
		if (ret) {
			LOG_ERR("Failed to read async in-band register (ret: %d)", ret);
			goto err_init;
		}

		ret = sdio_write_byte(&dev_ctx.card.func0, TI_CC33XX_SDIO_CCCR_ASYNC_IB_IRQ,
				      async_ib | TI_CC33XX_SDIO_ASYNC_IB_EN);
		if (ret) {
			LOG_ERR("Failed to enable async in-band (ret: %d)", ret);
			goto err_init;
		}
	}

	k_msleep(props.power_delay);

	k_event_post(&dev_ctx.ti_sdio_init, TI_CC33XX_SDIO_EVT_INIT_SYNC);
	k_mutex_unlock(&dev_ctx.io_lock);

	return 0;

err_init:
	(void)sdioAdapt_deInit();
	k_mutex_unlock(&dev_ctx.io_lock);

	return ret;
}

int sdioAdapt_read(uint32_t address, uint8_t *data, uint32_t length)
{
	int ret;
	uint32_t events;

	if (data == NULL) {
		LOG_ERR("Read failed: data pointer is NULL");
		return -1;
	}

	if (length == 0) {
		return 0;
	}

	if (!device_is_ready(dev_ctx.sdhc_dev)) {
		LOG_ERR("Read failed: SDHC host device is not ready");
		return -1;
	}

	events = k_event_wait(&dev_ctx.ti_sdio_init, TI_CC33XX_SDIO_EVT_INIT_SYNC, false,
			      K_MSEC(TI_CC33XX_SDIO_EVT_TIMEOUT_MS));

	if ((events & TI_CC33XX_SDIO_EVT_INIT_SYNC) == 0) {
		LOG_ERR("Read failed: Timeout waiting for SDIO init sync event");
		return -1;
	}

	k_mutex_lock(&dev_ctx.io_lock, K_FOREVER);

	if ((k_event_test(&dev_ctx.ti_sdio_init, TI_CC33XX_SDIO_EVT_INIT_SYNC) &
	     TI_CC33XX_SDIO_EVT_INIT_SYNC) == 0) {
		k_mutex_unlock(&dev_ctx.io_lock);
		LOG_ERR("Read failed: SDIO not initialized");
		return -1;
	}

	LOG_DBG("Reading FIFO payload: Addr=0x%08X, Len=%u bytes", address, length);
	ret = sdio_read_fifo(&dev_ctx.ti_sdio_func, address, data, length);
	k_mutex_unlock(&dev_ctx.io_lock);
	if (ret) {
		LOG_ERR("SDIO FIFO read failed at Addr 0x%08X (Len: %u, ret: %d)", address, length,
			ret);
		return -1;
	}

	return 0;
}

int sdioAdapt_write(uint32_t address, uint8_t *data, uint32_t length)
{
	int ret;
	uint32_t events;

	if (data == NULL) {
		LOG_ERR("Write failed: data pointer is NULL");
		return -1;
	}

	if (length == 0) {
		return 0;
	}

	if (!device_is_ready(dev_ctx.sdhc_dev)) {
		LOG_ERR("Write failed: SDHC host device is not ready");
		return -1;
	}

	events = k_event_wait(&dev_ctx.ti_sdio_init, TI_CC33XX_SDIO_EVT_INIT_SYNC, false,
			      K_MSEC(TI_CC33XX_SDIO_EVT_TIMEOUT_MS));

	if ((events & TI_CC33XX_SDIO_EVT_INIT_SYNC) == 0) {
		LOG_ERR("Write failed: Timeout waiting for SDIO init sync event");
		return -1;
	}

	k_mutex_lock(&dev_ctx.io_lock, K_FOREVER);

	if ((k_event_test(&dev_ctx.ti_sdio_init, TI_CC33XX_SDIO_EVT_INIT_SYNC) &
	     TI_CC33XX_SDIO_EVT_INIT_SYNC) == 0) {
		k_mutex_unlock(&dev_ctx.io_lock);
		LOG_ERR("Write failed: SDIO not initialized");
		return -1;
	}

	LOG_DBG("Writing FIFO payload: Addr=0x%08X, Len=%u bytes", address, length);
	ret = sdio_write_fifo(&dev_ctx.ti_sdio_func, address, data, length);
	k_mutex_unlock(&dev_ctx.io_lock);
	if (ret) {
		LOG_ERR("SDIO FIFO write failed at Addr 0x%08X (Len: %u, ret: %d)", address, length,
			ret);
		return -1;
	}

	return 0;
}

void wlan_IRQDeinit(void)
{
}

void wlan_IRQDisableInt(void)
{
}

void wlan_TurnOffWlan(void)
{
}

void wlan_TurnOnWlan(void)
{
}

void wlan_IRQDisableOnIRQHandler(void)
{
	wlan_irq_disable_on_handler();
}
