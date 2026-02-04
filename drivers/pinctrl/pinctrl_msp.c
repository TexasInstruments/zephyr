/*
 * Copyright (c) 2025 Texas Instruments
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_SOC_FAMILY_MSPM0) || defined(CONFIG_SOC_FAMILY_MSPM33)
#include <ti/driverlib/dl_gpio.h>
#elif defined(CONFIG_SOC_SERIES_AM13E)
#include <dl_gpio.h>
#endif

LOG_MODULE_REGISTER(pinctrl_msp, CONFIG_PINCTRL_LOG_LEVEL);

/* Common driver for MSP family microcontrollers */
#define DT_DRV_COMPAT ti_msp_pinctrl

#define MSP_PINCM(pinmux)        ((pinmux & 0xFFFF0000) >> 16)
#define MSP_PIN_FUNCTION(pinmux) (pinmux & 0x3F)

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

	uint8_t pin_function;
	uint32_t pin_cm;
	uint32_t iomux;

	for (int i = 0; i < pin_cnt; i++) {
		pin_cm = MSP_PINCM(pins[i].pinmux);
		pin_function = MSP_PIN_FUNCTION(pins[i].pinmux);
		iomux = pins[i].iomux;

#if defined(CONFIG_SOC_FAMILY_MSPM0) || defined(CONFIG_SOC_FAMILY_MSPM33)
		/* Check for invalid pull-up/pull-down configuration */
		if (((iomux >> MSP_GPIO_RESISTOR_PULL_UP) & 0x1) &&
		    ((iomux >> MSP_GPIO_RESISTOR_PULL_DOWN) & 0x1)) {
			LOG_ERR("Pin CM%d: Cannot enable both pull-up and pull-down simultaneously",
				pin_cm);
			return -EINVAL;
		}

		if (pin_function == 0x00) {
			DL_GPIO_initPeripheralAnalogFunction(pin_cm);
		} else {
			DL_GPIO_initPeripheralFunction(pin_cm, (iomux | pin_function));
		}
#elif defined(CONFIG_SOC_SERIES_AM13E)
		/* Check for invalid pull-up/pull-down configuration */
		if (((iomux >> AM13E_GPIO_RESISTOR_PULL_UP) & 0x1) &&
		    ((iomux >> AM13E_GPIO_RESISTOR_PULL_DOWN) & 0x1)) {
			LOG_ERR("Pin CM%d: Cannot enable both pull-up and pull-down simultaneously",
				pin_cm);
			return -EINVAL;
		}

		if (pin_function == 0x00) {
			DL_GPIO_initPeripheralAnalogFunction(pin_cm);
		} else if ((iomux >> AM13E_GPIO_INPUT_ENABLE) & 0x1) {
			/* Pin has input-enable property, configure as peripheral input */
			DL_GPIO_initPeripheralInputFunction(pin_cm, pin_function);
		} else {
			/* Configure as peripheral output */
			DL_GPIO_initPeripheralOutputFunction(pin_cm, pin_function);
		}
#endif
	}
	return 0;
}
