/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>

#define WAKE_TIMER_NODE DT_ALIAS(wake_timer)
#define SAMPLE_INTERVAL_SECONDS 3
#define SAMPLE_PERIOD           K_SECONDS(SAMPLE_INTERVAL_SECONDS)

#if !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "zephyr,user must contain an io-channels property"
#endif

static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const struct device *const wake_timer = DEVICE_DT_GET(WAKE_TIMER_NODE);

int main(void)
{
	uint16_t raw_value;
	struct adc_sequence sequence = {
		.buffer = &raw_value,
		.buffer_size = sizeof(raw_value),
	};
	int err;

	if (!device_is_ready(console)) {
		return -ENODEV;
	}

	if (!pm_device_runtime_is_enabled(console)) {
		printk("UART console runtime PM is not enabled\n");
		return -ENOTSUP;
	}

	if (!device_is_ready(wake_timer)) {
		printk("Wake timer is not ready\n");
		return -ENODEV;
	}

	err = counter_start(wake_timer);
	if (err < 0) {
		printk("Could not start wake timer (%d)\n", err);
		return err;
	}

	if (!adc_is_ready_dt(&adc_channel)) {
		printk("ADC controller %s is not ready\n", adc_channel.dev->name);
		return -ENODEV;
	}

	if (!pm_device_runtime_is_enabled(adc_channel.dev)) {
		printk("ADC runtime PM is not enabled\n");
		return -ENOTSUP;
	}

	err = adc_channel_setup_dt(&adc_channel);
	if (err < 0) {
		printk("ADC channel setup failed (%d)\n", err);
		return err;
	}

	err = adc_sequence_init_dt(&adc_channel, &sequence);
	if (err < 0) {
		printk("ADC sequence setup failed (%d)\n", err);
		return err;
	}

	printk("Timer G0: %u Hz; sleeping for %d seconds between ADC0 channel %u samples\n",
	       counter_get_frequency(wake_timer), SAMPLE_INTERVAL_SECONDS,
	       adc_channel.channel_id);

	while (true) {
		int32_t millivolts;

		/* System PM delegates this timeout to Timer G0 and stops SysTick. */
		k_sleep(SAMPLE_PERIOD);

		err = adc_read_dt(&adc_channel, &sequence);
		if (err < 0) {
			printk("ADC read failed (%d)\n", err);
			continue;
		}

		millivolts = raw_value;
		err = adc_raw_to_millivolts_dt(&adc_channel, &millivolts);
		if (err == 0) {
			printk("ADC0.%u: raw=%u, voltage=%" PRId32 " mV\n",
			       adc_channel.channel_id, raw_value, millivolts);
		} else {
			printk("ADC0.%u: raw=%u\n", adc_channel.channel_id, raw_value);
		}
	}

	return 0;
}
