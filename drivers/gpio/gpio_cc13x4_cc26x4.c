/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_cc13x4_cc26x4_gpio

#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/__assert.h>
#include <errno.h>

#include <driverlib/gpio.h>
#include <driverlib/ioc.h>
#include <driverlib/prcm.h>
#include <inc/hw_aon_event.h>

#ifdef CONFIG_PM
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26X2.h>
#endif

/**  Interrupt config bits in IOCFG register (bits 16-18) -
 * preserve across general config calls
 */
#define IOCFG_INT_MASK 0x00070000
#define IOCFG_GEN_MASK (~IOCFG_INT_MASK)

struct gpio_cc13x4_cc26x4_config {
	struct gpio_driver_config common;
};

struct gpio_cc13x4_cc26x4_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
};

static void cc13x4_set_multi(uint32_t mask)
{
	for (int i = 0; i < 32; i++) {
		if (mask & BIT(i)) {
			GPIO_setDio(i);
		}
	}
}

static void cc13x4_clear_multi(uint32_t mask)
{
	for (int i = 0; i < 32; i++) {
		if (mask & BIT(i)) {
			GPIO_clearDio(i);
		}
	}
}

static void cc13x4_toggle_multi(uint32_t mask)
{
	for (int i = 0; i < 32; i++) {
		if (mask & BIT(i)) {
			GPIO_toggleDio(i);
		}
	}
}

static uint32_t cc13x4_read_multi(uint32_t mask)
{
	uint32_t value = 0;

	for (int i = 0; i < 32; i++) {
		if ((mask & BIT(i)) && GPIO_readDio(i)) {
			value |= BIT(i);
		}
	}
	return value;
}

static int gpio_cc13x4_config(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
	__ASSERT_NO_MSG(pin < NUM_IO_MAX);

	switch (flags & (GPIO_INPUT | GPIO_OUTPUT)) {
	case GPIO_INPUT:
		break;
	case GPIO_OUTPUT:
		break;
	case 0: /* disconnected */
		IOCPortConfigureSet(pin, IOC_PORT_GPIO, IOC_NO_IOPULL);
		GPIO_setOutputEnableDio(pin, GPIO_OUTPUT_DISABLE);
		return 0;
	default: /* GPIO_INPUT | GPIO_OUTPUT simultaneously - invalid */
		return -ENOTSUP;
	}

	uint32_t config = IOC_CURRENT_2MA | IOC_STRENGTH_AUTO;

	config |= IOCPortConfigureGet(pin) & IOCFG_INT_MASK;

	config |= IOC_SLEW_DISABLE | IOC_NO_WAKE_UP;

	if (flags & GPIO_INPUT) {
		config |= IOC_INPUT_ENABLE;
	} else {
		config |= IOC_INPUT_DISABLE;
	}

	switch (flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) {
	case 0:
		config |= IOC_NO_IOPULL;
		break;
	case GPIO_PULL_UP:
		config |= IOC_IOPULL_UP;
		break;
	case GPIO_PULL_DOWN:
		config |= IOC_IOPULL_DOWN;
		break;
	default: /* invalid: both pull-up and pull-down */
		return -EINVAL;
	}

	IOCPortConfigureSet(pin, IOC_PORT_GPIO, config);

	if (flags & GPIO_OUTPUT) {
		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			GPIO_setDio(pin);
		} else if (flags & GPIO_OUTPUT_INIT_LOW) {
			GPIO_clearDio(pin);
		}
		GPIO_setOutputEnableDio(pin, GPIO_OUTPUT_ENABLE);
	} else {
		GPIO_setOutputEnableDio(pin, GPIO_OUTPUT_DISABLE);
	}

	return 0;
}

static int gpio_cc13x4_port_get_raw(const struct device *port, uint32_t *value)
{
	*value = cc13x4_read_multi(0xFFFFFFFF);
	return 0;
}

static int gpio_cc13x4_port_set_masked_raw(const struct device *port, uint32_t mask, uint32_t value)
{
	cc13x4_set_multi(mask & value);
	cc13x4_clear_multi(mask & ~value);
	return 0;
}

static int gpio_cc13x4_port_set_bits_raw(const struct device *port, uint32_t mask)
{
	cc13x4_set_multi(mask);
	return 0;
}

static int gpio_cc13x4_port_clear_bits_raw(const struct device *port, uint32_t mask)
{
	cc13x4_clear_multi(mask);
	return 0;
}

static int gpio_cc13x4_port_toggle_bits(const struct device *port, uint32_t mask)
{
	cc13x4_toggle_multi(mask);
	return 0;
}

static int gpio_cc13x4_pin_interrupt_configure(const struct device *port, gpio_pin_t pin,
					       enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	uint32_t config = 0;

	if (mode != GPIO_INT_MODE_DISABLED) {
		if (mode == GPIO_INT_MODE_EDGE) {
			if (trig == GPIO_INT_TRIG_BOTH) {
				config |= IOC_BOTH_EDGES;
			} else if (trig == GPIO_INT_TRIG_HIGH) {
				config |= IOC_RISING_EDGE;
			} else { /* GPIO_INT_TRIG_LOW */
				config |= IOC_FALLING_EDGE;
			}
		} else {
			return -ENOTSUP;
		}

		config |= IOC_INT_ENABLE;
	} else {
		config |= IOC_INT_DISABLE | IOC_NO_EDGE;
	}

	config |= IOCPortConfigureGet(pin) & IOCFG_GEN_MASK;
	IOCPortConfigureSet(pin, IOC_PORT_GPIO, config);

	return 0;
}

static int gpio_cc13x4_manage_callback(const struct device *port, struct gpio_callback *callback,
				       bool set)
{
	struct gpio_cc13x4_cc26x4_data *data = port->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static uint32_t gpio_cc13x4_get_pending_int(const struct device *dev)
{
	uint32_t status = 0;

	for (int i = 0; i < 32; i++) {
		if (GPIO_getEventDio(i)) {
			status |= BIT(i);
		}
	}
	return status;
}

static void gpio_cc13x4_isr(const struct device *dev)
{
	struct gpio_cc13x4_cc26x4_data *data = dev->data;
	uint32_t status = 0;

	/*
	 * Iterate all NUM_IO_MAX DIOs. Events on DIO32-47 must be cleared
	 * or they will re-fire immediately causing an infinite interrupt loop.
	 */
	for (int i = 0; i < NUM_IO_MAX; i++) {
		if (GPIO_getEventDio(i)) {
			GPIO_clearEventDio(i);
			if (i < 32) {
				status |= BIT(i);
			}
		}
	}

	gpio_fire_callbacks(&data->callbacks, dev, status);
}

#ifdef CONFIG_GPIO_GET_DIRECTION
static int gpio_cc13x4_port_get_direction(const struct device *dev, gpio_port_pins_t map,
					  gpio_port_pins_t *inputs, gpio_port_pins_t *outputs)
{
	gpio_port_pins_t ip = 0, op = 0;

	for (int i = 0; i < 32; i++) {
		if (!(map & BIT(i))) {
			continue;
		}
		uint32_t cfg = IOCPortConfigureGet(i);

		if (cfg & IOC_INPUT_ENABLE) {
			ip |= BIT(i);
		} else if (GPIO_getOutputEnableDio(i) == GPIO_OUTPUT_ENABLE) {
			op |= BIT(i);
		}
	}

	if (inputs) {
		*inputs = ip;
	}
	if (outputs) {
		*outputs = op;
	}
	return 0;
}
#endif /* CONFIG_GPIO_GET_DIRECTION */

#ifdef CONFIG_PM_DEVICE
static int gpio_cc13x4_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}
#endif /* CONFIG_PM_DEVICE */

static int gpio_cc13x4_init(const struct device *dev)
{
#ifdef CONFIG_PM
	Power_setDependency(PowerCC26XX_PERIPH_GPIO);
#else
	PRCMPowerDomainOn(PRCM_DOMAIN_PERIPH);
	PRCMPeripheralRunEnable(PRCM_PERIPH_GPIO);
	PRCMLoadSet();
	while (!PRCMLoadGet()) {
		continue;
	}
#endif

	/* Enable GPIO edge events as MCU wakeup source */
	HWREG(AON_EVENT_BASE + AON_EVENT_O_MCUWUSEL) =
		(HWREG(AON_EVENT_BASE + AON_EVENT_O_MCUWUSEL) & (~AON_EVENT_MCUWUSEL_WU1_EV_M)) |
		AON_EVENT_MCUWUSEL_WU1_EV_PAD;

	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), gpio_cc13x4_isr,
		    DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));

	/* FIX 3: wait for domain power-on in both PM and non-PM paths */
	while (PRCMPowerDomainsAllOn(PRCM_DOMAIN_PERIPH) != PRCM_DOMAIN_POWER_ON) {
		continue;
	}

	return 0;
}

static DEVICE_API(gpio, gpio_cc13x4_driver_api) = {
	.pin_configure = gpio_cc13x4_config,
	.port_get_raw = gpio_cc13x4_port_get_raw,
	.port_set_masked_raw = gpio_cc13x4_port_set_masked_raw,
	.port_set_bits_raw = gpio_cc13x4_port_set_bits_raw,
	.port_clear_bits_raw = gpio_cc13x4_port_clear_bits_raw,
	.port_toggle_bits = gpio_cc13x4_port_toggle_bits,
	.pin_interrupt_configure = gpio_cc13x4_pin_interrupt_configure,
	.manage_callback = gpio_cc13x4_manage_callback,
	.get_pending_int = gpio_cc13x4_get_pending_int,
#ifdef CONFIG_GPIO_GET_DIRECTION
	.port_get_direction = gpio_cc13x4_port_get_direction,
#endif
};

static const struct gpio_cc13x4_cc26x4_config gpio_cc13x4_config_0 = {
	.common.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(0),
};

static struct gpio_cc13x4_cc26x4_data gpio_cc13x4_data_0;

#ifdef CONFIG_PM_DEVICE
PM_DEVICE_DT_DEFINE(0, gpio_cc13x4_pm_action);
#endif

DEVICE_DT_INST_DEFINE(0, gpio_cc13x4_init, PM_DEVICE_DT_GET(0), &gpio_cc13x4_data_0,
		      &gpio_cc13x4_config_0, PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,
		      &gpio_cc13x4_driver_api);
