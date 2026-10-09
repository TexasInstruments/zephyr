/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <zephyr/drivers/led.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS_OKAY(DT_PARENT(LED0_NODE))
static const struct led_dt_spec led0 = LED_DT_SPEC_GET(LED0_NODE);
#else
static const struct led_dt_spec led0;
#endif

static void button_input_cb(struct input_event *evt, void *user_data)
{
	if (evt->sync == 0) {
		return;
	}

	printk("Button %d %s at %" PRIu32 "\n",
	       evt->code,
	       evt->value ? "pressed" : "released",
	       k_cycle_get_32());

	if (led0.dev != NULL) {
		led_set_brightness_dt(&led0, evt->value ? 100 : 0);
	}
}
int booster_pack_gpios[] = {9, 10, 14, 15};

#define GPIO_NODE	DT_NODELABEL(gpio0)

const struct device *dev = DEVICE_DT_GET(GPIO_NODE);;

#define CHECK_MODE(_flags, _x) #_x, _flags&_x?"YES":"NO"

void print_gpio(uint32_t pin, uint32_t flags) {
	printk("PIN [%d]\n", pin);
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_INPUT));
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_OUTPUT));
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_OUTPUT_INIT_HIGH));
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_OUTPUT_INIT_LOW));
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_PULL_UP));
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_PULL_DOWN));
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_INT_EDGE_RISING));
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_INT_EDGE_FALLING));
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_INT_DISABLE));
	printk(" %20.20s\t[%3.3s]\n", CHECK_MODE(flags, GPIO_OPEN_SOURCE));
}

INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);

int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button.port->name, button.pin);
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return 0;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", button.port->name, button.pin);

	if (led.port && !gpio_is_ready_dt(&led)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, led.port->name);
		led.port = NULL;
	}
	if (led.port) {
		ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, led.port->name, led.pin);
			led.port = NULL;
		} else {
			printk("Set up LED at %s pin %d\n", led.port->name, led.pin);
		}
	}

	uint32_t flags = 0;

	for (int i = 0 ; i < (sizeof(booster_pack_gpios) / sizeof(int)) ; i++) {
		ret = gpio_pin_get_config(dev, booster_pack_gpios[i], &flags);
		if (ret != 0) {
			printk("Error %d: failed to configure %d\n", ret, booster_pack_gpios[i]);
			return 0;
		} else {
			print_gpio(booster_pack_gpios[i], flags);
		}
	}

	printk("Press the button\n");

	k_sleep(K_FOREVER);

	return 0;
}
