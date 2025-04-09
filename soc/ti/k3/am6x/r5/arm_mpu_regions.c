/*
 * Copyright (c) 2025 Texas Instruments
 * Copyright (c) 2025 Siemens Mobility GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/arch/arm/mpu/arm_mpu.h>

static const struct arm_mpu_region mpu_regions[] = {
#if defined CONFIG_SOC_AM2434_R5F
	MPU_REGION_ENTRY("Device", 0x0, REGION_2G, {MPU_RASR_S_Msk | NOT_EXEC | P_RW_U_RO_Msk}),
	/* Also explicitly allow executing the exception vector since the TCM might
	 * be disabled and therefor it might be not executable
	 */
	MPU_REGION_ENTRY(
		"Exception vector", 0x0, REGION_64B,
		{P_RO_U_NA_Msk | ((1 << MPU_RASR_TEX_Pos) | MPU_RASR_C_Msk | MPU_RASR_B_Msk)}),
#if DT_NODE_HAS_STATUS(DT_NODELABEL(mspi0), okay)
	MPU_REGION_ENTRY("FSS0", DT_REG_ADDR_BY_IDX(DT_NODELABEL(mspi0), 1), REGION_32B,
			 {P_RW_U_NA_Msk}),
#endif
#endif
};

const struct arm_mpu_config mpu_config = {
	.num_regions = ARRAY_SIZE(mpu_regions),
	.mpu_regions = mpu_regions,
};
