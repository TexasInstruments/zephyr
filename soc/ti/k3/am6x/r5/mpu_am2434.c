/*
 * Copyright (c) 2025 Siemens Mobility GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm/mpu/arm_mpu.h>
#include <zephyr/arch/arm/cortex_a_r/mpu.h>
#include <zephyr/devicetree.h>

/* based off the TI MCU+ SDK default layout */
static const struct arm_mpu_region mpu_regions[] = {
	MPU_REGION_ENTRY("Base permissions", 0x0, ARM_MPU_REGION_SIZE_2GB << MPU_RASR_SIZE_Pos,
			 {MPU_RASR_S_Msk | MPU_RASR_XN_Msk | P_RW_U_RO_Msk}),
	MPU_REGION_ENTRY(
		"ATCM", DT_REG_ADDR(DT_NODELABEL(atcm_boot)),
		ARM_MPU_REGION_SIZE_32KB << MPU_RASR_SIZE_Pos,
		{MPU_RASR_C_Msk | MPU_RASR_B_Msk | (1 << MPU_RASR_TEX_Pos) | P_RW_U_RO_Msk}),
	MPU_REGION_ENTRY(
		"BTCM", DT_REG_ADDR(DT_NODELABEL(btcm)),
		ARM_MPU_REGION_SIZE_32KB << MPU_RASR_SIZE_Pos,
		{MPU_RASR_C_Msk | MPU_RASR_B_Msk | (1 << MPU_RASR_TEX_Pos) | P_RW_U_RO_Msk}),
	/* protect SRAM generally so the memory of other cores can't be accessed by accident */
	MPU_REGION_ENTRY(
		"SRAM other", 0x70000000, ARM_MPU_REGION_SIZE_2MB << MPU_RASR_SIZE_Pos,
		{MPU_RASR_C_Msk | MPU_RASR_B_Msk | (1 << MPU_RASR_TEX_Pos) | NO_ACCESS_Msk}),
	/* SRAM partition the core exclusively runs on; no other core should access it */
	MPU_REGION_ENTRY(
		"SRAM core partition", DT_REG_ADDR(DT_NODELABEL(sram_core)),
		ARM_MPU_REGION_SIZE_256KB << MPU_RASR_SIZE_Pos,
		{MPU_RASR_C_Msk | MPU_RASR_B_Msk | (1 << MPU_RASR_TEX_Pos) | P_RW_U_RO_Msk}),
	/* Shared SRAM. Caching is disabled for now */
	MPU_REGION_ENTRY("SRAM shared", DT_REG_ADDR(DT_NODELABEL(sram_shared)),
			 ARM_MPU_REGION_SIZE_256KB << MPU_RASR_SIZE_Pos,
			 {(0b100 << MPU_RASR_TEX_Pos) | MPU_RASR_S_Msk | P_RW_U_RO_Msk}),
};

const struct arm_mpu_config mpu_config = {.num_regions = ARRAY_SIZE(mpu_regions),
					  .mpu_regions = mpu_regions};
