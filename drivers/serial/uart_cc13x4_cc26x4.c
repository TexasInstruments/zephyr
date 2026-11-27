/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_cc13x4_cc26x4_uart

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>
#include <driverlib/uart.h>
#include <driverlib/prcm.h>

#ifdef CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN
#include <zephyr/drivers/dma.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/sys/sys_io.h>
#include <inc/hw_udma.h>

/*
 * Burst length = half the 8-entry FIFO (4 items).
 * FIFO RX trigger is set to UART_FIFO_RX4_8 (1/2 full) in async_rx_enable
 * so that burst requests match this value.
 */
#define UART_CC13X4_BURST_LEN 4

/*
 * TRM section 16.3.10: "Completion interrupt on peripheral's IRQ vector."
 *
 * On CC13x4/CC26x4, when DMA finishes a UART peripheral channel transfer,
 * the hardware fires the UART's own IRQ (not IRQ 40).  However, the CC13x4
 * UART ICSR register has NO DMA-done status bits (those exist only on the
 * newer CC23x0/CC27xx LPF3 UART IP).  UARTIntStatus() therefore returns 0
 * for DMA completion - the signal source is the REQDONE register in the uDMA
 * controller (base 0x40020000, offset 0x504), where one bit per channel is
 * set sticky until software clears it.
 *
 * Correct detection sequence in the UART ISR:
 *   1. sys_read32(UDMA0_BASE + UDMA_O_REQDONE) & BIT(channel)  - was it this channel?
 *   2. sys_write32(BIT(channel), UDMA0_BASE + UDMA_O_REQDONE)  - clear sticky bit first
 *   3. Handle UART_TX_DONE / UART_RX_RDY                       - notify application
 *   4. UARTIntClear(reg, any pending UART flags)               - clean up UART side
 */

/* DT helpers - uart node must have dmas + dma-names = "tx","rx" */
#define UART_CC13X4_DT_INST_DMA_CTLR(n, name)    DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, name))
#define UART_CC13X4_DT_INST_DMA_CHANNEL(n, name) DT_INST_DMAS_CELL_BY_NAME(n, name, channel)
#endif /* CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN */

/* Compile-time check: does any uart instance in this build have a dmas property? */
#define _UART_CC13X4_INST_HAS_DMAS_OR(n) || DT_INST_NODE_HAS_PROP(n, dmas)
#define UART_CC13X4_ANY_INST_HAS_DMAS (0 DT_INST_FOREACH_STATUS_OKAY(_UART_CC13X4_INST_HAS_DMAS_OR))

/* ------------------------------------------------------------------ */
/* Config struct                                                        */
/* ------------------------------------------------------------------ */
struct uart_cc13x4_cc26x4_config {
	uint32_t reg;
	uint32_t sys_clk_freq;
	uint32_t prcm_domain;
	uint32_t prcm_periph;
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN)
	unsigned int irq;
#endif
#ifdef CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN
	const struct device *dma_dev;
	uint8_t dma_channel_tx;
	uint8_t dma_channel_rx;
#endif
};

/* ------------------------------------------------------------------ */
/* PM lock types                                                        */
/* ------------------------------------------------------------------ */
enum uart_cc13x4_cc26x4_pm_locks {
	UART_CC13X4_CC26X4_PM_LOCK_TX,
	UART_CC13X4_CC26X4_PM_LOCK_RX,
	UART_CC13X4_CC26X4_PM_LOCK_COUNT,
};

/* ------------------------------------------------------------------ */
/* Data struct                                                          */
/* ------------------------------------------------------------------ */
struct uart_cc13x4_cc26x4_data {
	struct uart_config uart_config;
	const struct pinctrl_dev_config *pcfg;
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN)
	uart_irq_callback_user_data_t callback;
	void *user_data;
#endif
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	bool tx_irq_enabled;
#endif
#ifdef CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN
	const struct device *dev;

	uart_callback_t async_callback;
	void *async_user_data;

	struct k_work_delayable tx_timeout_work;
	const uint8_t *tx_buf;
	size_t tx_len;

	uint8_t *rx_buf;
	size_t rx_len;
	size_t rx_processed_len;
	uint8_t *rx_next_buf;
	size_t rx_next_len;
#endif
#ifdef CONFIG_PM_DEVICE
	ATOMIC_DEFINE(pm_lock, UART_CC13X4_CC26X4_PM_LOCK_COUNT);
#endif
};

/* ------------------------------------------------------------------ */
/* PM helpers                                                           */
/* ------------------------------------------------------------------ */
#ifdef CONFIG_PM_DEVICE
static inline void uart_cc13x4_cc26x4_pm_lock_get(struct uart_cc13x4_cc26x4_data *data,
						  enum uart_cc13x4_cc26x4_pm_locks type)
{
	if (!atomic_test_and_set_bit(data->pm_lock, type)) {
		pm_policy_state_lock_get(PM_STATE_RUNTIME_IDLE, PM_ALL_SUBSTATES);
		pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
	}
}

static inline void uart_cc13x4_cc26x4_pm_lock_put(struct uart_cc13x4_cc26x4_data *data,
						  enum uart_cc13x4_cc26x4_pm_locks type)
{
	if (atomic_test_and_clear_bit(data->pm_lock, type)) {
		pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		pm_policy_state_lock_put(PM_STATE_RUNTIME_IDLE, PM_ALL_SUBSTATES);
	}
}
#endif /* CONFIG_PM_DEVICE */

/* ------------------------------------------------------------------ */
/* Clock helpers                                                        */
/* ------------------------------------------------------------------ */
static void uart_cc13x4_cc26x4_clock_enable(const struct uart_cc13x4_cc26x4_config *config)
{
	PRCMPowerDomainOn(config->prcm_domain);
	while (PRCMPowerDomainsAllOn(config->prcm_domain) != PRCM_DOMAIN_POWER_ON) {
	}
	PRCMPeripheralRunEnable(config->prcm_periph);
	PRCMPeripheralSleepEnable(config->prcm_periph);
	PRCMPeripheralDeepSleepEnable(config->prcm_periph);
	PRCMLoadSet();
	while (!PRCMLoadGet()) {
	}
}

#ifdef CONFIG_PM_DEVICE
static void uart_cc13x4_cc26x4_clock_disable(const struct uart_cc13x4_cc26x4_config *config)
{
	PRCMPeripheralRunDisable(config->prcm_periph);
	PRCMLoadSet();
	while (!PRCMLoadGet()) {
	}
}
#endif

/* ------------------------------------------------------------------ */
/* Poll I/O                                                             */
/* ------------------------------------------------------------------ */
static int uart_cc13x4_cc26x4_poll_in(const struct device *dev, unsigned char *c)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

	if (!UARTCharsAvail(config->reg)) {
		return -1;
	}
	*c = (unsigned char)UARTCharGetNonBlocking(config->reg);
	return 0;
}

static void uart_cc13x4_cc26x4_poll_out(const struct device *dev, unsigned char c)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

	UARTCharPut(config->reg, c);
#ifdef CONFIG_PM_DEVICE
	/* Wait for character to be transmitted to ensure CPU
	 * does not enter standby when UART is busy
	 */
	while (UARTBusy(config->reg)) {
	}
#endif
}

static int uart_cc13x4_cc26x4_err_check(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	uint32_t flags = UARTRxErrorGet(config->reg);
	int error = 0;

	error |= (flags & UART_RXERROR_FRAMING) ? UART_ERROR_FRAMING : 0;
	error |= (flags & UART_RXERROR_PARITY) ? UART_ERROR_PARITY : 0;
	error |= (flags & UART_RXERROR_BREAK) ? UART_BREAK : 0;
	error |= (flags & UART_RXERROR_OVERRUN) ? UART_ERROR_OVERRUN : 0;

	UARTRxErrorClear(config->reg);
	return error;
}

/* ------------------------------------------------------------------ */
/* Configure                                                            */
/* ------------------------------------------------------------------ */
static int uart_cc13x4_cc26x4_configure(const struct device *dev, const struct uart_config *cfg)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	struct uart_cc13x4_cc26x4_data *data = dev->data;
	uint32_t line_ctrl = 0;
	bool flow_ctrl;

	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		line_ctrl |= UART_CONFIG_PAR_NONE;
		break;
	case UART_CFG_PARITY_ODD:
		line_ctrl |= UART_CONFIG_PAR_ODD;
		break;
	case UART_CFG_PARITY_EVEN:
		line_ctrl |= UART_CONFIG_PAR_EVEN;
		break;
	case UART_CFG_PARITY_MARK:
		line_ctrl |= UART_CONFIG_PAR_ONE;
		break;
	case UART_CFG_PARITY_SPACE:
		line_ctrl |= UART_CONFIG_PAR_ZERO;
		break;
	default:
		return -EINVAL;
	}

	switch (cfg->stop_bits) {
	case UART_CFG_STOP_BITS_1:
		line_ctrl |= UART_CONFIG_STOP_ONE;
		break;
	case UART_CFG_STOP_BITS_2:
		line_ctrl |= UART_CONFIG_STOP_TWO;
		break;
	case UART_CFG_STOP_BITS_0_5:
	case UART_CFG_STOP_BITS_1_5:
		return -ENOTSUP;
	default:
		return -EINVAL;
	}

	switch (cfg->data_bits) {
	case UART_CFG_DATA_BITS_5:
		line_ctrl |= UART_CONFIG_WLEN_5;
		break;
	case UART_CFG_DATA_BITS_6:
		line_ctrl |= UART_CONFIG_WLEN_6;
		break;
	case UART_CFG_DATA_BITS_7:
		line_ctrl |= UART_CONFIG_WLEN_7;
		break;
	case UART_CFG_DATA_BITS_8:
		line_ctrl |= UART_CONFIG_WLEN_8;
		break;
	default:
		return -EINVAL;
	}

	switch (cfg->flow_ctrl) {
	case UART_CFG_FLOW_CTRL_NONE:
		flow_ctrl = false;
		break;
	case UART_CFG_FLOW_CTRL_RTS_CTS:
		flow_ctrl = true;
		break;
	case UART_CFG_FLOW_CTRL_DTR_DSR:
		return -ENOTSUP;
	default:
		return -EINVAL;
	}

	UARTConfigSetExpClk(config->reg, config->sys_clk_freq, cfg->baudrate, line_ctrl);
	UARTIntClear(config->reg, UART_INT_OE | UART_INT_BE | UART_INT_PE | UART_INT_FE |
					  UART_INT_RT | UART_INT_TX | UART_INT_RX | UART_INT_CTS);

	if (flow_ctrl) {
		UARTHwFlowControlEnable(config->reg);
	} else {
		UARTHwFlowControlDisable(config->reg);
	}

	UARTEnable(config->reg);
	UARTFIFODisable(config->reg);

	data->uart_config = *cfg;
	return 0;
}

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
static int uart_cc13x4_cc26x4_config_get(const struct device *dev, struct uart_config *cfg)
{
	*cfg = ((struct uart_cc13x4_cc26x4_data *)dev->data)->uart_config;
	return 0;
}
#endif

/* ------------------------------------------------------------------ */
/* Interrupt-driven API                                                 */
/* ------------------------------------------------------------------ */
#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static int uart_cc13x4_cc26x4_fifo_fill(const struct device *dev, const uint8_t *buf, int len)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	int n = 0;

	while (n < len && UARTSpaceAvail(config->reg)) {
		UARTCharPutNonBlocking(config->reg, buf[n++]);
	}
	return n;
}

static int uart_cc13x4_cc26x4_fifo_read(const struct device *dev, uint8_t *buf, const int len)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	int n = 0;

	while (n < len && UARTCharsAvail(config->reg)) {
		buf[n++] = (uint8_t)UARTCharGetNonBlocking(config->reg);
	}
	return n;
}

static void uart_cc13x4_cc26x4_irq_tx_enable(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	struct uart_cc13x4_cc26x4_data *data = dev->data;

#ifdef CONFIG_PM_DEVICE
	uart_cc13x4_cc26x4_pm_lock_get(data, UART_CC13X4_CC26X4_PM_LOCK_TX);
#endif
	data->tx_irq_enabled = true;
	UARTIntEnable(config->reg, UART_INT_TX);

	if (UARTSpaceAvail(config->reg)) {
		NVIC_SetPendingIRQ(config->irq);
	}
}

static void uart_cc13x4_cc26x4_irq_tx_disable(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	struct uart_cc13x4_cc26x4_data *data = dev->data;

	data->tx_irq_enabled = false;
	UARTIntDisable(config->reg, UART_INT_TX);
#ifdef CONFIG_PM_DEVICE
	uart_cc13x4_cc26x4_pm_lock_put(data, UART_CC13X4_CC26X4_PM_LOCK_TX);
#endif
}

static int uart_cc13x4_cc26x4_irq_tx_ready(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	const struct uart_cc13x4_cc26x4_data *data = dev->data;

	return (data->tx_irq_enabled && UARTSpaceAvail(config->reg)) ? 1 : 0;
}

static void uart_cc13x4_cc26x4_irq_rx_enable(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

#ifdef CONFIG_PM_DEVICE
	uart_cc13x4_cc26x4_pm_lock_get(dev->data, UART_CC13X4_CC26X4_PM_LOCK_RX);
#endif
	UARTIntEnable(config->reg, UART_INT_RX | UART_INT_RT);
}

static void uart_cc13x4_cc26x4_irq_rx_disable(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

	UARTIntDisable(config->reg, UART_INT_RX | UART_INT_RT);
#ifdef CONFIG_PM_DEVICE
	uart_cc13x4_cc26x4_pm_lock_put(dev->data, UART_CC13X4_CC26X4_PM_LOCK_RX);
#endif
}

static int uart_cc13x4_cc26x4_irq_tx_complete(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

	return UARTBusy(config->reg) ? 0 : 1;
}

static int uart_cc13x4_cc26x4_irq_rx_ready(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

	return UARTCharsAvail(config->reg) ? 1 : 0;
}

static void uart_cc13x4_cc26x4_irq_err_enable(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

	UARTIntEnable(config->reg, UART_INT_OE | UART_INT_BE | UART_INT_PE | UART_INT_FE);
}

static void uart_cc13x4_cc26x4_irq_err_disable(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

	UARTIntDisable(config->reg, UART_INT_OE | UART_INT_BE | UART_INT_PE | UART_INT_FE);
}

static int uart_cc13x4_cc26x4_irq_is_pending(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	const struct uart_cc13x4_cc26x4_data *data = dev->data;

	if (UARTIntStatus(config->reg, true)) {
		return 1;
	}
	if (data->tx_irq_enabled && UARTSpaceAvail(config->reg)) {
		return 1;
	}
	return 0;
}

static void uart_cc13x4_cc26x4_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void uart_cc13x4_cc26x4_irq_callback_set(const struct device *dev,
						uart_irq_callback_user_data_t cb, void *user_data)
{
	struct uart_cc13x4_cc26x4_data *data = dev->data;

	data->callback = cb;
	data->user_data = user_data;
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

/* ------------------------------------------------------------------ */
/* Async / DMA API                                                      */
/* ------------------------------------------------------------------ */
#ifdef CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN

static int uart_cc13x4_cc26x4_async_callback_set(const struct device *dev, uart_callback_t callback,
						 void *user_data)
{
	struct uart_cc13x4_cc26x4_data *data = dev->data;

	data->async_callback = callback;
	data->async_user_data = user_data;

	return 0;
}

static int uart_cc13x4_cc26x4_async_tx(const struct device *dev, const uint8_t *buf, size_t len,
				       int32_t timeout)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	struct uart_cc13x4_cc26x4_data *data = dev->data;
	unsigned int key;
	int ret;

	struct dma_block_config block_cfg_tx = {
		.source_address = (uint32_t)buf,
		.dest_address = config->reg, /* UART DR at base+0 */
		.source_addr_adj = DMA_ADDR_ADJ_INCREMENT,
		.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE,
		.block_size = len,
	};

	struct dma_config dma_cfg_tx = {
		.dma_slot = 0, /* not used; CC13x4 channels are silicon-fixed */
		.channel_direction = MEMORY_TO_PERIPHERAL,
		.block_count = 1,
		.head_block = &block_cfg_tx,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = UART_CC13X4_BURST_LEN,
		.dest_burst_length = UART_CC13X4_BURST_LEN,
		.dma_callback = NULL,
		.user_data = NULL,
	};

	key = irq_lock();

	if (data->tx_len) {
		irq_unlock(key);
		return -EBUSY;
	}

	data->tx_buf = buf;
	data->tx_len = len;

	irq_unlock(key);

	ret = dma_config(config->dma_dev, config->dma_channel_tx, &dma_cfg_tx);
	if (ret) {
		return ret;
	}

	/*
	 * configure() calls UARTFIFODisable() for polling/interrupt mode.
	 * Re-enable the 32-deep FIFO here so the DMA burst (UART_CC13X4_BURST_LEN
	 * bytes per arbitration cycle) does not overflow the 1-deep holding
	 * register and silently drop bytes.  Set TX threshold to 4/8 full to
	 * match the burst length - the same values used by async_rx_enable().
	 */
	UARTFIFOEnable(config->reg);
	UARTFIFOLevelSet(config->reg, UART_FIFO_TX4_8, UART_FIFO_RX4_8);

	/* Disable trigger before arming so DMA does not start prematurely */
	UARTDMADisable(config->reg, UART_DMA_TX);

	if (timeout != SYS_FOREVER_US) {
		k_work_reschedule(&data->tx_timeout_work, K_USEC(timeout));
	}

	ret = dma_start(config->dma_dev, config->dma_channel_tx);
	if (ret) {
		return ret;
	}

#ifdef CONFIG_PM_DEVICE
	uart_cc13x4_cc26x4_pm_lock_get(data, UART_CC13X4_CC26X4_PM_LOCK_TX);
#endif

	/* Enable TX DMA trigger - transfer starts immediately */
	UARTDMAEnable(config->reg, UART_DMA_TX);

	return 0;
}

static int uart_cc13x4_cc26x4_tx_halt(struct uart_cc13x4_cc26x4_data *data)
{
	const struct uart_cc13x4_cc26x4_config *config = data->dev->config;
	struct dma_status status;
	struct uart_event evt;
	size_t total_len;
	unsigned int key;

	key = irq_lock();

	total_len = data->tx_len;

	evt.type = UART_TX_ABORTED;
	evt.data.tx.buf = data->tx_buf;
	evt.data.tx.len = 0;

	data->tx_buf = NULL;
	data->tx_len = 0;

	dma_stop(config->dma_dev, config->dma_channel_tx);

	/* Stop the UART from asserting further DMA requests - without this,
	 * TXDMAE stays set and the UART keeps requesting DMA into the now-
	 * disabled channel, causing spurious UART ISR firings after abort.
	 */
	UARTDMADisable(config->reg, UART_DMA_TX);

	irq_unlock(key);

	if (dma_get_status(config->dma_dev, config->dma_channel_tx, &status) == 0) {
		evt.data.tx.len = total_len - status.pending_length;
	}

	if (total_len) {
		if (data->async_callback) {
			data->async_callback(data->dev, &evt, data->async_user_data);
		}
#ifdef CONFIG_PM_DEVICE
		uart_cc13x4_cc26x4_pm_lock_put(data, UART_CC13X4_CC26X4_PM_LOCK_TX);
#endif
	} else {
		return -EINVAL;
	}

	return 0;
}

#if UART_CC13X4_ANY_INST_HAS_DMAS
static void uart_cc13x4_cc26x4_async_tx_timeout(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct uart_cc13x4_cc26x4_data *data =
		CONTAINER_OF(dwork, struct uart_cc13x4_cc26x4_data, tx_timeout_work);

	uart_cc13x4_cc26x4_tx_halt(data);
}
#endif /* UART_CC13X4_ANY_INST_HAS_DMAS */

static int uart_cc13x4_cc26x4_async_tx_abort(const struct device *dev)
{
	struct uart_cc13x4_cc26x4_data *data = dev->data;

	k_work_cancel_delayable(&data->tx_timeout_work);

	return uart_cc13x4_cc26x4_tx_halt(data);
}

static int uart_cc13x4_cc26x4_async_rx_enable(const struct device *dev, uint8_t *buf, size_t len,
					      int32_t timeout)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	struct uart_cc13x4_cc26x4_data *data = dev->data;
	struct uart_event evt;
	unsigned int key;
	int ret;

	struct dma_block_config block_cfg_rx = {
		.source_address = config->reg, /* UART DR at base+0 */
		.dest_address = (uint32_t)buf,
		.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE,
		.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT,
		.block_size = len,
	};

	struct dma_config dma_cfg_rx = {
		.dma_slot = 0,
		.channel_direction = PERIPHERAL_TO_MEMORY,
		.block_count = 1,
		.head_block = &block_cfg_rx,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = UART_CC13X4_BURST_LEN,
		.dest_burst_length = UART_CC13X4_BURST_LEN,
		.dma_callback = NULL,
		.user_data = NULL,
	};

	if (timeout != SYS_FOREVER_US) {
		return -ENOTSUP;
	}

	key = irq_lock();

	if (data->rx_len) {
		ret = -EBUSY;
		goto unlock;
	}

	/*
	 * Enable FIFO with RX trigger at 1/2 full (4 bytes) to match
	 * UART_CC13X4_BURST_LEN.  configure() calls UARTFIFODisable for
	 * polled/interrupt mode; re-enable here for DMA mode.
	 */
	UARTFIFOEnable(config->reg);
	UARTFIFOLevelSet(config->reg, UART_FIFO_TX4_8, UART_FIFO_RX4_8);

	ret = dma_config(config->dma_dev, config->dma_channel_rx, &dma_cfg_rx);
	if (ret) {
		goto unlock;
	}

	UARTDMADisable(config->reg, UART_DMA_RX);

	ret = dma_start(config->dma_dev, config->dma_channel_rx);
	if (ret) {
		goto unlock;
	}

#ifdef CONFIG_PM_DEVICE
	uart_cc13x4_cc26x4_pm_lock_get(data, UART_CC13X4_CC26X4_PM_LOCK_RX);
#endif

	UARTDMAEnable(config->reg, UART_DMA_RX);

	data->rx_buf = buf;
	data->rx_len = len;
	data->rx_processed_len = 0;

	/* Ask application for the next buffer (ping-pong) */
	if (data->async_callback) {
		evt.type = UART_RX_BUF_REQUEST;
		data->async_callback(dev, &evt, data->async_user_data);
	}

unlock:
	irq_unlock(key);
	return ret;
}

static int uart_cc13x4_cc26x4_async_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	struct uart_cc13x4_cc26x4_data *data = dev->data;
	unsigned int key;
	int ret = 0;

	key = irq_lock();

	if (data->rx_len == 0) {
		ret = -EACCES;
		goto unlock;
	}

	if (data->rx_next_len) {
		ret = -EBUSY;
		goto unlock;
	}

	data->rx_next_buf = buf;
	data->rx_next_len = len;

unlock:
	irq_unlock(key);
	return ret;
}

static void uart_cc13x4_cc26x4_notify_rx_processed(struct uart_cc13x4_cc26x4_data *data,
						   size_t processed)
{
	struct uart_event evt;

	if (!data->async_callback || data->rx_processed_len == processed) {
		return;
	}

	evt.type = UART_RX_RDY;
	evt.data.rx.buf = data->rx_buf;
	evt.data.rx.offset = data->rx_processed_len;
	evt.data.rx.len = processed - data->rx_processed_len;

	data->rx_processed_len = processed;

	data->async_callback(data->dev, &evt, data->async_user_data);
}

static int uart_cc13x4_cc26x4_async_rx_disable(const struct device *dev)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	struct uart_cc13x4_cc26x4_data *data = dev->data;
	struct dma_status status;
	struct uart_event evt;
	size_t rx_processed;
	unsigned int key;
	int ret = 0;

	key = irq_lock();

	if (data->rx_len == 0) {
		ret = -EINVAL;
		goto unlock;
	}

	dma_stop(config->dma_dev, config->dma_channel_rx);

#ifdef CONFIG_PM_DEVICE
	uart_cc13x4_cc26x4_pm_lock_put(data, UART_CC13X4_CC26X4_PM_LOCK_RX);
#endif

	if (dma_get_status(config->dma_dev, config->dma_channel_rx, &status) == 0 &&
	    status.pending_length) {
		rx_processed = data->rx_len - status.pending_length;
		uart_cc13x4_cc26x4_notify_rx_processed(data, rx_processed);
	}

	if (data->async_callback) {
		evt.type = UART_RX_BUF_RELEASED;
		evt.data.rx_buf.buf = data->rx_buf;
		data->async_callback(dev, &evt, data->async_user_data);
	}

	data->rx_buf = NULL;
	data->rx_len = 0;

	if (data->rx_next_len) {
		if (data->async_callback) {
			evt.type = UART_RX_BUF_RELEASED;
			evt.data.rx_buf.buf = data->rx_next_buf;
			data->async_callback(dev, &evt, data->async_user_data);
		}
		data->rx_next_buf = NULL;
		data->rx_next_len = 0;
	}

	if (data->async_callback) {
		evt.type = UART_RX_DISABLED;
		data->async_callback(dev, &evt, data->async_user_data);
	}

unlock:
	irq_unlock(key);
	return ret;
}

#endif /* CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN */

/* ------------------------------------------------------------------ */
/* ISR                                                                  */
/* ------------------------------------------------------------------ */
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN)

static void uart_cc13x4_cc26x4_isr(const struct device *dev)
{
	struct uart_cc13x4_cc26x4_data *data = dev->data;

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	if (data->callback) {
		data->callback(dev, data->user_data);
	}
#endif

#ifdef CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	struct uart_event evt;
	unsigned int key;

	/*
	 * TRM 16.3.10: DMA completion fires on the peripheral's own IRQ.
	 * The CC13x4 UART ICSR has no DMA-done bits, so UARTIntStatus()
	 * returns 0.  Read REQDONE from the uDMA controller instead - it
	 * is set sticky by hardware and must be cleared here to prevent
	 * the NVIC from immediately re-entering this ISR.
	 */
	uint32_t reqdone = sys_read32(UDMA0_BASE + UDMA_O_REQDONE);

	if (reqdone & BIT(config->dma_channel_tx)) {
		k_work_cancel_delayable(&data->tx_timeout_work);

		/*
		 * Correct sequence to clear REQDONE and keep it clear:
		 *
		 * 1. UARTDMADisable - clears UART DMACR.TXDMAE so the UART
		 *    stops asserting its DMA TX request signal.  Without this
		 *    step the UART re-raises the request the instant the TX
		 *    FIFO drops below the trigger level, which re-sets
		 *    REQDONE before the write below reaches the register -
		 *    making it appear the bit can never be cleared.
		 *
		 * 2. sys_write32 to UDMA0_BASE + UDMA_O_REQDONE - W1C, clears the sticky
		 *    bit.  Safe to stay clear now because the request line
		 *    is held low by step 1.
		 */
		UARTDMADisable(config->reg, UART_DMA_TX);
		sys_write32(BIT(config->dma_channel_tx), UDMA0_BASE + UDMA_O_REQDONE);

		key = irq_lock();

		if (data->tx_len && data->async_callback) {
			evt.type = UART_TX_DONE;
			evt.data.tx.buf = data->tx_buf;
			evt.data.tx.len = data->tx_len;
			data->async_callback(dev, &evt, data->async_user_data);
		}

		data->tx_buf = NULL;
		data->tx_len = 0;

#ifdef CONFIG_PM_DEVICE
		uart_cc13x4_cc26x4_pm_lock_put(data, UART_CC13X4_CC26X4_PM_LOCK_TX);
#endif

		irq_unlock(key);
	}

	if (reqdone & BIT(config->dma_channel_rx)) {
		/* Clear REQDONE first so the interrupt line deasserts */
		sys_write32(BIT(config->dma_channel_rx), UDMA0_BASE + UDMA_O_REQDONE);

		key = irq_lock();

		uart_cc13x4_cc26x4_notify_rx_processed(data, data->rx_len);

		if (data->async_callback) {
			evt.type = UART_RX_BUF_RELEASED;
			evt.data.rx_buf.buf = data->rx_buf;
			data->async_callback(dev, &evt, data->async_user_data);
		}

		if (data->rx_next_len == 0) {
			data->rx_buf = NULL;
			data->rx_len = 0;

			if (data->async_callback) {
				evt.type = UART_RX_DISABLED;
				data->async_callback(dev, &evt, data->async_user_data);
			}

#ifdef CONFIG_PM_DEVICE
			uart_cc13x4_cc26x4_pm_lock_put(data, UART_CC13X4_CC26X4_PM_LOCK_RX);
#endif
		} else {
			/* Ping-pong: swap to next buffer and re-arm DMA */
			data->rx_buf = data->rx_next_buf;
			data->rx_len = data->rx_next_len;
			data->rx_next_buf = NULL;
			data->rx_next_len = 0;
			data->rx_processed_len = 0;

			dma_reload(config->dma_dev, config->dma_channel_rx, config->reg,
				   (uint32_t)data->rx_buf, data->rx_len);

			dma_start(config->dma_dev, config->dma_channel_rx);

			if (data->async_callback) {
				evt.type = UART_RX_BUF_REQUEST;
				data->async_callback(dev, &evt, data->async_user_data);
			}
		}

		irq_unlock(key);
	}
#endif /* CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN */
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN || CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN */

/* ------------------------------------------------------------------ */
/* Driver API table                                                     */
/* ------------------------------------------------------------------ */
static DEVICE_API(uart, uart_cc13x4_cc26x4_driver_api) = {
	.poll_in = uart_cc13x4_cc26x4_poll_in,
	.poll_out = uart_cc13x4_cc26x4_poll_out,
	.err_check = uart_cc13x4_cc26x4_err_check,
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.configure = uart_cc13x4_cc26x4_configure,
	.config_get = uart_cc13x4_cc26x4_config_get,
#endif
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_cc13x4_cc26x4_fifo_fill,
	.fifo_read = uart_cc13x4_cc26x4_fifo_read,
	.irq_tx_enable = uart_cc13x4_cc26x4_irq_tx_enable,
	.irq_tx_disable = uart_cc13x4_cc26x4_irq_tx_disable,
	.irq_tx_ready = uart_cc13x4_cc26x4_irq_tx_ready,
	.irq_rx_enable = uart_cc13x4_cc26x4_irq_rx_enable,
	.irq_rx_disable = uart_cc13x4_cc26x4_irq_rx_disable,
	.irq_tx_complete = uart_cc13x4_cc26x4_irq_tx_complete,
	.irq_rx_ready = uart_cc13x4_cc26x4_irq_rx_ready,
	.irq_err_enable = uart_cc13x4_cc26x4_irq_err_enable,
	.irq_err_disable = uart_cc13x4_cc26x4_irq_err_disable,
	.irq_is_pending = uart_cc13x4_cc26x4_irq_is_pending,
	.irq_update = uart_cc13x4_cc26x4_irq_update,
	.irq_callback_set = uart_cc13x4_cc26x4_irq_callback_set,
#endif
#ifdef CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN
	.callback_set = uart_cc13x4_cc26x4_async_callback_set,
	.tx = uart_cc13x4_cc26x4_async_tx,
	.tx_abort = uart_cc13x4_cc26x4_async_tx_abort,
	.rx_enable = uart_cc13x4_cc26x4_async_rx_enable,
	.rx_buf_rsp = uart_cc13x4_cc26x4_async_rx_buf_rsp,
	.rx_disable = uart_cc13x4_cc26x4_async_rx_disable,
#endif
};

/* ------------------------------------------------------------------ */
/* PM action                                                            */
/* ------------------------------------------------------------------ */
#ifdef CONFIG_PM_DEVICE
static int uart_cc13x4_cc26x4_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		UARTDisable(config->reg);
		uart_cc13x4_cc26x4_clock_disable(config);
		return 0;
	case PM_DEVICE_ACTION_RESUME:
		uart_cc13x4_cc26x4_clock_enable(config);
		return uart_cc13x4_cc26x4_configure(
			dev, &((struct uart_cc13x4_cc26x4_data *)dev->data)->uart_config);
	default:
		return -ENOTSUP;
	}
}
#endif

/* ------------------------------------------------------------------ */
/* Init macros                                                          */
/* ------------------------------------------------------------------ */
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN)
#define UART_CC13X4_CC26X4_IRQ_CFG(n)                                                              \
	do {                                                                                       \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), uart_cc13x4_cc26x4_isr,     \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	} while (false)
#define UART_CC13X4_CC26X4_IRQ_INIT(n) .irq = DT_INST_IRQN(n),
#else
#define UART_CC13X4_CC26X4_IRQ_CFG(n)
#define UART_CC13X4_CC26X4_IRQ_INIT(n)
#endif

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN)
#define UART_CC13X4_CC26X4_SHARED_INT_FIELDS .callback = NULL, .user_data = NULL,
#else
#define UART_CC13X4_CC26X4_SHARED_INT_FIELDS
#endif

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#define UART_CC13X4_CC26X4_IRQ_DATA .tx_irq_enabled = false,
#else
#define UART_CC13X4_CC26X4_IRQ_DATA
#endif

#ifdef CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN
/*
 * Only populate DMA fields for instances that actually have a 'dmas'
 * property in the devicetree (e.g. uart0 in the overlay).  uart1 and
 * any other instance without 'dmas' get empty config so the compiler
 * does not try to read undefined DT cells.
 */
#define UART_CC13X4_CC26X4_DMA_CONFIG(n)                                                           \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, dmas), (                                             \
		.dma_dev        = UART_CC13X4_DT_INST_DMA_CTLR(n, rx),                            \
		.dma_channel_tx = UART_CC13X4_DT_INST_DMA_CHANNEL(n, tx),                         \
		.dma_channel_rx = UART_CC13X4_DT_INST_DMA_CHANNEL(n, rx),                         \
	), ())
#define UART_CC13X4_CC26X4_DMA_DATA .async_callback = NULL, .async_user_data = NULL,
#else
#define UART_CC13X4_CC26X4_DMA_CONFIG(n)
#define UART_CC13X4_CC26X4_DMA_DATA
#endif

static int uart_cc13x4_cc26x4_init_common(const struct device *dev)
{
	struct uart_cc13x4_cc26x4_data *data = dev->data;
	const struct uart_cc13x4_cc26x4_config *config = dev->config;
	int ret;

	uart_cc13x4_cc26x4_clock_enable(config);

	ret = pinctrl_apply_state(data->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret) {
		return ret;
	}

	ret = uart_cc13x4_cc26x4_configure(dev, &data->uart_config);
	if (ret) {
		return ret;
	}

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN)
	UARTIntClear(config->reg, UART_INT_RX | UART_INT_RT);
#endif

	return 0;
}

#if defined(CONFIG_UART_CC13X4_CC26X4_DMA_DRIVEN) && UART_CC13X4_ANY_INST_HAS_DMAS
static int uart_cc13x4_cc26x4_init_dma(const struct device *dev)
{
	struct uart_cc13x4_cc26x4_data *data = dev->data;
	const struct uart_cc13x4_cc26x4_config *config = dev->config;

	if (!device_is_ready(config->dma_dev)) {
		return -ENODEV;
	}

	k_work_init_delayable(&data->tx_timeout_work, uart_cc13x4_cc26x4_async_tx_timeout);
	data->dev = dev;

	return 0;
}

#define UART_CC13X4_CC26X4_INIT_DMA(n)                                                             \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, dmas), (ret = uart_cc13x4_cc26x4_init_dma(dev);), ())
#else
#define UART_CC13X4_CC26X4_INIT_DMA(n)
#endif

#define UART_CC13X4_CC26X4_INIT(n)                                                                 \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	PM_DEVICE_DT_INST_DEFINE(n, uart_cc13x4_cc26x4_pm_action);                                 \
                                                                                                   \
	static int uart_cc13x4_cc26x4_init_##n(const struct device *dev)                           \
	{                                                                                          \
		int ret = uart_cc13x4_cc26x4_init_common(dev);                                     \
                                                                                                   \
		if (ret) {                                                                         \
			return ret;                                                                \
		}                                                                                  \
                                                                                                   \
		UART_CC13X4_CC26X4_IRQ_CFG(n);                                                     \
		UART_CC13X4_CC26X4_INIT_DMA(n)                                                     \
                                                                                                   \
		return ret;                                                                        \
	}                                                                                          \
                                                                                                   \
	static const struct uart_cc13x4_cc26x4_config uart_cc13x4_cc26x4_config_##n = {            \
		.reg = DT_INST_REG_ADDR(n),                                                        \
		.sys_clk_freq = DT_INST_PROP_BY_PHANDLE(n, clocks, clock_frequency),               \
		.prcm_domain = (DT_INST_REG_ADDR(n) == 0x40001000) ? PRCM_DOMAIN_SERIAL            \
								   : PRCM_DOMAIN_PERIPH,           \
		.prcm_periph = (DT_INST_REG_ADDR(n) == 0x40001000) ? PRCM_PERIPH_UART0             \
								   : PRCM_PERIPH_UART1,            \
		UART_CC13X4_CC26X4_IRQ_INIT(n) UART_CC13X4_CC26X4_DMA_CONFIG(n)};                  \
                                                                                                   \
	static struct uart_cc13x4_cc26x4_data uart_cc13x4_cc26x4_data_##n = {                      \
		.uart_config =                                                                     \
			{                                                                          \
				.baudrate = DT_INST_PROP(n, current_speed),                        \
				.parity = UART_CFG_PARITY_NONE,                                    \
				.stop_bits = UART_CFG_STOP_BITS_1,                                 \
				.data_bits = UART_CFG_DATA_BITS_8,                                 \
				.flow_ctrl = DT_INST_PROP(n, hw_flow_control)                      \
						     ? UART_CFG_FLOW_CTRL_RTS_CTS                  \
						     : UART_CFG_FLOW_CTRL_NONE,                    \
			},                                                                         \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		UART_CC13X4_CC26X4_SHARED_INT_FIELDS UART_CC13X4_CC26X4_IRQ_DATA                   \
			UART_CC13X4_CC26X4_DMA_DATA};                                              \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, uart_cc13x4_cc26x4_init_##n, PM_DEVICE_DT_INST_GET(n),            \
			      &uart_cc13x4_cc26x4_data_##n, &uart_cc13x4_cc26x4_config_##n,        \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY,                           \
			      &uart_cc13x4_cc26x4_driver_api)

DT_INST_FOREACH_STATUS_OKAY(UART_CC13X4_CC26X4_INIT)
