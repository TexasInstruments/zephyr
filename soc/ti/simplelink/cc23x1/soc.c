/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <driverlib/setup.h>

/* This empty function is required by ti_drivers_config.c
 * placed here in case power management is disabled
 */
void customPolicyFxn(void)
{
}

void soc_reset_hook(void)
{
	/* Perform necessary trim of the device. */
	SetupTrimDevice();
}
