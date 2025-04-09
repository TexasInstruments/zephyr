/* Copyright (C) 2023 BeagleBoard.org Foundation
 * Copyright (C) 2023 S Prashanth
 * Copyright (C) 2025 Siemens Mobility GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/fatal.h>

#include "soc.h"
#include "zephyr/arch/arm/cortex_a_r/sys_io.h"
#include "zephyr/kernel.h"
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

/* TODO: Use real TISCI driver when it's implemented */
#ifdef CONFIG_SOC_AM2434_R5F0_0_BOOT

#define TISCI_DATA_BASE_ADDRESS        0x4d000000
#define TISCI_THREAD_INFO_BASE_ADDRESS 0x4A600000

/* we need to wait for the DMSC-L boot notification as early as possible since
 * some instability occurs otherwise
 */
void soc_prep_hook(void)
{
	/* check whether the error bit is set despite having no message sent already */
	if (sys_read32(TISCI_THREAD_INFO_BASE_ADDRESS) & BIT(31)) {
		k_panic();
	}

	/* wait for any message */
	while ((sys_read32(TISCI_THREAD_INFO_BASE_ADDRESS) & 0xFF) == 0) {
		/* wait actively */
	}

	/* read message type part of the message header */
	uint16_t type = sys_read16(TISCI_DATA_BASE_ADDRESS + 8);

	/* verify it's a successful boot notification from the DMSC-L */
	if (type != 0x000A) {
		k_panic();
	}

	/* read last part of message to signal we finished receiving the message */
	(void)sys_read32(TISCI_DATA_BASE_ADDRESS + 4 + (4 * 14));
}
#endif
