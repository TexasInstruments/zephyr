/*
 * Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SERIAL_UART_NS16550_TI_K3_H_
#define ZEPHYR_INCLUDE_DRIVERS_SERIAL_UART_NS16550_TI_K3_H_

/* UART Register offsets */
#define UART_DLL ((uint32_t)0x0U)
#define UART_DLH ((uint32_t)0x4U)
#define UART_IER ((uint32_t)0x4U)
#define UART_FCR ((uint32_t)0x8U)
#define UART_EFR ((uint32_t)0x8U)
#define UART_LCR ((uint32_t)0xcU)
#define UART_MCR ((uint32_t)0x10U)
#define UART_TLR ((uint32_t)0x1cU)
#define UART_SCR ((uint32_t)0x40U)

/* Values to be written to LCR register to switch between modes */
#define UART_REG_CONFIG_MODE_A    ((uint32_t)0x0080)
#define UART_REG_CONFIG_MODE_B    ((uint32_t)0x00BF)
#define UART_REG_OPERATIONAL_MODE ((uint32_t)0x007F)

/* Register Mask for DLL, DLH registers */
#define UART_DLL_CLOCK_LSB_MASK ((uint32_t)0x000000ffU)
#define UART_DLH_CLOCK_MSB_MASK ((uint32_t)0x0000003fU)

/* Bitmask and shift for EFR register to enable Enhanced Write */
#define UART_EFR_ENHANCED_EN_SHIFT ((uint32_t)4U)
#define UART_EFR_ENHANCED_EN_MASK  ((uint32_t)0x00000010U)

/* Bitmask and shift for MCR register to enable TCR_TLR submode */
#define UART_MCR_TCR_TLR_SHIFT ((uint32_t)6U)
#define UART_MCR_TCR_TLR_MASK  ((uint32_t)0x00000040U)

/* Bitmask and shift for MDR1 register  */
#define UART_MDR1                   ((uint32_t)0x20U)
#define UART_MDR1_MODE_SELECT_SHIFT ((uint32_t)0U)
#define UART_MDR1_MODE_SELECT_MASK  ((uint32_t)0x00000007U)

/* Bitmask and shift for IER register to toggle sleep */
#define UART_IER_SLEEP_MODE_SHIFT ((uint32_t)4U)
#define UART_IER_SLEEP_MODE_MASK  ((uint32_t)0x00000010U)

/* Helper Macros for SCR register */
#define UART_SCR_TX_TRIG_GRANU1_SHIFT ((uint32_t)6U)
#define UART_SCR_TX_TRIG_GRANU1_MASK  ((uint32_t)0x00000040U)
#define UART_SCR_RX_TRIG_GRANU1_SHIFT ((uint32_t)7U)
#define UART_SCR_RX_TRIG_GRANU1_MASK  ((uint32_t)0x00000080U)
#define UART_SCR_DMA_MODE_CTL_SHIFT   ((uint32_t)0U)
#define UART_SCR_DMA_MODE_CTL_MASK    ((uint32_t)0x00000001U)
#define UART_SCR_DMA_MODE_2_SHIFT     ((uint32_t)1U)
#define UART_SCR_DMA_MODE_2_MASK      ((uint32_t)0x00000006U)

/* Helper Macros for FCR register */
#define UART_FCR_DMA_MODE_SHIFT     ((uint32_t)3U)
#define UART_FCR_DMA_MODE_MASK      ((uint32_t)0x00000008U)
#define UART_FCR_FIFO_EN_MASK       0x1
#define UART_FCR_RX_FIFO_TRIG_SHIFT ((uint32_t)6U)
#define UART_FCR_TX_FIFO_TRIG_SHIFT ((uint32_t)4U)

/*
 * Helper macros to define 32-bit value containing entire FIFO DMA configs
 *
 * txGra - Set to 1 to enable granularity in defining Tx trigger level
 * rxGra - Set to 1 to enable granularity in defining Rx trigger level
 * txTrig - Tx Trigger level
 * rxTrig - Rx trigger leve
 * txClr - Set to 1 to clear Tx FIFO during config
 * rxClr - Set to 1 to clear Rx FIFO during config
 * dmaEnPath - Set to 1 to write to FCR, else write to SCR
 * dmaMode - DMA Modes from 0 to 3 (Refer TRM for details)
 */

#define UART_FIFO_CONFIG_TXGRA_SHIFT     (26)
#define UART_FIFO_CONFIG_RXGRA_SHIFT     (22)
#define UART_FIFO_CONFIG_TXTRIG_SHIFT    (14)
#define UART_FIFO_CONFIG_RXTRIG_SHIFT    (6)
#define UART_FIFO_CONFIG_TXCLR_SHIFT     (5)
#define UART_FIFO_CONFIG_RXCLR_SHIFT     (4)
#define UART_FIFO_CONFIG_DMAENPATH_SHIFT (3)
#define UART_FIFO_CONFIG_DMAMODE_SHIFT   (0)

#define UART_FIFO_CONFIG_TXGRA_MASK     ((uint32_t)0xFU << UART_FIFO_CONFIG_TXGRA_SHIFT)
#define UART_FIFO_CONFIG_RXGRA_MASK     ((uint32_t)0xFU << UART_FIFO_CONFIG_RXGRA_SHIFT)
#define UART_FIFO_CONFIG_TXTRIG_MASK    ((uint32_t)0xFFU << UART_FIFO_CONFIG_TXTRIG_SHIFT)
#define UART_FIFO_CONFIG_RXTRIG_MASK    ((uint32_t)0xFFU << UART_FIFO_CONFIG_RXTRIG_SHIFT)
#define UART_FIFO_CONFIG_TXCLR_MASK     ((uint32_t)0x1U << UART_FIFO_CONFIG_TXCLR_SHIFT)
#define UART_FIFO_CONFIG_RXCLR_MASK     ((uint32_t)0x1U << UART_FIFO_CONFIG_RXCLR_SHIFT)
#define UART_FIFO_CONFIG_DMAENPATH_MASK ((uint32_t)0x1U << UART_FIFO_CONFIG_DMAENPATH_SHIFT)
#define UART_FIFO_CONFIG_DMAMODE_MASK   ((uint32_t)0x7U << UART_FIFO_CONFIG_DMAMODE_SHIFT)

#define UART_FIFO_CONFIG(txGra, rxGra, txTrig, rxTrig, txClr, rxClr, dmaEnPath, dmaMode)           \
	(((uint32_t)(txGra & 0xFU) << UART_FIFO_CONFIG_TXGRA_SHIFT) |                              \
	 ((uint32_t)(rxGra & 0xFU) << UART_FIFO_CONFIG_RXGRA_SHIFT) |                              \
	 ((uint32_t)(txTrig & 0xFFU) << UART_FIFO_CONFIG_TXTRIG_SHIFT) |                           \
	 ((uint32_t)(rxTrig & 0xFFU) << UART_FIFO_CONFIG_RXTRIG_SHIFT) |                           \
	 ((uint32_t)(txClr & 0x1U) << UART_FIFO_CONFIG_TXCLR_SHIFT) |                              \
	 ((uint32_t)(rxClr & 0x1U) << UART_FIFO_CONFIG_RXCLR_SHIFT) |                              \
	 ((uint32_t)(dmaEnPath & 0x1U) << UART_FIFO_CONFIG_DMAENPATH_SHIFT) |                      \
	 ((uint32_t)(dmaMode & 0x7U)) << UART_FIFO_CONFIG_DMAMODE_SHIFT)

#define UART_TRIG_LVL_GRANULARITY_1 ((uint32_t)0x0001U)

#define UART_DMA_EN_PATH_FCR   (0)
#define UART_DMA_MODE_1_ENABLE (1)

#define UART_FIFO_CONFIG_TRIGLVL_SCR_MASK ((uint32_t)0x3c)
#define UART_FIFO_CONFIG_TRIGLVL_FCR_MASK ((uint32_t)0x3)
#define UART_FIFO_CONFIG_TRIGLVL_MASK     ((uint32_t)0x3f)

#endif /* ZEPHYR_INCLUDE_DRIVERS_SERIAL_UART_NS16550_TI_K3_H_ */
