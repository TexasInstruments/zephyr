/*
 * Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <xbar_configs.h>

#ifdef CONFIG_DMA
void configure_dma_xbars(void)
{

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart0), okay)
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart0), rx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart0), rx, channel),
		DMA_TRIG_XBAR_USART0_DMA_1); /* Rx */
#endif
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart0), tx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart0), tx, channel),
		DMA_TRIG_XBAR_USART0_DMA_0); /* Tx */
#endif
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(uart0), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart1), okay)
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart1), rx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart1), rx, channel),
		DMA_TRIG_XBAR_USART1_DMA_1); /* Rx */
#endif
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart1), tx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart1), tx, channel),
		DMA_TRIG_XBAR_USART1_DMA_0); /* Tx */
#endif
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(uart1), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart2), okay)
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart2), rx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart2), rx, channel),
		DMA_TRIG_XBAR_USART2_DMA_1); /* Rx */
#endif
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart2), tx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart2), tx, channel),
		DMA_TRIG_XBAR_USART2_DMA_0); /* Tx */
#endif
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(uart2), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart3), okay)
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart3), rx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart3), rx, channel),
		DMA_TRIG_XBAR_USART3_DMA_1); /* Rx */
#endif
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart3), tx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart3), tx, channel),
		DMA_TRIG_XBAR_USART3_DMA_0); /* Tx */
#endif
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(uart3), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart4), okay)
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart4), rx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart4), rx, channel),
		DMA_TRIG_XBAR_USART4_DMA_1); /* Rx */
#endif
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart4), tx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart4), tx, channel),
		DMA_TRIG_XBAR_USART4_DMA_0); /* Tx */
#endif
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(uart4), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart5), okay)
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart5), rx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart5), rx, channel),
		DMA_TRIG_XBAR_USART5_DMA_1); /* Rx */
#endif
#if DT_DMAS_HAS_NAME(DT_NODELABEL(uart5), tx)
	SOC_xbarSelectEdmaTrigXbarInputSource(
		CSL_EDMA_TRIG_XBAR_U_BASE, DT_DMAS_CELL_BY_NAME(DT_NODELABEL(uart5), tx, channel),
		DMA_TRIG_XBAR_USART5_DMA_0); /* Tx */
#endif
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(uart5), okay) */
}
#endif /* CONFIG_DMA */
