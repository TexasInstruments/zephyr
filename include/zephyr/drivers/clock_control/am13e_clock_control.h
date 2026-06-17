/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_AM13E_CLOCK_CONTROL_H
#define ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_AM13E_CLOCK_CONTROL_H

#include <zephyr/dt-bindings/clock/am13e_clock.h>

/**
 * @brief Clock subsystem descriptor passed to clock_control_get_rate().
 *
 * Peripheral drivers embed this in their config and pass a pointer to it
 * as the clock_control_subsys_t argument:
 *
 *   static const struct am13e_sys_clock my_clk = AM13E_CLOCK_SUBSYS(AM13E_CLOCK_MCLK4);
 *   clock_control_get_rate(ckm_dev, (clock_control_subsys_t)&my_clk, &rate);
 */
struct am13e_sys_clock {
	uint32_t clk;
};

#define AM13E_CLOCK_SUBSYS(clock_id) {.clk = (clock_id)}

/** Convenience macro for use in peripheral driver config structs. */
#define AM13E_CLOCK_SUBSYS_INST(index) {.clk = DT_INST_CLOCKS_CELL(index, clk)}

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_AM13E_CLOCK_CONTROL_H */
