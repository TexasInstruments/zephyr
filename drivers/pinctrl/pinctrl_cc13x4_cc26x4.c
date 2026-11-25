/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_cc13x4_cc26x4_pinctrl

#include <zephyr/drivers/pinctrl.h>
#include <inc/hw_ioc.h>
#include <inc/hw_memmap.h>
#include <inc/hw_types.h>

#define LOG_LEVEL CONFIG_PINCTRL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pinctrl_cc13x4_cc26x4);

/* IOCFG0 starts at IOC_BASE directly */
#define IOC_ADDR(index) (IOC_BASE + (sizeof(uint32_t) * (index)))

static int pinctrl_cc13x4_cc26x4_set(uint32_t pin, uint32_t func, uint32_t mode)
{
	uint32_t iocfg_reg_addr = IOC_ADDR(pin);

	LOG_DBG("pin=%u func=0x%x mode=0x%x iocfg_reg_addr=0x%x", pin, func, mode, iocfg_reg_addr);

	HWREG(iocfg_reg_addr) = mode | func;

	return 0;
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

	LOG_DBG("Configuring %u pins", pin_cnt);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		pinctrl_cc13x4_cc26x4_set(pins[i].pin, pins[i].iofunc, pins[i].iomode);
	}

	return 0;
}
