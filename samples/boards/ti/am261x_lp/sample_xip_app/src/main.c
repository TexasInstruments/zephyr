/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
	int ret;
	bool led_state = true;

	printf("Main Core Blinky Started\n");

	if (!gpio_is_ready_dt(&led)) {
		printf("LED device not ready - continuing with LED simulation\n");
		/* Continue with simulated LED behavior even if real LED isn't available */
	} else {
		ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			printf("Error %d: Failed to configure LED pin\n", ret);
			/* Continue with simulated LED behavior */
		}
	}

	/* Main loop */
	while (1) {
		/* Toggle real LED if available */
		if (gpio_is_ready_dt(&led)) {
			ret = gpio_pin_toggle_dt(&led);
			if (ret < 0) {
				printf("Error: Failed to toggle LED\n");
			}
		}

		/* Simulated LED state for console output */
		led_state = !led_state;
		printf("LED state: %s\n", led_state ? "ON" : "OFF");

		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
