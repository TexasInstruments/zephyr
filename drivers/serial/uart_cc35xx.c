/*
 * Copyright (c) 2025 Conclusive Engineering Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_cc35xx_uart

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/dt-bindings/pinctrl/ti-cc35xx-pinctrl.h>

#ifdef CONFIG_UART_ASYNC_API
#include "zephyr/arch/common/sys_io.h"
#include "zephyr/devicetree.h"
#include "zephyr/devicetree/dma.h"
#include <zephyr/drivers/dma.h>
#include <zephyr/sys/util.h>
#endif /* CONFIG_UART_ASYNC_API */

#include <inc/hw_memmap.h>
#include <inc/hw_ints.h>
#include <inc/hw_uartlin.h>
#ifdef CONFIG_UART_ASYNC_API
#include <inc/hw_host_dma.h>
#endif
#include <driverlib/uart.h>

LOG_MODULE_REGISTER(uart_cc35xx);

#define UART_CC35XX_IRQ_ENABLED                                                                    \
	UTIL_OR(IS_ENABLED(CONFIG_UART_INTERRUPT_DRIVEN), IS_ENABLED(CONFIG_UART_ASYNC_API))

#ifdef CONFIG_UART_ASYNC_API
/*
 * Helper macro: computes the absolute register address for a given UART
 * register offset, using the base address stored in the device config.
 */
#define UART_CC35XX_REG_GET(data, reg)                                                             \
	((((struct uart_cc35xx_dev_config *)(data->dev->config))->base) + reg)

/* RX interrupts handled by the async API */
#define UART_CC35XX_RX_INT (UART_INT_RXDMADONE | UART_INT_OE | UART_INT_RT)

/* Size of HAL internal buffers for DMA operations */
#define CC35XX_UART_RX_BUF 32
#define CC35XX_UART_TX_BUF 32

/*
 * Per-direction DMA stream descriptor.
 * Holds the DMA device reference, channel number, DMA configuration,
 * block configuration, and optional software timeout machinery.
 */
struct uart_cc35xx_dma_stream {
	const struct device *dev_dma;         /* DMA controller device */
	uint32_t dma_channel;                 /* DMA channel number */
	struct dma_config dma_cfg;            /* DMA transfer configuration */
	struct dma_block_config blk_cfg;      /* Single-block DMA descriptor */
	int32_t timeout;                      /* Transfer timeout in microseconds; */
	struct k_work_delayable timeout_work; /* Delayed work item used to fire the timeout */
};
#endif /* CONFIG_UART_ASYNC_API */

/*
 * Static (compile-time) device configuration, placed in read-only memory.
 * One instance is created per UART node in the devicetree.
 */
struct uart_cc35xx_dev_config {
	unsigned long base;    /* UART peripheral base address */
	uint32_t sys_clk_freq; /* Source clock frequency in Hz (from DT clocks node) */
	const struct pinctrl_dev_config *pcfg; /* Pin-control configuration (TX/RX/CTS/RTS mux) */
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
	uart_irq_config_func_t irq_config_func; /* Board-level IRQ connect + enable function */
#endif
};

/*
 * Mutable per-device runtime state.
 * One instance per UART node in the devicetree.
 */
struct uart_cc35xx_dev_data {
	const struct device *dev; /* Back-pointer to the Zephyr device struct */
	uint32_t baud_rate;       /* Current baud rate */
	bool flow_ctrl;           /* true = RTS/CTS hardware flow control enabled */
	int id;                   /* Instance index (from DT) */
	struct k_spinlock lock;   /* Spinlock protecting shared state */
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t callback; /* Upper-layer IRQ callback */
	void *user_data;                        /* Opaque pointer passed to callback */
#endif                                          /* CONFIG_UART_INTERRUPT_DRIVEN */
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	struct uart_config uart_config; /* Cached copy of the current UART configuration */
#endif                                  /* CONFIG_UART_USE_RUNTIME_CONFIGURE */
#ifdef CONFIG_UART_ASYNC_API
	struct uart_cc35xx_dma_stream dma_rx; /* RX DMA stream state */
	struct uart_cc35xx_dma_stream dma_tx; /* TX DMA stream state */
	uint8_t fifo_state;                   /* Reserved for future FIFO level tracking */

	uart_callback_t async_callback; /* Upper-layer async event callback */
	void *async_user_data;          /* Opaque pointer passed to async_callback */

	/* RX buffer management */
	uint8_t *rx_buf;          /* Active RX buffer provided by the user */
	size_t rx_buflen;         /* Length of rx_buf in bytes */
	size_t rx_dma_block_size; /* Number of bytes of the current DMA block, 0 if stopped */
	size_t rx_buf_filled;     /* Bytes already written into rx_buf*/
	size_t rx_processed;      /* Bytes already reported to the user via UART_RX_RDY */
	int64_t rx_last_activity; /* Last observed RX progress, in microseconds */
	uint8_t *rx_next_buf;     /* Next RX buffer queued by the user (via rx_buf_rsp) */
	size_t rx_next_buflen;    /* Length of rx_next_buf in bytes */

	/* TX buffer management */
	uint8_t tx_scratch_buf[CONFIG_UART_CC35XX_DMA_SCRATCH_BUFFER_SIZE];
	const uint8_t *tx_buf; /* Active TX buffer provided by the user */
	size_t tx_buflen;      /* Length of tx_buf in bytes */
#endif                         /* CONFIG_UART_ASYNC_API */
};

#ifdef CONFIG_UART_ASYNC_API

static int uart_cc35xx_async_tx(const struct device *dev, const uint8_t *tx_data, size_t buf_size,
				int32_t timeout);
static int uart_cc35xx_async_tx_abort(const struct device *dev);
static int uart_cc35xx_async_rx_disable_dma(struct uart_cc35xx_dev_data *data);
static int uart_cc35xx_async_rx_disable(const struct device *dev);
static int uart_cc35xx_async_rx_enable_dma(struct uart_cc35xx_dev_data *data);
static int uart_cc35xx_async_tx_disable_dma(struct uart_cc35xx_dev_data *data);
#endif /* CONFIG_UART_ASYNC_API */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static int uart_cc35xx_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size)
{
	const struct uart_cc35xx_dev_config *config = dev->config;
	int i = 0;

	for (i = 0; i < size && UARTSpaceAvailable(config->base); i++) {
		UARTPutCharNonBlocking(config->base, tx_data[i]);
	}

	return i;
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
static int uart_cc35xx_fifo_read(const struct device *dev, uint8_t *rx_data, int size)
{
	const struct uart_cc35xx_dev_config *config = dev->config;
	int i = 0;

	for (i = 0; i < size && UARTCharAvailable(config->base); i++) {
		rx_data[i] = UARTGetCharNonBlocking(config->base);
	}

	return i;
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN || CONFIG_UART_ASYNC_API */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

/*
 * uart_cc35xx_irq_tx_enable - Enable TX interrupts.
 *
 */
static void uart_cc35xx_irq_tx_enable(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;
	struct uart_cc35xx_dev_data *data = dev->data;
	uint32_t flags = UART_INT_TX | UART_INT_EOT;

	if (data->flow_ctrl) {
		flags |= UART_INT_CTS;
	}

	UARTEnableInt(config->base, flags);
}

/*
 * uart_cc35xx_irq_tx_disable - Disable TX interrupts.
 */
static void uart_cc35xx_irq_tx_disable(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;
	struct uart_cc35xx_dev_data *data = dev->data;
	uint32_t flags = UART_INT_TX | UART_INT_EOT;

	if (data->flow_ctrl) {
		flags |= UART_INT_CTS;
	}

	UARTDisableInt(config->base, flags);
}

/*
 * uart_cc35xx_irq_tx_ready - Return non-zero if the TX interrupt is asserted.
 *
 * Reads the masked interrupt status and checks for the TX / EOT / CTS bits.
 */
static int uart_cc35xx_irq_tx_ready(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;
	struct uart_cc35xx_dev_data *data = dev->data;
	uint32_t status;
	uint32_t flags = UART_INT_TX | UART_INT_EOT;

	if (data->flow_ctrl) {
		flags |= UART_INT_CTS;
	}

	/* Read masked (post-enable) interrupt status */
	status = UARTIntStatus(config->base, true);

	return !!(status & flags);
}

/* uart_cc35xx_irq_rx_enable - Enable RX FIFO and receive-timeout interrupts. */
static void uart_cc35xx_irq_rx_enable(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;

	UARTEnableInt(config->base, UART_INT_RX | UART_INT_RT);
}

/* uart_cc35xx_irq_rx_disable - Disable RX FIFO and receive-timeout interrupts. */
static void uart_cc35xx_irq_rx_disable(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;

	UARTDisableInt(config->base, UART_INT_RX | UART_INT_RT);
}

/*
 * uart_cc35xx_irq_tx_complete - Return non-zero while the TX shift register
 * is still busy (i.e. last byte has not yet been transmitted on the wire).
 */
static int uart_cc35xx_irq_tx_complete(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;

	return UARTBusy(config->base);
}

/* uart_cc35xx_irq_rx_ready - Return 1 if at least one byte is in the RX FIFO. */
static int uart_cc35xx_irq_rx_ready(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;

	return UARTCharAvailable(config->base) ? 1 : 0;
}

/* uart_cc35xx_irq_is_pending - Return 1 if any masked interrupt is pending. */
static int uart_cc35xx_irq_is_pending(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;
	uint32_t status;

	status = UARTIntStatus(config->base, true);

	return status ? 1 : 0;
}

/* uart_cc35xx_irq_err_enable - Enable all UART error interrupts (OE/BE/PE/FE). */
static void uart_cc35xx_irq_err_enable(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;

	UARTEnableInt(config->base, UART_INT_OE | UART_INT_BE | UART_INT_PE | UART_INT_FE);
}

/* uart_cc35xx_irq_err_disable - Disable all UART error interrupts. */
static void uart_cc35xx_irq_err_disable(const struct device *dev)
{
	const struct uart_cc35xx_dev_config *config = dev->config;

	UARTDisableInt(config->base, UART_INT_OE | UART_INT_BE | UART_INT_PE | UART_INT_FE);
}

static void uart_cc35xx_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
}

/*
 * uart_cc35xx_irq_callback_set - Register the upper-layer IRQ callback.
 *
 * The callback is invoked from the ISR every time a UART interrupt fires.
 */
static void uart_cc35xx_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb,
					 void *cb_data)
{
	struct uart_cc35xx_dev_data *data = dev->data;

	data->callback = cb;
	data->user_data = cb_data;
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#ifdef CONFIG_UART_ASYNC_API

/*
 * uart_cc35xx_notify_rx_processed - Fire a UART_RX_RDY event for new data.
 *
 * Reports the bytes written into the buffer ('data->rx_buf_filled') that
 * were not yet reported to the user ('data->rx_processed'). Buffer
 * ownership does not change. Call with interrupts locked.
 */
static void uart_cc35xx_notify_rx_processed(struct uart_cc35xx_dev_data *data)
{
	struct uart_event evt;

	/* Nothing to report if there is no new data */
	if (data->rx_processed == data->rx_buf_filled) {
		return;
	}

	evt.type = UART_RX_RDY;
	evt.data.rx.buf = data->rx_buf;
	evt.data.rx.offset = data->rx_processed;
	evt.data.rx.len = data->rx_buf_filled - data->rx_processed;
	data->rx_processed = data->rx_buf_filled;

	if (data->async_callback) {
		data->async_callback(data->dev, &evt, data->async_user_data);
	}
}

/*
 * uart_cc35xx_rx_collect - Update the fill position from the DMA progress.
 *
 * With 'stop', DMA is stopped first and the HW FIFO is drained, which also
 * collects bytes below the UART DMA FIFO threshold. 'complete' means that
 * RXDMADONE confirmed the whole DMA block.
 */
static int uart_cc35xx_rx_collect(struct uart_cc35xx_dev_data *data, bool stop, bool complete)
{
	size_t block_size = data->rx_dma_block_size;
	size_t filled = data->rx_buf_filled;
	struct dma_status dma_stat;
	int ret;

	if (stop) {
		ret = uart_cc35xx_async_rx_disable_dma(data);
		if (ret != 0) {
			return ret;
		}
	}

	if (block_size != 0 && complete) {
		filled = data->rx_buflen;
	} else if (block_size != 0) {
		ret = dma_get_status(data->dma_rx.dev_dma, data->dma_rx.dma_channel, &dma_stat);
		if (ret != 0) {
			return ret;
		}
		if (dma_stat.pending_length > block_size ||
		    data->rx_buflen - dma_stat.pending_length < data->rx_buf_filled) {
			return -EIO;
		}
		filled = data->rx_buflen - dma_stat.pending_length;
	}

	/* Drain any bytes still sitting in the HW FIFO */
	if (stop) {
		filled += uart_cc35xx_fifo_read(data->dev, data->rx_buf + filled,
						data->rx_buflen - filled);
	}

	if (filled != data->rx_buf_filled) {
		data->rx_buf_filled = filled;
		data->rx_last_activity = k_ticks_to_us_floor64(k_uptime_ticks());
	}

	return 0;
}

/*
 * uart_cc35xx_rx_schedule - Schedule the next RX progress check.
 *
 * The receive timeout interrupt requires a nonempty FIFO, so the DMA
 * progress is also sampled to detect the end of reception.
 */
static void uart_cc35xx_rx_schedule(struct uart_cc35xx_dev_data *data)
{
	int32_t interval = 1000;

	if (data->rx_buflen == 0 || data->dma_rx.timeout == SYS_FOREVER_US) {
		return;
	}

	if (data->dma_rx.timeout > 0) {
		interval = MIN(MAX(data->dma_rx.timeout / 4, 1), 1000);
	}

	k_work_reschedule(&data->dma_rx.timeout_work, K_USEC(interval));
}

/*
 * uart_cc35xx_rx_advance - Switch from a full RX buffer to the next one.
 *
 * Only a full buffer is released; a timeout keeps the current one.
 * Without a next buffer, reception stops.
 */
static void uart_cc35xx_rx_advance(struct uart_cc35xx_dev_data *data)
{
	uint8_t *current = data->rx_buf;
	struct uart_event evt;

	uart_cc35xx_notify_rx_processed(data);
	if (data->rx_buf != current) {
		return;
	}

	if (data->rx_next_buflen == 0) {
		uart_cc35xx_async_rx_disable(data->dev);
		return;
	}

	evt.type = UART_RX_BUF_RELEASED;
	evt.data.rx_buf.buf = data->rx_buf;

	/* Swap in the next buffer and restart DMA */
	data->rx_buf = data->rx_next_buf;
	data->rx_buflen = data->rx_next_buflen;

	data->rx_next_buf = NULL;
	data->rx_next_buflen = 0;
	data->rx_buf_filled = 0;
	data->rx_processed = 0;
	current = data->rx_buf;

	/* The previous buffer is done – release it to the user */
	if (data->async_callback) {
		data->async_callback(data->dev, &evt, data->async_user_data);
	}
	if (data->rx_buf != current) {
		return;
	}

	/* Ask the upper layer for another buffer in advance */
	if (data->async_callback) {
		evt.type = UART_RX_BUF_REQUEST;

		data->async_callback(data->dev, &evt, data->async_user_data);
	}

	if (data->rx_buf == current && uart_cc35xx_async_rx_enable_dma(data) != 0) {
		uart_cc35xx_async_rx_disable(data->dev);
	}
}

/*
 * uart_cc35xx_dma_tx_callback - Called from the ISR when transmission completes.
 *
 * Stops the DMA channel, fires a UART_TX_DONE event, and clears the
 * TX buffer state so that a new transmission can be started.
 */
static void uart_cc35xx_dma_tx_callback(struct uart_cc35xx_dev_data *data, int status)
{
	unsigned int key;
	struct uart_event evt;

	key = irq_lock();
	uart_cc35xx_async_tx_disable_dma(data);

	if (data->tx_buflen) {
		if (data->async_callback) {
			evt.type = UART_TX_DONE;
			evt.data.tx.buf = data->tx_buf;
			evt.data.tx.len = data->tx_buflen;

			/*
			 * Clear TX state before calling the callback to allow
			 * the user to queue the next transfer from within it.
			 */
			data->tx_buflen = 0;
			data->tx_buf = NULL;

			data->async_callback(data->dev, &evt, data->async_user_data);
		} else {
			data->tx_buflen = 0;
			data->tx_buf = NULL;
		}
	}

	irq_unlock(key);
}

/*
 * uart_cc35xx_dma_rx_callback - Called from the ISR on RX DMA done / timeout / overrun.
 *
 * Stops DMA, collects the bytes it transferred and drains the HW FIFO. A
 * full buffer is replaced by the next one; otherwise DMA continues in the
 * current buffer.
 */
static void uart_cc35xx_dma_rx_callback(struct uart_cc35xx_dev_data *data, int status)
{
	uint8_t *current;
	unsigned int key;

	key = irq_lock();
	current = data->rx_buf;

	if (data->rx_buflen == 0) {
		/* No active buffer */
		goto out;
	}

	if (uart_cc35xx_rx_collect(data, true, (status & UART_INT_RXDMADONE) != 0) != 0) {
		uart_cc35xx_async_rx_disable(data->dev);
		goto out;
	}

	if (data->rx_buf_filled == data->rx_buflen) {
		uart_cc35xx_rx_advance(data);
		goto out;
	}

	if (data->dma_rx.timeout == 0) {
		uart_cc35xx_notify_rx_processed(data);
	}

	/* Re-arm the DMA for the continued transfer */
	if (data->rx_buf == current && uart_cc35xx_async_rx_enable_dma(data) != 0) {
		uart_cc35xx_async_rx_disable(data->dev);
	}
out:
	irq_unlock(key);
}
#endif /* CONFIG_UART_ASYNC_API */

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
/*
 * uart_cc35xx_isr - Top-level UART interrupt service routine.
 *
 * When the interrupt-driven API is in use (data->callback set), the ISR
 * delegates entirely to the upper-layer callback so that it can call
 * fifo_fill / fifo_read as needed.
 *
 * When the async API is in use, the ISR:
 *  - Logs and clears any framing/parity/break errors.
 *  - Dispatches to the RX DMA callback on RXDMADONE / OE / RT.
 *  - Dispatches to the TX completion callback on EOT.
 */
static void uart_cc35xx_isr(const struct device *dev)
{
	struct uart_cc35xx_dev_data *data = dev->data;
#ifdef CONFIG_UART_ASYNC_API
	const struct uart_cc35xx_dev_config *config = dev->config;
	uint32_t errStatus, event;
	/* Read masked interrupt status once to avoid race between reads */
	uint32_t status = UARTIntStatus(config->base, true);
#endif /* CONFIG_UART_ASYNC_API */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	/* Interrupt-driven mode: hand off to the registered callback */
	if (data->callback) {
		data->callback(dev, data->user_data);
		return;
	}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#ifdef CONFIG_UART_ASYNC_API
	/* Handle line / framing errors */
	if ((status & (UART_INT_BE | UART_INT_PE | UART_INT_FE))) {
		errStatus = UARTGetRxError(config->base);
		event = __builtin_clz(errStatus & UARTLIN_RSRECR_OE_M);
		LOG_ERR("Error Status 0x%08x event 0x%08x", errStatus, event);
		if (errStatus) {
			UARTClearRxError(config->base);
		}
	}

	/* RX DMA done, overrun, or receive-timeout: process received data */
	if (status & (UART_INT_RXDMADONE | UART_INT_OE | UART_INT_RT)) {
		uart_cc35xx_dma_rx_callback(data, status);
	}

	/* Wait for the FIFO and shift register to drain after DMA completes. */
	if (status & UART_INT_TXDMADONE) {
		UARTDisableDma(config->base, UART_DMA_TX);
		UARTDisableInt(config->base, UART_INT_TXDMADONE);
		UARTClearInt(config->base, UART_INT_TXDMADONE | UART_INT_EOT);
		if (UARTBusy(config->base)) {
			UARTEnableInt(config->base, UART_INT_EOT);
		} else {
			uart_cc35xx_dma_tx_callback(data, status);
		}
	}

	if (status & UART_INT_EOT) {
		uart_cc35xx_dma_tx_callback(data, status);
	}
#endif /* CONFIG_UART_ASYNC_API */
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

/*
 * uart_cc35xx_poll_in - Non-blocking RX byte read.
 *
 * Returns 0 and stores the received byte in *c on success.
 * Returns -1 if the RX FIFO is empty.
 */
static int uart_cc35xx_poll_in(const struct device *dev, unsigned char *c)
{
	const struct uart_cc35xx_dev_config *config = dev->config;

	if (!UARTCharAvailable(config->base)) {
		return -1;
	}

	*c = UARTGetCharNonBlocking(config->base);

	return 0;
}

/*
 * uart_cc35xx_poll_out - Blocking TX byte write.
 *
 * Blocks until space is available in the TX FIFO, then writes the byte.
 * When CONFIG_PM_DEVICE is enabled, also waits for the shift register to
 * drain so the CPU cannot enter standby mid-transmission.
 */
static void uart_cc35xx_poll_out(const struct device *dev, unsigned char c)
{
	const struct uart_cc35xx_dev_config *config = dev->config;

	UARTPutChar(config->base, c);

#ifdef CONFIG_PM_DEVICE
	/* Wait for character to be transmitted to ensure CPU
	 * does not enter standby when UART is busy
	 */
	while (UARTBusy(config->base)) {
	}
#endif /* CONFIG_PM_DEVICE */
}

#ifdef CONFIG_UART_ASYNC_API

/*
 * uart_cc35xx_async_tx_disable_dma - Stop the TX DMA channel.
 *
 * Cancels any pending TX timeout, disables the UART DMA TX trigger,
 * clears and masks the TXDMADONE interrupt, and stops the DMA channel.
 */
static int uart_cc35xx_async_tx_disable_dma(struct uart_cc35xx_dev_data *data)
{
	const struct uart_cc35xx_dev_config *config = data->dev->config;

	k_work_cancel_delayable(&data->dma_tx.timeout_work);

	UARTDisableDma(config->base, UART_DMA_TX);
	UARTDisableInt(config->base, UART_INT_TXDMADONE | UART_INT_EOT);
	UARTClearInt(config->base, UART_INT_TXDMADONE | UART_INT_EOT);

	dma_stop(data->dma_tx.dev_dma, data->dma_tx.dma_channel);

	return 0;
}

/*
 * uart_cc35xx_tx_halt - Abort an in-progress TX transfer.
 *
 * Stops DMA, queries how many bytes were actually sent, fires a
 * UART_TX_ABORTED event (with len = 0 to indicate an abort rather
 * than a partial transfer), and resets TX state.
 *
 * Returns -EINVAL if no transfer is currently active.
 */
static int uart_cc35xx_tx_halt(struct uart_cc35xx_dev_data *data)
{
	struct dma_status dma_stat;
	struct uart_event evt;
	unsigned int key;
	int ret;

	if (data->tx_buflen == 0) {
		return -EINVAL;
	}

	key = irq_lock();
	uart_cc35xx_async_tx_disable_dma(data);

	/* Query how many bytes the DMA engine had not yet consumed */
	ret = dma_get_status(data->dma_tx.dev_dma, data->dma_tx.dma_channel, &dma_stat);
	if (ret == 0) {
		evt.data.tx.len = data->tx_buflen - dma_stat.pending_length;
	}

	/* Report the abort; len is intentionally set to 0 (incomplete) */
	evt.type = UART_TX_ABORTED;
	evt.data.tx.buf = data->tx_buf;
	evt.data.tx.len = 0;

	data->tx_buf = NULL;
	data->tx_buflen = 0;
	irq_unlock(key);

	if (data->async_callback) {
		data->async_callback(data->dev, &evt, data->async_user_data);
	}

	return 0;
}

/*
 * uart_cc35xx_async_tx_abort - Public API: abort the current TX transfer.
 *
 * Delegates to uart_cc35xx_tx_halt().
 */
static int uart_cc35xx_async_tx_abort(const struct device *dev)
{
	struct uart_cc35xx_dev_data *data = dev->data;

	return uart_cc35xx_tx_halt(data);
}

/*
 * uart_cc35xx_async_tx - Start an async (DMA) TX transfer.
 *
 * Configures the TX DMA block to transfer 'buf_size' bytes from 'tx_data'
 * to the UART data register, then starts the DMA channel and enables the
 * TXDMADONE interrupt.  An optional software timeout can be set to abort
 * the transfer if it takes too long.
 *
 * Returns -EBUSY if a transfer is already in progress.
 */
static int uart_cc35xx_async_tx(const struct device *dev, const uint8_t *tx_data, size_t buf_size,
				int32_t timeout)
{
	struct uart_cc35xx_dev_data *data = dev->data;
	const struct uart_cc35xx_dev_config *config = dev->config;
	uintptr_t addr = POINTER_TO_UINT(tx_data);
	uintptr_t sram_start = CONFIG_SRAM_BASE_ADDRESS;
	uintptr_t sram_end = sram_start + CONFIG_SRAM_SIZE * 1024UL;
	unsigned int key;
	int ret;

	if (tx_data == NULL || buf_size == 0 || buf_size > HOST_DMA_CH0TCTL_TRANSB_M) {
		return -EINVAL;
	}

	key = irq_lock();
	if (data->tx_buflen != 0) {
		irq_unlock(key);
		return -EBUSY;
	}

	/* Host DMA cannot read external flash; stage such data in internal SRAM. */
	if (addr < sram_start || addr >= sram_end || buf_size > sram_end - addr) {
		if (buf_size > sizeof(data->tx_scratch_buf)) {
			irq_unlock(key);
			return -ENOTSUP;
		}
		memcpy(data->tx_scratch_buf, tx_data, buf_size);
		addr = POINTER_TO_UINT(data->tx_scratch_buf);
	}

	data->dma_tx.blk_cfg.dest_address = UART_CC35XX_REG_GET(data, UARTLIN_O_DR);
	data->dma_tx.blk_cfg.source_address = addr;
	data->dma_tx.blk_cfg.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	data->dma_tx.blk_cfg.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	data->dma_tx.blk_cfg.block_size = buf_size;
	data->tx_buf = tx_data;
	data->tx_buflen = buf_size;

	UARTDisableDma(config->base, UART_DMA_TX);
	ret = dma_config(data->dma_tx.dev_dma, data->dma_tx.dma_channel, &data->dma_tx.dma_cfg);
	if (ret == 0) {
		UARTClearInt(config->base, UART_INT_TXDMADONE | UART_INT_EOT);
		UARTEnableInt(config->base, UART_INT_TXDMADONE);
		ret = dma_start(data->dma_tx.dev_dma, data->dma_tx.dma_channel);
	}
	if (ret != 0) {
		uart_cc35xx_async_tx_disable_dma(data);
		data->tx_buf = NULL;
		data->tx_buflen = 0;
	} else {
		UARTEnableDma(config->base, UART_DMA_TX);
		if (timeout != SYS_FOREVER_US) {
			data->dma_tx.timeout = timeout;
			k_work_reschedule(&data->dma_tx.timeout_work, K_USEC(timeout));
		}
	}
	irq_unlock(key);
	return ret;
}

/*
 * uart_cc35xx_async_rx_buf_rsp - Provide the next RX buffer to the driver.
 *
 * Called by the upper layer in response to a UART_RX_BUF_REQUEST event.
 * The buffer will be used once the current one is full or released.
 *
 * Returns -EACCES if RX is not active, -EBUSY if a next buffer is already
 * queued.
 */
static int uart_cc35xx_async_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	struct uart_cc35xx_dev_data *data = dev->data;

	unsigned int key;
	int ret = 0;

	key = irq_lock();

	if (data->rx_buflen == 0) {
		/* RX is not currently active */
		ret = -EACCES;
		goto unlock;
	}

	if (data->rx_next_buflen != 0) {
		/* A next buffer is already pending */
		ret = -EBUSY;
		goto unlock;
	}

	data->rx_next_buf = buf;
	data->rx_next_buflen = len;

unlock:
	irq_unlock(key);

	return ret;
}

/*
 * uart_cc35xx_async_rx_enable_dma - (Re)start the RX DMA channel.
 *
 * Configures the DMA block to transfer data from the UART DR register
 * into the current RX buffer (starting at the fill offset), then enables
 * the DMA trigger and the RX interrupts.
 *
 * Also schedules the software RX timeout check if one is configured.
 */
static int uart_cc35xx_async_rx_enable_dma(struct uart_cc35xx_dev_data *data)
{
	const struct uart_cc35xx_dev_config *config = data->dev->config;
	int ret;

	/* Source: UART data register (fixed address) */
	data->dma_rx.blk_cfg.source_address = UART_CC35XX_REG_GET(data, UARTLIN_O_DR);

	/* Destination: current fill position in the RX buffer */
	data->dma_rx.blk_cfg.dest_address = (uint32_t)data->rx_buf + data->rx_buf_filled,
	data->dma_rx.blk_cfg.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	data->dma_rx.blk_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	/* Only request the remaining space in the buffer */
	data->dma_rx.blk_cfg.block_size = data->rx_buflen - data->rx_buf_filled;
	data->rx_dma_block_size = data->rx_buflen - data->rx_buf_filled;

	ret = dma_config(data->dma_rx.dev_dma, data->dma_rx.dma_channel, &data->dma_rx.dma_cfg);
	if (ret) {
		data->rx_dma_block_size = 0;
		return ret;
	}

	/* Disable DMA trigger before starting the channel to avoid a race */
	UARTDisableDma(config->base, UART_DMA_RX);

	/* Clear stale interrupts before the new block can complete */
	UARTClearInt(config->base, UART_CC35XX_RX_INT);
	UARTEnableInt(config->base, UART_CC35XX_RX_INT);

	/* Start DMA channel */
	ret = dma_start(data->dma_rx.dev_dma, data->dma_rx.dma_channel);
	if (ret) {
		data->rx_dma_block_size = 0;
		UARTDisableInt(config->base, UART_CC35XX_RX_INT);
		return ret;
	}

	/* Arm UART DMA RX trigger */
	UARTEnableDma(config->base, UART_DMA_RX);

	/* Restart the software inactivity timeout, if configured */
	uart_cc35xx_rx_schedule(data);

	return 0;
}

/*
 * uart_cc35xx_async_rx_enable - Start async (DMA) reception.
 *
 * Sets up the initial RX buffer, arms the DMA, and requests a second
 * buffer from the upper layer so that the driver can switch buffers
 * without losing data.
 *
 * Returns -EBUSY if reception is already active.
 */
static int uart_cc35xx_async_rx_enable(const struct device *dev, uint8_t *rx_buf, size_t buf_size,
				       int32_t timeout)
{
	struct uart_cc35xx_dev_data *data = dev->data;

	struct uart_event evt;
	unsigned int key;
	int ret = 0;

	key = irq_lock();

	if (data->rx_buflen) {
		ret = -EBUSY;
		goto unlock;
	}

	/* Initialise RX state for the new buffer */
	data->rx_buf = rx_buf;
	data->rx_buflen = buf_size;
	data->rx_dma_block_size = buf_size;
	data->rx_buf_filled = 0;
	data->rx_processed = 0;
	data->dma_rx.timeout = timeout;

	ret = uart_cc35xx_async_rx_enable_dma(data);
	if (ret != 0) {
		data->rx_buf = NULL;
		data->rx_buflen = 0;
		goto unlock;
	}

	/* Request next buffer so we have one ready when this one fills up */
	if (data->async_callback) {
		evt.type = UART_RX_BUF_REQUEST;

		data->async_callback(dev, &evt, data->async_user_data);
	}

unlock:
	irq_unlock(key);

	return ret;
}

/*
 * uart_cc35xx_async_rx_disable_dma - Stop the RX DMA channel.
 *
 * Cancels any pending RX timeout, stops the DMA channel, disables
 * the UART DMA RX trigger, and masks / clears the RX interrupts.
 */
static int uart_cc35xx_async_rx_disable_dma(struct uart_cc35xx_dev_data *data)
{
	const struct uart_cc35xx_dev_config *config = data->dev->config;
	int ret = 0;

	k_work_cancel_delayable(&data->dma_rx.timeout_work);

	UARTDisableDma(config->base, UART_DMA_RX);
	if (data->rx_dma_block_size != 0) {
		ret = dma_stop(data->dma_rx.dev_dma, data->dma_rx.dma_channel);
		data->rx_dma_block_size = 0;
	}
	UARTDisableInt(config->base, UART_CC35XX_RX_INT);
	UARTClearInt(config->base, UART_CC35XX_RX_INT);

	return ret;
}

/*
 * uart_cc35xx_async_rx_disable - Stop async (DMA) reception.
 *
 * Stops DMA, reports any partially received data, releases the current
 * and (if present) the next buffer to the upper layer, then fires
 * UART_RX_DISABLED.
 *
 * Returns -EINVAL if RX was not active.
 */
static int uart_cc35xx_async_rx_disable(const struct device *dev)
{
	struct uart_cc35xx_dev_data *data = dev->data;
	const struct uart_cc35xx_dev_config *config = dev->config;
	struct uart_event evt;
	unsigned int key;
	int ret = 0;

	key = irq_lock();

	if (data->rx_buflen == 0) {
		ret = -EINVAL;
		goto unlock;
	}

	/* Collect data received so far, including the HW FIFO */
	ret = uart_cc35xx_rx_collect(data, true, false);

	/* Report any data that was received but not yet notified */
	uart_cc35xx_notify_rx_processed(data);

	/* Release the current RX buffer */
	if (data->async_callback) {
		evt.type = UART_RX_BUF_RELEASED;
		evt.data.rx_buf.buf = data->rx_buf;

		data->async_callback(dev, &evt, data->async_user_data);
	}

	data->rx_buf = NULL;
	data->rx_buflen = 0;
	data->rx_dma_block_size = 0;

	/* Release the queued next buffer (if any), after draining the FIFO into it */
	if (data->rx_next_buflen) {
		data->rx_buf = data->rx_next_buf;
		data->rx_buflen = data->rx_next_buflen;
		data->rx_buf_filled = 0;
		data->rx_processed = 0;

		data->rx_next_buf = NULL;
		data->rx_next_buflen = 0;

		if (UARTCharAvailable(config->base)) {
			uart_cc35xx_rx_collect(data, true, false);
		}
		uart_cc35xx_notify_rx_processed(data);

		if (data->async_callback) {
			evt.type = UART_RX_BUF_RELEASED;
			evt.data.rx_buf.buf = data->rx_buf;

			data->async_callback(dev, &evt, data->async_user_data);
		}

		data->rx_buf = NULL;
		data->rx_buflen = 0;
	}

	/* Signal that reception has fully stopped */
	if (data->async_callback) {
		evt.type = UART_RX_DISABLED;

		data->async_callback(dev, &evt, data->async_user_data);
	}

unlock:
	irq_unlock(key);

	return ret;
}

/*
 * uart_cc35xx_async_tx_timeout - Delayed-work handler for TX timeout.
 *
 * Fired when the TX transfer does not complete within the timeout period
 * specified in uart_cc35xx_async_tx().  Delegates to uart_cc35xx_tx_halt()
 * which stops DMA and fires UART_TX_ABORTED.
 */
static void uart_cc35xx_async_tx_timeout(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct uart_cc35xx_dma_stream *tx_stream =
		CONTAINER_OF(dwork, struct uart_cc35xx_dma_stream, timeout_work);
	struct uart_cc35xx_dev_data *data =
		CONTAINER_OF(tx_stream, struct uart_cc35xx_dev_data, dma_tx);

	uart_cc35xx_tx_halt(data);
}

/*
 * uart_cc35xx_async_rx_timeout - Delayed-work handler for RX inactivity timeout.
 *
 * Samples the DMA progress. When no new data arrives within the timeout
 * period, reports all data received so far via UART_RX_RDY and keeps
 * receiving into the same buffer. A full buffer is replaced by the next one.
 */
static void uart_cc35xx_async_rx_timeout(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct uart_cc35xx_dma_stream *rx_stream =
		CONTAINER_OF(dwork, struct uart_cc35xx_dma_stream, timeout_work);
	struct uart_cc35xx_dev_data *data =
		CONTAINER_OF(rx_stream, struct uart_cc35xx_dev_data, dma_rx);
	uint8_t *current;
	unsigned int key;
	int64_t now;
	bool full;

	key = irq_lock();
	current = data->rx_buf;

	if (data->rx_buflen == 0 || data->dma_rx.timeout == SYS_FOREVER_US) {
		goto out;
	}

	if (uart_cc35xx_rx_collect(data, false, false) != 0) {
		uart_cc35xx_async_rx_disable(data->dev);
		goto out;
	}

	now = k_ticks_to_us_floor64(k_uptime_ticks());
	full = data->rx_buf_filled == data->rx_buflen;
	if (!full && (data->rx_buf_filled == data->rx_processed ||
		      now - data->rx_last_activity < data->dma_rx.timeout)) {
		uart_cc35xx_rx_schedule(data);
		goto out;
	}

	/* Include the HW FIFO before declaring inactivity or a full buffer */
	if (uart_cc35xx_rx_collect(data, true, full) != 0) {
		uart_cc35xx_async_rx_disable(data->dev);
		goto out;
	}

	if (data->rx_buf_filled == data->rx_buflen) {
		uart_cc35xx_rx_advance(data);
		goto out;
	}

	/* New data in the HW FIFO restarts the inactivity period */
	if (now - data->rx_last_activity >= data->dma_rx.timeout) {
		uart_cc35xx_notify_rx_processed(data);
	}

	if (data->rx_buf == current && uart_cc35xx_async_rx_enable_dma(data) != 0) {
		uart_cc35xx_async_rx_disable(data->dev);
	}
out:
	irq_unlock(key);
}

/*
 * uart_cc35xx_async_callback_set - Register the upper-layer async callback.
 *
 * When CONFIG_UART_EXCLUSIVE_API_CALLBACKS is enabled, registering an
 * async callback clears the interrupt-driven callback (and vice-versa)
 * so that both APIs cannot be active simultaneously.
 */
static int uart_cc35xx_async_callback_set(const struct device *dev, uart_callback_t callback,
					  void *user_data)
{
	struct uart_cc35xx_dev_data *data = dev->data;

	data->async_callback = callback;
	data->async_user_data = user_data;

#if defined(CONFIG_UART_EXCLUSIVE_API_CALLBACKS)
	/* Prevent concurrent use of interrupt-driven and async APIs */
	data->callback = NULL;
	data->user_data = NULL;
#endif /* CONFIG_UART_EXCLUSIVE_API_CALLBACKS */

	return 0;
}

#endif /* CONFIG_UART_ASYNC_API */

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE

/*
 * uart_cc35xx_configure - Apply a new UART configuration at runtime.
 *
 * Translates the Zephyr uart_config struct into the TI driverlib bit-field
 * values, then reconfigures the UART peripheral.  The UART is briefly
 * disabled while the baud-rate divisors and format registers are updated.
 *
 * Returns -ENOTSUP for unsupported settings, -EINVAL for invalid values.
 */
static int uart_cc35xx_configure(const struct device *dev, const struct uart_config *cfg)
{
	const struct uart_cc35xx_dev_config *config = dev->config;
	struct uart_cc35xx_dev_data *data = dev->data;
	uint32_t conf = 0;

	/* Map Zephyr data-bits enum to driverlib word-length constant */
	switch (cfg->data_bits) {
	case UART_CFG_DATA_BITS_5:
		conf |= UART_CONFIG_WLEN_5;
		break;
	case UART_CFG_DATA_BITS_6:
		conf |= UART_CONFIG_WLEN_6;
		break;
	case UART_CFG_DATA_BITS_7:
		conf |= UART_CONFIG_WLEN_7;
		break;
	case UART_CFG_DATA_BITS_8:
		conf |= UART_CONFIG_WLEN_8;
		break;
	default:
		return -ENOTSUP;
	}

	/* Map Zephyr parity enum to driverlib parity constant */
	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		conf |= UART_CONFIG_PAR_NONE;
		break;
	case UART_CFG_PARITY_EVEN:
		conf |= UART_CONFIG_PAR_EVEN;
		break;
	case UART_CFG_PARITY_ODD:
		conf |= UART_CONFIG_PAR_ODD;
		break;
	default:
		return -ENOTSUP;
	}

	/* Map Zephyr stop-bits enum to driverlib stop-bits constant */
	switch (cfg->stop_bits) {
	case UART_CFG_STOP_BITS_1:
		conf |= UART_CONFIG_STOP_ONE;
		break;
	case UART_CFG_STOP_BITS_2:
		conf |= UART_CONFIG_STOP_TWO;
		break;
	default:
		return -ENOTSUP;
	}

	/* Map Zephyr flow-control enum; DTR/DSR is not supported by this HW */
	switch (cfg->flow_ctrl) {
	case UART_CFG_FLOW_CTRL_NONE:
		data->flow_ctrl = false;
		break;
	case UART_CFG_FLOW_CTRL_RTS_CTS:
		data->flow_ctrl = true;
		break;
	case UART_CFG_FLOW_CTRL_DTR_DSR:
		return -ENOTSUP;
	default:
		return -EINVAL;
	}

	/* Disables UART before setting control registers */
	UARTDisable(config->base);
	UARTConfigSetExpClk(config->base, config->sys_clk_freq, cfg->baudrate, conf);

	/* Apply hardware flow-control pins */
	if (data->flow_ctrl) {
		UARTEnableCts(config->base);
		UARTEnableRts(config->base);
	} else {
		UARTDisableCts(config->base);
		UARTDisableRts(config->base);
	}
	/* Re-enable UART */
	UARTEnable(config->base);

	/* Make use of the FIFO to reduce chances of data being lost */
	UARTEnableFifo(config->base);

	/* Cache the new configuration for config_get() */
	data->uart_config = *cfg;
	return 0;
}

/*
 * uart_cc35xx_config_get - Return the current UART configuration.
 *
 * Copies the cached uart_config struct (updated by uart_cc35xx_configure)
 * into the caller-supplied struct.
 */
static int uart_cc35xx_config_get(const struct device *dev, struct uart_config *cfg)
{
	struct uart_cc35xx_dev_data *data = dev->data;

	*cfg = data->uart_config;
	return 0;
}
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

/*
 * uart_cc35xx_init - Initialise the UART peripheral.
 */
static int uart_cc35xx_init(const struct device *dev)
{
	struct uart_cc35xx_dev_data *data = dev->data;
	const struct uart_cc35xx_dev_config *config = dev->config;
	int ret;

	/* Apply pin-control (TX/RX/CTS/RTS mux) */
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}
	data->dev = dev;

	/* Clock + default 8N1 configuration */
	UARTClockCtrl(config->base, true);
	UARTDisable(config->base);
	UARTConfigSetExpClk(config->base, config->sys_clk_freq, data->baud_rate,
			    UART_CONFIG_WLEN_8 | UART_CONFIG_PAR_NONE | UART_CONFIG_STOP_ONE);
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	data->uart_config = (struct uart_config){
		.baudrate = data->baud_rate,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
#endif
	/* Start with flow control and all interrupts disabled */
	UARTDisableCts(config->base);
	UARTDisableRts(config->base);
	UARTDisableInt(config->base, UART_INT_ALL);

	/* Enable the UART */
	UARTEnable(config->base);

#ifdef CONFIG_UART_ASYNC_API
	data->fifo_state = 0;

	/* Validate and initialise RX DMA */
	if (data->dma_rx.dev_dma != NULL) {
		if (!device_is_ready(data->dma_rx.dev_dma)) {
			LOG_ERR("RX DMA channel not ready");
			return -ENODEV;
		}
		k_work_init_delayable(&data->dma_rx.timeout_work, uart_cc35xx_async_rx_timeout);
		/* Link the single block descriptor into the DMA config chain */
		data->dma_rx.dma_cfg.head_block = &data->dma_rx.blk_cfg;
		data->dma_rx.dma_cfg.user_data = (void *)data;
	}

	/* Validate and initialise TX DMA */
	if (data->dma_tx.dev_dma != NULL) {
		if (!device_is_ready(data->dma_tx.dev_dma)) {
			LOG_ERR("TX DMA channel not ready");
			return -ENODEV;
		}
		k_work_init_delayable(&data->dma_tx.timeout_work, uart_cc35xx_async_tx_timeout);
		data->dma_tx.dma_cfg.head_block = &data->dma_tx.blk_cfg;
		data->dma_tx.dma_cfg.user_data = (void *)data;
	}

	UARTDisableFifo(config->base);
	/*
	 * Set FIFO thresholds: TX fires at 4/8 full, RX fires at 6/8 full. This parameters and
	 * values are required by the DMA controller.
	 * This reduces interrupt frequency while keeping latency acceptable.
	 */
	UARTSetFifoLevel(config->base, UART_FIFO_TX4_8, UART_FIFO_RX6_8);
	UARTEnableFifo(config->base);
#endif /* CONFIG_UART_ASYNC_API */

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
	/* Connect and enable the UART interrupt at the NVIC level */
	config->irq_config_func(dev);
#endif
	return 0;
}

/*
 * uart_cc35xx_driver_api - Zephyr UART driver API filled with the
 * function pointers implemented above.  Sections are conditionally
 * compiled to match the enabled Kconfig options.
 */
static DEVICE_API(uart, uart_cc35xx_driver_api) = {
	.poll_in = uart_cc35xx_poll_in,
	.poll_out = uart_cc35xx_poll_out,

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.configure = uart_cc35xx_configure,
	.config_get = uart_cc35xx_config_get,
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_cc35xx_fifo_fill,
	.fifo_read = uart_cc35xx_fifo_read,
	.irq_tx_enable = uart_cc35xx_irq_tx_enable,
	.irq_tx_disable = uart_cc35xx_irq_tx_disable,
	.irq_tx_ready = uart_cc35xx_irq_tx_ready,
	.irq_rx_enable = uart_cc35xx_irq_rx_enable,
	.irq_rx_disable = uart_cc35xx_irq_rx_disable,
	.irq_tx_complete = uart_cc35xx_irq_tx_complete,
	.irq_rx_ready = uart_cc35xx_irq_rx_ready,
	.irq_is_pending = uart_cc35xx_irq_is_pending,
	.irq_err_enable = uart_cc35xx_irq_err_enable,
	.irq_err_disable = uart_cc35xx_irq_err_disable,
	.irq_update = uart_cc35xx_irq_update,
	.irq_callback_set = uart_cc35xx_irq_callback_set,

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
#ifdef CONFIG_UART_ASYNC_API
	.callback_set = uart_cc35xx_async_callback_set,
	.tx = uart_cc35xx_async_tx,
	.tx_abort = uart_cc35xx_async_tx_abort,
	.rx_enable = uart_cc35xx_async_rx_enable,
	.rx_disable = uart_cc35xx_async_rx_disable,
	.rx_buf_rsp = uart_cc35xx_async_rx_buf_rsp,
#endif /* CONFIG_UART_ASYNC_API */
};

#ifdef CONFIG_UART_ASYNC_API

#define UART_CC35XX_DMA_CHANNEL_INIT(index, dir, ch_dir, src_burst, dst_burst)                     \
	.dev_dma = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(index, dir)),                           \
	.dma_channel = DT_INST_DMAS_CELL_BY_NAME(index, dir, channel),                             \
	.dma_cfg = {                                                                               \
		.dma_slot = DT_INST_DMAS_CELL_BY_NAME(index, dir, channel_config),                 \
		.channel_direction = ch_dir,                                                       \
		.source_data_size = 1,                                                             \
		.dest_data_size = 1,                                                               \
		.source_burst_length = src_burst,                                                  \
		.dest_burst_length = dst_burst,                                                    \
		.block_count = 1,                                                                  \
	},

#define UART_CC35XX_DMA_CHANNEL(index, dir, ch_dir, src_burst, dst_burst)                          \
	.dma_##dir = {COND_CODE_1(                                                                 \
		DT_INST_DMAS_HAS_NAME(index, dir),                                                 \
		(UART_CC35XX_DMA_CHANNEL_INIT(index, dir, ch_dir, src_burst, dst_burst)), (NULL))},
#else
#define UART_CC35XX_DMA_CHANNEL(index, dir, ch_dir, src_burst, dst_burst)
#endif /* CONFIG_UART_ASYNC_API */

/* clang-format off */
#define UART_CC35XX_DEVICE(index) \
	PINCTRL_DT_INST_DEFINE(index); \
	IF_ENABLED(UART_CC35XX_IRQ_ENABLED, ( \
	static void uart_cc35xx_cfg_func_##index(const struct device *dev) \
	{ \
		ARG_UNUSED(dev); \
		IRQ_CONNECT(DT_INST_IRQN(index), DT_INST_IRQ(index, priority), \
			uart_cc35xx_isr, DEVICE_DT_INST_GET(index), 0); \
		irq_enable(DT_INST_IRQN(index)); \
	})); \
	static const struct uart_cc35xx_dev_config uart_cc35xx_dev_cfg_##index = { \
		.base = DT_INST_REG_ADDR(index), \
		.sys_clk_freq = DT_INST_PROP_BY_PHANDLE(index, clocks, clock_frequency), \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(index), \
		IF_ENABLED(UART_CC35XX_IRQ_ENABLED, \
		    (.irq_config_func = uart_cc35xx_cfg_func_##index,)) }; \
	static struct uart_cc35xx_dev_data uart_cc35xx_dev_data_##index = { \
		.baud_rate = DT_INST_PROP(index, current_speed), \
		.id = index, \
		UART_CC35XX_DMA_CHANNEL(index, tx, MEMORY_TO_PERIPHERAL, 1, 1) \
			UART_CC35XX_DMA_CHANNEL(index, rx, PERIPHERAL_TO_MEMORY, 1, 1)}; \
	DEVICE_DT_INST_DEFINE(index, uart_cc35xx_init, NULL, &uart_cc35xx_dev_data_##index, \
			      &uart_cc35xx_dev_cfg_##index, PRE_KERNEL_1, \
			      CONFIG_SERIAL_INIT_PRIORITY, (void *)&uart_cc35xx_driver_api);
/* clang-format on */

DT_INST_FOREACH_STATUS_OKAY(UART_CC35XX_DEVICE);
