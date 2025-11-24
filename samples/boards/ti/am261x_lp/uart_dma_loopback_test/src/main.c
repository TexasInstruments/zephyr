/*
 * Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include <zephyr/cache.h>

/*
 * Hardware Setup Required:
 * Connect UART TX to UART RX with a jumper wire for hardware loopback.
 * Refer to your board's overlay file for specific pin assignments.
 */

/*
 * Get UART device from the devicetree uart_dut alias. This is mandatory.
 */
#define UART_DUT_NODE DT_ALIAS(uart_dut)
#if !DT_NODE_HAS_STATUS_OKAY(UART_DUT_NODE)
#error "Unsupported board: uart_dut devicetree alias is not defined"
#endif

int main(void)
{
	const struct device *uart_dev = DEVICE_DT_GET(UART_DUT_NODE);
	char rx_buf[96] __aligned(CONFIG_DCACHE_LINE_SIZE);
	char tx_buf[96] __aligned(CONFIG_DCACHE_LINE_SIZE) =
		"Hi from UART. This is an intentionally long message, my friend! Lorem Ipsum!";
	int err;

	sys_cache_data_flush_range((void *)tx_buf, sizeof(tx_buf));
	uart_rx_enable(uart_dev, rx_buf, sizeof(rx_buf), SYS_FOREVER_US);

	err = uart_tx(uart_dev, tx_buf, sizeof(tx_buf), SYS_FOREVER_US);
	if (err) {
		printk("Unexpected error encountered with error code %d\n", err);
		return err;
	}

	k_sleep(K_MSEC(100));

	uart_rx_disable(uart_dev);

	sys_cache_data_invd_range((void *)rx_buf, sizeof(rx_buf));
	printk("%s\n", rx_buf);

	if (memcmp(tx_buf, rx_buf, sizeof(tx_buf))) {
		printk("UART DMA loopback test failed!\n");
		return -1;
	}

	printk("UART DMA loopback test passed!\n");
	return 0;
}
