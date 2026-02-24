/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This linker file contains sections responsible for early boot.
 * This section must be placed in RAM for accessing Flash of AM261x SoC.
 */

SECTION_PROLOGUE(early_boot_section,(NOLOAD),)
{
    . = ALIGN(_region_min_align);
    __early_boot_section_start = .;
    *libarch__arm__core__cortex_a_r.a:(*)
    *libarch__arm__core__mpu.a:(*)
    *libarch__arm__core.a:(*)

    *(*memcpy*)
    *(*memset*)
    *(*__aeabi_memcpy*)
    *(*__aeabi_memcpy4*)
    *(*__aeabi_memcpy8*)

    *libzephyr.a:(.rodata.mpu_config)
    *libzephyr.a:(.rodata.mpu_regions)
    *libzephyr.a:(.soc_early_boot)


    *(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)

    __early_boot_section_end = .;
} GROUP_LINK_IN(RAMABLE_REGION)
