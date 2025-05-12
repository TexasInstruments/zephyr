/*
 * Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/arch/common/sys_io.h>
#include <zephyr/drivers/mm/system_mm.h>

#define KICK0_OFFSET     (0x1008)
#define KICK1_OFFSET     (0x100C)
#define KICK0_UNLOCK_VAL (0x68EF3490U)
#define KICK1_UNLOCK_VAL (0xD172BC5AU)
#define KICK_LOCK_VAL    (0x0U)

const uintptr_t ctrl_partitions[] = {
#if defined CONFIG_SOC_AM6234_M4 || defined CONFIG_SOC_AM6442_M4 || defined CONFIG_SOC_AM2434_M4 || defined CONFIG_SOC_AM2434_R5
	0x4080000, /* mcu/wkup padcfg 1 */
	0x4084000, /* mcu/wkup padcfg 2 */
#endif

#if defined CONFIG_SOC_AM2434_R5
	0xf0000, /* main padcfg 1 */
	0xf4000, /* main padcfg 2 */
#endif
};

static uintptr_t translate_addr(uintptr_t phys)
{
#ifdef CONFIG_MM_TI_RAT
	uintptr_t virt;
	sys_mm_drv_page_phys_get((void *)phys, &virt);
	return virt;
#else
	return phys;
#endif
}

void k3_unlock_all_ctrl_partitions(void)
{
	ARRAY_FOR_EACH(ctrl_partitions, i) {
		uintptr_t base_addr = translate_addr(ctrl_partitions[i]);
		sys_write32(KICK0_UNLOCK_VAL, base_addr + KICK0_OFFSET);
		sys_write32(KICK1_UNLOCK_VAL, base_addr + KICK1_OFFSET);
	}
}
