/*
 * Copyright (c) 2026 Conclusive Engineering Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Peripheral clock-gating helpers for CC35XXE. They control per-peripheral
 * CLKCFG bits used by the UART/I2C/SPI/... drivers. Suspend/resume handling
 * lives in power.c.
 */

#include <errno.h>
#include <stdint.h>
#include <zephyr/spinlock.h>
#include <zephyr/arch/common/sys_io.h>
#include <zephyr/sys/util.h>

#include <zephyr/drivers/pm/cc35xx_pm.h>

#include <inc/hw_adc.h>
#include <inc/hw_dcan.h>
#include <inc/hw_gptimer.h>
#include <inc/hw_i2c.h>
#include <inc/hw_i2s.h>
#include <inc/hw_memmap.h>
#include <inc/hw_sdio_card_fn1.h>
#include <inc/hw_sdmmc.h>
#include <inc/hw_spi.h>
#include <inc/hw_systim.h>
#include <inc/hw_types.h>
#include <inc/hw_uartlin.h>

static struct k_spinlock resource_lock;
static uint8_t resource_counts[CC35XX_PM_RESOURCE_COUNT];
static uint32_t resource_enable_mask;

static const uint32_t resource_clk_en_regs[CC35XX_PM_RESOURCE_COUNT] = {
	DCAN_BASE + DCAN_O_CLKCFG,
	GPTIMER0_BASE + GPTIMER_O_CLKCFG,
	GPTIMER1_BASE + GPTIMER_O_CLKCFG,
	I2C0_BASE + I2C_O_CLKCFG,
	I2C1_BASE + I2C_O_CLKCFG,
	I2S_BASE + I2S_O_CLKCFG,
	SDMMC_BASE + SDMMC_O_CLKCFG,
	SPI0_BASE + SPI_O_CLKCFG,
	SPI1_BASE + SPI_O_CLKCFG,
	SYSTIM_BASE + SYSTIM_O_CLKCFG,
	UARTLIN0_BASE + UARTLIN_O_CLKCFG,
	UARTLIN1_BASE + UARTLIN_O_CLKCFG,
	SDIO_CARD_FN1_BASE + SDIO_CARD_FN1_O_CLKEN,
	ADC_BASE + ADC_O_CLKCFG,
	UARTLIN2_BASE + UARTLIN_O_CLKCFG,
};

static uint32_t resource_clock_config(enum cc35xx_pm_resource resource_id)
{
	if (resource_id == CC35XX_PM_RESOURCE_DCAN) {
		return DCAN_CLKCFG_CLKEN | DCAN_CLKCFG_RAMEN |
		       DCAN_CLKCFG_CLKSEL_HOST_DIV2_CLK;
	}
	return 1U;
}

int cc35xx_pm_resource_get(enum cc35xx_pm_resource resource_id)
{
	k_spinlock_key_t key;
	int ret = 0;

	if ((unsigned int)resource_id >= CC35XX_PM_RESOURCE_COUNT) {
		return -EINVAL;
	}
	key = k_spin_lock(&resource_lock);
	if (resource_counts[resource_id] == UINT8_MAX) {
		ret = -EOVERFLOW;
	} else if (resource_counts[resource_id]++ == 0U) {
		sys_write32(resource_clock_config(resource_id), resource_clk_en_regs[resource_id]);
		resource_enable_mask |= BIT(resource_id);
	}
	k_spin_unlock(&resource_lock, key);
	return ret;
}

int cc35xx_pm_resource_put(enum cc35xx_pm_resource resource_id)
{
	k_spinlock_key_t key;
	int ret = 0;

	if ((unsigned int)resource_id >= CC35XX_PM_RESOURCE_COUNT) {
		return -EINVAL;
	}
	key = k_spin_lock(&resource_lock);
	if (resource_counts[resource_id] == 0U) {
		ret = -EALREADY;
	} else if (--resource_counts[resource_id] == 0U) {
		sys_write32(0U, resource_clk_en_regs[resource_id]);
		resource_enable_mask &= ~BIT(resource_id);
	}
	k_spin_unlock(&resource_lock, key);
	return ret;
}

int cc35xx_pm_resource_refcount(enum cc35xx_pm_resource resource_id)
{
	k_spinlock_key_t key;
	int count;

	if ((unsigned int)resource_id >= CC35XX_PM_RESOURCE_COUNT) {
		return -EINVAL;
	}
	key = k_spin_lock(&resource_lock);
	count = resource_counts[resource_id];
	k_spin_unlock(&resource_lock, key);
	return count;
}

void cc35xx_pm_resources_resume(void)
{
	k_spinlock_key_t key = k_spin_lock(&resource_lock);

	for (uint32_t id = 0U; id < CC35XX_PM_RESOURCE_COUNT; id++) {
		if ((resource_enable_mask & BIT(id)) != 0U) {
			sys_write32(resource_clock_config(id), resource_clk_en_regs[id]);
		}
	}
	k_spin_unlock(&resource_lock, key);
}
