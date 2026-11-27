/*
 * Copyright (c) 2024 Texas Instruments Incorporated
 * Copyright (c) 2026 Conclusive Engineering Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(ti_wifi_cc33xx_spi, LOG_LEVEL_INF);

struct wlan_irq_context {
	struct gpio_callback gpio_cb;
	void *user_cb;
};

static struct wlan_irq_context wlan_irq_ctx;

#define SPI_BUFFER_SIZE (256)
#define WLAN_NODE       DT_NODELABEL(wlan0)
static const struct gpio_dt_spec wlan_on = GPIO_DT_SPEC_GET(WLAN_NODE, wifi_reg_on_gpios);
static const struct gpio_dt_spec wlan_irq = GPIO_DT_SPEC_GET(WLAN_NODE, wifi_host_wake_gpios);

static const struct spi_dt_spec wlan_spi =
	SPI_DT_SPEC_GET(WLAN_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8));

static void wlan_gpio_init(void)
{
	gpio_pin_configure_dt(&wlan_on, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&wlan_irq, GPIO_INPUT);
}

static void wlan_irq_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(pins);
	void (*callback_fn)(void);
	struct wlan_irq_context *ctx = CONTAINER_OF(cb, struct wlan_irq_context, gpio_cb);

	if (ctx->user_cb) {
		callback_fn = ctx->user_cb;
		callback_fn();
	}
}

void wlan_IRQInit(void *cb)
{
	if (!device_is_ready(wlan_irq.port)) {
		return;
	}

	wlan_irq_ctx.user_cb = cb;

	gpio_pin_configure_dt(&wlan_irq, GPIO_INPUT);

	gpio_init_callback(&wlan_irq_ctx.gpio_cb, wlan_irq_handler, BIT(wlan_irq.pin));

	gpio_add_callback(wlan_irq.port, &wlan_irq_ctx.gpio_cb);
}

void wlan_IRQInitBeforeHwInit(void *cb)
{
	wlan_gpio_init();
	wlan_IRQInit(cb);
}

void wlan_IRQDeinit(void)
{
}

void wlan_IRQEnableInt(void)
{
	uintptr_t key;

	key = irq_lock();

	gpio_pin_interrupt_configure_dt(&wlan_irq, GPIO_INT_EDGE_TO_ACTIVE);

	irq_unlock(key);
}

void wlan_IRQDisableInt(void)
{
}

void wlan_IRQDisableOnIRQHandler(void)
{
}

void wlan_TurnOffWlan(void)
{
	gpio_pin_set_dt(&wlan_on, 0);
}

void wlan_TurnOnWlan(void)
{
	gpio_pin_set_dt(&wlan_on, 1);
}

int spi_ReadSync(uint8_t *data, uint32_t length)
{
	int ret;
	uint32_t i, offset, chunk_size;
	uint32_t *ptr;
	struct spi_buf rx_buf;
	struct spi_buf_set rx;
	uint8_t buffer[SPI_BUFFER_SIZE] __aligned(sizeof(uint32_t));

	if (!IS_ALIGNED(length, sizeof(uint32_t))) {
		return -1;
	}

	if (!spi_is_ready_dt(&wlan_spi)) {
		return -1;
	}

	for (i = 0; i < length; i += sizeof(buffer)) {

		chunk_size = MIN(sizeof(buffer), length - i);

		rx_buf.buf = buffer;
		rx_buf.len = chunk_size;

		rx.buffers = &rx_buf;
		rx.count = 1;

		ret = spi_read_dt(&wlan_spi, &rx);
		if (ret) {
			return -1;
		}

		for (offset = 0; offset < chunk_size; offset += sizeof(uint32_t)) {
			ptr = (uint32_t *)(buffer + offset);
			*ptr = sys_be32_to_cpu(*ptr);
		}

		memcpy(data + i, buffer, chunk_size);
	}

	return 0;
}

int spi_WriteSync(uint8_t *data, uint32_t length)
{
	int ret;
	uint32_t i, offset, chunk_size;
	uint32_t *ptr;
	struct spi_buf tx_buf;
	struct spi_buf_set tx;
	uint8_t buffer[SPI_BUFFER_SIZE] __aligned(sizeof(uint32_t));

	if (!IS_ALIGNED(length, sizeof(uint32_t))) {
		return -1;
	}

	if (!spi_is_ready_dt(&wlan_spi)) {
		return -1;
	}

	for (i = 0; i < length; i += sizeof(buffer)) {
		chunk_size = MIN(sizeof(buffer), length - i);
		memcpy(buffer, data + i, chunk_size);

		for (offset = 0; offset < chunk_size; offset += sizeof(uint32_t)) {
			ptr = (uint32_t *)(buffer + offset);
			*ptr = sys_cpu_to_be32(*ptr);
		}

		tx_buf.buf = buffer;
		tx_buf.len = chunk_size;

		tx.buffers = &tx_buf;
		tx.count = 1;

		ret = spi_write_dt(&wlan_spi, &tx);
		if (ret) {
			return -1;
		}
	}

	return 0;
}
