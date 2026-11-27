/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief TI SimpleLink LPF3 clock gate compatibility helpers
 */

#ifndef _TI_LPF3_CLKCTL_H_
#define _TI_LPF3_CLKCTL_H_

#include <driverlib/clkctl.h>

/* CC23X1's CLKCTLEnable/Disable take only the peripheral argument, not the
 * base address. Provide a compatibility wrapper so the Zephyr drivers can use
 * the same call pattern as on CC23X0 and CC27XX.
 */
#if defined(CONFIG_SOC_SERIES_CC23X1)
#define CLKCTL_ENABLE(base, periph)  CLKCTLEnable(periph)
#define CLKCTL_DISABLE(base, periph) CLKCTLDisable(periph)
#else
#define CLKCTL_ENABLE(base, periph)  CLKCTLEnable(base, periph)
#define CLKCTL_DISABLE(base, periph) CLKCTLDisable(base, periph)
#endif

#endif /* _TI_LPF3_CLKCTL_H_ */
