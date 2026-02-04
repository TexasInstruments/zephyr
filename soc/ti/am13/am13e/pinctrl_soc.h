/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ZEPHYR_SOC_ARM_TI_AM13E_PINCTRL_SOC_H__
#define __ZEPHYR_SOC_ARM_TI_AM13E_PINCTRL_SOC_H__

#include <zephyr/devicetree.h>
#include <zephyr/types.h>
#include <zephyr/dt-bindings/pinctrl/msp-pinctrl.h>

#define AM13E_GPIO_RESISTOR_PULL_DOWN (16)
#define AM13E_GPIO_RESISTOR_PULL_UP   (17)
#define AM13E_GPIO_INPUT_ENABLE       (18)
#define AM13E_GPIO_HYSTERESIS_ENABLED (19)
#define AM13E_GPIO_HIGH_DRIVE         (20)
#define AM13E_GPIO_HIZ                (25)
#define AM13E_GPIO_INVERSION_ENABLED  (26)

#define AM13E_PINMUX_INIT(node_id) DT_PROP(node_id, pinmux)

#define AM13E_PIN_CONTROL_IOMUX_INIT(node_id)                                                      \
	((DT_PROP(node_id, bias_pull_up) << AM13E_GPIO_RESISTOR_PULL_UP) |                         \
	 (DT_PROP(node_id, bias_pull_down) << AM13E_GPIO_HIGH_DRIVE) |                             \
	 (DT_PROP(node_id, drive_open_drain) << AM13E_GPIO_HIZ) |                                  \
	 (DT_ENUM_IDX(node_id, drive_strength) << AM13E_GPIO_HIGH_DRIVE) |                         \
	 (DT_PROP(node_id, ti_hysteresis) << AM13E_GPIO_HYSTERESIS_ENABLED) |                      \
	 (DT_PROP(node_id, ti_invert) << AM13E_GPIO_INVERSION_ENABLED) |                           \
	 (DT_PROP(node_id, input_enable) << AM13E_GPIO_INPUT_ENABLE))

typedef struct pinctrl_soc_pin {
	/* PINCM register index and pin function */
	uint32_t pinmux;
	/* IOMUX Pin Control Management (direction, inversion, pullups) */
	uint32_t iomux;
} pinctrl_soc_pin_t;

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                               \
	{.pinmux = AM13E_PINMUX_INIT(DT_PROP_BY_IDX(node_id, prop, idx)),                          \
	 .iomux = AM13E_PIN_CONTROL_IOMUX_INIT(DT_PROP_BY_IDX(node_id, prop, idx))},

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT)}

#endif /* __ZEPHYR_SOC_ARM_TI_AM13E_PINCTRL_SOC_H__ */
