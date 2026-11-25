/*
 * Copyright (c) 2021, Commonwealth Scientific and Industrial Research
 * Organisation (CSIRO) ABN 41 687 119 230.
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

GROUP_START(FLASH_CCFG)

SECTION_PROLOGUE(.ti_ccfg,,)
{
	KEEP(*(.ccfg))
} GROUP_LINK_IN(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(ti_ccfg_partition)))

GROUP_END(FLASH_CCFG)
