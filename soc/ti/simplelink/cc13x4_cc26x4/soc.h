/*
 * Copyright (c) 2019 Brett Witherspoon
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TI_SIMPLELINK_CC13X4_CC26X4_SOC_H_
#define TI_SIMPLELINK_CC13X4_CC26X4_SOC_H_

#include <zephyr/arch/arm/cortex_m/nvic.h>

typedef enum {
	Reset_IRQn = -15,
	NonMaskableInt_IRQn = -14,
	HardFault_IRQn = -13,
	MemoryManagement_IRQn = -12,
	BusFault_IRQn = -11,
	UsageFault_IRQn = -10,
	SecureFault_IRQn = -9,
	SVCall_IRQn = -5,
	DebugMonitor_IRQn = -4,
	PendSV_IRQn = -2,
	SysTick_IRQn = -1,
} IRQn_Type;

#define __CM33_REV             0x0004U
#define __MPU_PRESENT          1
#define __NVIC_PRIO_BITS       NUM_IRQ_PRIO_BITS
#define __Vendor_SysTickConfig 0
#define __FPU_PRESENT          1
#define __DSP_PRESENT          1
#define __SAUREGION_PRESENT    0

#include <core_cm33.h>
#endif /* TI_SIMPLELINK_CC13X4_CC26X4_SOC_H_ */
