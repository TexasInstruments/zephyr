/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <dl_fri.h>

static void FLASH_init(void)
{
	/* Disable cache before changing wait states */
	DL_FRI_disableDLB();
	DL_FRI_disableCache();

	/* Set flash wait states appropriate for 200 MHz operation */
	DL_FRI_setReadWaitStates(0x3);

	/* Re-enable cache for improved code-execution performance */
	DL_FRI_enableDLB();
	DL_FRI_enableCache();
}

/*
 * soc_early_init_hook runs before any device initialization, including the
 * clock control driver.  Configure flash wait states here so the CPU can
 * safely execute at 200 MHz once the clock driver switches to the SYSPLL.
 *
 * Clock tree configuration (HFXT, SYSPLL, MCLK switch) is handled by the
 * clock_control_am13e driver at PRE_KERNEL_1 priority.
 */
void soc_early_init_hook(void)
{
	FLASH_init();
}
