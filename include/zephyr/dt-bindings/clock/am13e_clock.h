/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_AM13E_CLOCK_H
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_AM13E_CLOCK_H

/* Clock reference IDs used in DTS:  clocks = <&ckm AM13E_CLOCK_xxx> */
#define AM13E_CLOCK_SYSOSC  0
#define AM13E_CLOCK_HFCLK   1
#define AM13E_CLOCK_SYSPLL0 2
#define AM13E_CLOCK_SYSPLL1 3
#define AM13E_CLOCK_HSCLK   4
#define AM13E_CLOCK_MCLK    5
#define AM13E_CLOCK_MCLK2   6
#define AM13E_CLOCK_MCLK4   7
#define AM13E_CLOCK_CANCLK  8
#define AM13E_CLOCK_LFOSC   9

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_AM13E_CLOCK_H */
