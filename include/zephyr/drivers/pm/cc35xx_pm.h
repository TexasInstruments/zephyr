/*
 * Copyright (c) 2026 Conclusive Engineering Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_PM_CC35XX_PM_H_
#define ZEPHYR_INCLUDE_DRIVERS_PM_CC35XX_PM_H_

enum cc35xx_pm_resource {
	CC35XX_PM_RESOURCE_DCAN = 0,
	CC35XX_PM_RESOURCE_GPTIMER0,
	CC35XX_PM_RESOURCE_GPTIMER1,
	CC35XX_PM_RESOURCE_I2C0,
	CC35XX_PM_RESOURCE_I2C1,
	CC35XX_PM_RESOURCE_I2S,
	CC35XX_PM_RESOURCE_SDMMC,
	CC35XX_PM_RESOURCE_SPI0,
	CC35XX_PM_RESOURCE_SPI1,
	CC35XX_PM_RESOURCE_SYSTIM,
	CC35XX_PM_RESOURCE_UARTLIN0,
	CC35XX_PM_RESOURCE_UARTLIN1,
	CC35XX_PM_RESOURCE_SDIO_CARD_FN1,
	CC35XX_PM_RESOURCE_ADC,
	CC35XX_PM_RESOURCE_UARTLIN2,
	CC35XX_PM_RESOURCE_COUNT,
};

/** Enable a peripheral clock and acquire its dependency reference. */
int cc35xx_pm_resource_get(enum cc35xx_pm_resource resource_id);
/** Release a dependency and gate the clock after its last user. */
int cc35xx_pm_resource_put(enum cc35xx_pm_resource resource_id);
/** Return the dependency count, or -EINVAL for an invalid resource. */
int cc35xx_pm_resource_refcount(enum cc35xx_pm_resource resource_id);

/** Restore clock enables for retained dependency references after standby. */
void cc35xx_pm_resources_resume(void);

#endif /* ZEPHYR_INCLUDE_DRIVERS_PM_CC35XX_PM_H_ */
