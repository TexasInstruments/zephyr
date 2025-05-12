/* Copyright (C) 2023 BeagleBoard.org Foundation
 * Copyright (C) 2023 S Prashanth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/fatal.h>

#include "soc.h"
#include <common/ctrl_partitions.h>

unsigned int z_soc_irq_get_active(void)
{
	return z_vim_irq_get_active();
}

void z_soc_irq_eoi(unsigned int irq)
{
	z_vim_irq_eoi(irq);
}

void z_soc_irq_init(void)
{
	z_vim_irq_init();
}

void z_soc_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags)
{
	/* Configure interrupt type and priority */
	z_vim_irq_priority_set(irq, prio, flags);
}

void z_soc_irq_enable(unsigned int irq)
{
	/* Enable interrupt */
	z_vim_irq_enable(irq);
}

void z_soc_irq_disable(unsigned int irq)
{
	/* Disable interrupt */
	z_vim_irq_disable(irq);
}

int z_soc_irq_is_enabled(unsigned int irq)
{
	/* Check if interrupt is enabled */
	return z_vim_irq_is_enabled(irq);
}

void soc_early_init_hook(void)
{
	k3_unlock_all_ctrl_partitions();
}

/*
 * Copyright (c) 2025 Siemens Mobility GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm/mpu/arm_mpu.h>
#include <zephyr/arch/arm/cortex_a_r/mpu.h>
#include <zephyr/devicetree.h>

/* based off the TI MCU SDK default layout */
static const struct arm_mpu_region mpu_regions[] = {
	MPU_REGION_ENTRY("Base permissions", 0x0, ARM_MPU_REGION_SIZE_2GB << MPU_RASR_SIZE_Pos,
			 {MPU_RASR_S_Msk | MPU_RASR_XN_Msk | P_RW_U_RO_Msk}),
	MPU_REGION_ENTRY(
		"ATCM", DT_REG_ADDR(DT_NODELABEL(atcm)),
		ARM_MPU_REGION_SIZE_32KB << MPU_RASR_SIZE_Pos,
		{MPU_RASR_C_Msk | MPU_RASR_B_Msk | (1 << MPU_RASR_TEX_Pos) | P_RW_U_RO_Msk}),
	MPU_REGION_ENTRY(
		"BTCM", DT_REG_ADDR(DT_NODELABEL(btcm)),
		ARM_MPU_REGION_SIZE_32KB << MPU_RASR_SIZE_Pos,
		{MPU_RASR_C_Msk | MPU_RASR_B_Msk | (1 << MPU_RASR_TEX_Pos) | P_RW_U_RO_Msk}),
	/* protect SRAM generally so the memory of other cores can't be accessed by accident */
	MPU_REGION_ENTRY(
		"SRAM core partition", DT_REG_ADDR(DT_NODELABEL(msram)),
		ARM_MPU_REGION_SIZE_512KB << MPU_RASR_SIZE_Pos,
		{MPU_RASR_C_Msk | MPU_RASR_B_Msk | (1 << MPU_RASR_TEX_Pos) | P_RW_U_RO_Msk}),
	MPU_REGION_ENTRY("Base permissions1", 0x60000000, REGION_256M,
			 {MPU_RASR_C_Msk | MPU_RASR_B_Msk | MPU_RASR_TEX_Msk | RO_Msk}),
	/* Shared SRAM. Caching is disabled for now */
};

const struct arm_mpu_config mpu_config = {.num_regions = ARRAY_SIZE(mpu_regions),
					  .mpu_regions = mpu_regions};