/*
 * Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <clocks_configs.h>

void configure_soc_clocks(void)
{

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart0), okay)
	sys_write32(0x777, CSL_MSS_RCM_LIN0_UART0_CLK_SRC_SEL);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart1), okay)
	sys_write32(0x777, CSL_MSS_RCM_LIN1_UART1_CLK_SRC_SEL);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart2), okay)
	sys_write32(0x777, CSL_MSS_RCM_LIN2_UART2_CLK_SRC_SEL);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart3), okay)
	sys_write32(0x777, CSL_MSS_RCM_LIN3_UART3_CLK_SRC_SEL);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart4), okay)
	sys_write32(0x777, CSL_MSS_RCM_LIN4_UART4_CLK_SRC_SEL);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart5), okay)
	sys_write32(0x777, CSL_MSS_RCM_LIN5_UART5_CLK_SRC_SEL);
#endif
}
