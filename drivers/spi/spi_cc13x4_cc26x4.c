/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_cc13x4_cc26x4_spi

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_cc13x4_cc26x4);

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/pinctrl/cc13x4_cc26x4-pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/util.h>

#include <driverlib/prcm.h>
#include <driverlib/spi.h>

#include <inc/hw_memmap.h>
#include <inc/hw_spi.h>

/* driverlib GPIO is used for the CPOL=1 SCK pre-stage fix */
#include <driverlib/gpio.h>

#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
#include <zephyr/drivers/dma.h>
#include <zephyr/sys/sys_io.h>

#include <driverlib/udma.h>
#include <inc/hw_udma.h>
#endif /* CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN */

#include "spi_context.h"

#define SPI_CC13X4_CC26X4_DFS 1 /* only 8-bit frames are supported */

#define SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(op)                                                      \
	(IS_ENABLED(CONFIG_SPI_SLAVE) && (SPI_OP_MODE_GET(op) == SPI_OP_MODE_SLAVE))

#define SPI_CC13X4_26X4_DUMMY_DATA 0x00

#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
/* Interrupts used to follow a DMA transfer */
#define SPI_CC13X4_CC26X4_INT_MASK (SPI_DMA_DONE_RX | SPI_DMA_DONE_TX | SPI_RXFIFO_OVF)

#define SPI_CC13X4_CC26X4_DMA_RX_DONE BIT(0)
#define SPI_CC13X4_CC26X4_DMA_TX_DONE BIT(1)

/*
 * One uDMA basic transfer moves at most UDMA_XFER_SIZE_MAX items. Longer
 * buffers are split in chunks, re-armed from the SPI ISR.
 */
#define SPI_CC13X4_CC26X4_MAX_DMA_XFER UDMA_XFER_SIZE_MAX

/*
 * Arbitration size: number of bytes transferred per uDMA bus arbitration
 * cycle (maps to R_POWER in the uDMA channel control word via Zephyr DMA
 * source_burst_length / dest_burst_length). ARB_4 batches 4 bytes per burst
 * request, reducing re-arbitration overhead from once per byte (ARB_1) to
 * once per 4 bytes. The FIFO thresholds below are matched to this value so
 * every BREQ triggers a full 4-byte burst with no partial transfers.
 */
#define SPI_CC13X4_CC26X4_DMA_BURST_LEN 4
#endif /* CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN */

struct spi_cc13x4_cc26x4_config {
	uint32_t base;
	uint32_t prcm_periph;
	uint32_t prcm_domain;
	const struct pinctrl_dev_config *pcfg;
#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
	void (*irq_config_func)(void);
	/* NULL when the instance has no "dmas" property: polled transfers are used */
	const struct device *dma_dev;
	uint8_t dma_channel_tx;
	uint8_t dma_channel_rx;
#endif
};

struct spi_cc13x4_cc26x4_data {
	struct spi_context ctx;
#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
	struct dma_block_config block_cfg_tx;
	struct dma_config dma_cfg_tx;
	struct dma_block_config block_cfg_rx;
	struct dma_config dma_cfg_rx;
	volatile uint32_t dma_tx_xfer_len;
	volatile uint32_t dma_rx_xfer_len;
	volatile uint8_t dma_status_flags;
#endif
#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_SPI_SLAVE)
	bool slave_pm_lock_held;
#endif
};

#define CPU_FREQ DT_PROP(DT_PATH(cpus, cpu_0), clock_frequency)

#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
/* TX source when there is no TX buffer, RX sink when there is no RX buffer */
static uint8_t dummy_tx_data = SPI_CC13X4_26X4_DUMMY_DATA;
static uint8_t dummy_rx_data;
#endif

/**
 * @brief Find the FSS (chip-select) pin entry from the pinctrl default state.
 *
 * Walks the pinctrl default state looking for any pin whose iofunc matches one
 * of the SPIx_FSS port-function values. Returns a pointer to the entry, or
 * NULL if not found (e.g. when GPIO-emulated CS is used instead of HW CS).
 */
static const pinctrl_soc_pin_t *spi_cc13x4_cc26x4_find_fss_pin(const struct device *dev)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	const struct pinctrl_state *state = NULL;

	for (uint8_t s = 0; s < cfg->pcfg->state_cnt; s++) {
		if (cfg->pcfg->states[s].id == PINCTRL_STATE_DEFAULT) {
			state = &cfg->pcfg->states[s];
			break;
		}
	}

	if (state == NULL) {
		return NULL;
	}

	for (uint8_t p = 0; p < state->pin_cnt; p++) {
		uint32_t fn = state->pins[p].iofunc;

		if (fn == IOC_PORT_MCU_SPI0_FSS || fn == IOC_PORT_MCU_SPI1_FSS ||
		    fn == IOC_PORT_MCU_SPI2_FSS || fn == IOC_PORT_MCU_SPI3_FSS) {
			return &state->pins[p];
		}
	}

	return NULL;
}

static int spi_cc13x4_cc26x4_configure(const struct device *dev, const struct spi_config *config)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	uint32_t prot;
	uint32_t spi_op_mode;
	uint32_t frequency;
	int ret;

	if (spi_context_configured(ctx, config)) {
		return 0;
	}

	if (config->operation & SPI_HALF_DUPLEX) {
		LOG_ERR("Half-duplex not supported");
		return -ENOTSUP;
	}

	if (SPI_WORD_SIZE_GET(config->operation) != 8) {
		LOG_ERR("Word sizes other than 8 bits are not supported");
		return -ENOTSUP;
	}

	if (IS_ENABLED(CONFIG_SPI_EXTENDED_MODES) &&
	    (config->operation & SPI_LINES_MASK) != SPI_LINES_SINGLE) {
		LOG_ERR("Multiple lines are not supported");
		return -EINVAL;
	}

	if ((config->operation & SPI_CS_ACTIVE_HIGH) && !spi_cs_is_gpio(config)) {
		LOG_ERR("Active high CS requires GPIO emulation");
		return -EINVAL;
	}

	if (SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(config->operation)) {
		spi_op_mode = SPI_MODE_PERIPHERAL;
		/*
		 * In peripheral mode the SPI clock is provided by the controller;
		 * the frequency field is ignored by the hardware. Pass the CPU
		 * frequency so SPIConfigSetExpClk does not produce a divide-by-zero.
		 *
		 * The CC13x4/CC26x4 SPI peripheral requires the incoming SCK to be
		 * at most CPU_FREQ/12 so the synchroniser chain can reliably latch
		 * each bit. The driverlib ASSERT that enforces this is compiled out
		 * when DRIVERLIB_DEBUG is not defined (which is always the case in
		 * a Zephyr build), so the check must be done here explicitly.
		 * Exceeding this limit causes silent data corruption (wrong bytes
		 * clocked out / received) rather than a hardware fault.
		 */
		if (config->frequency > CPU_FREQ / 12) {
			LOG_ERR("Peripheral SCK %u Hz exceeds max %u Hz (CPU_FREQ/12 = %u)",
				(unsigned int)config->frequency, (unsigned int)(CPU_FREQ / 12),
				(unsigned int)(CPU_FREQ / 12));
			return -ENOTSUP;
		}
		frequency = CPU_FREQ;
	} else {
		spi_op_mode = SPI_MODE_CONTROLLER;
		frequency = config->frequency;

		if (frequency < 200000) {
			LOG_ERR("Frequencies lower than 200 kHz are not supported");
			return -EINVAL;
		}

		if (2 * frequency > CPU_FREQ) {
			LOG_ERR("Frequency greater than supported in controller mode");
			return -EINVAL;
		}
	}

	/*
	 * Use 4-wire (hardware CS) modes for peripheral so the SPI IP samples
	 * the hardware CS pin.  In controller mode use 3-wire (GPIO-emulated CS)
	 * unless the application does not provide a gpio CS, in which case the
	 * hardware CS line is used and 4-wire modes apply.
	 */
	if (SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(config->operation) || !spi_cs_is_gpio(config)) {
		if (SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) {
			prot = (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA)
				       ? SPI_FRF_MOTO_MODE_7
				       : SPI_FRF_MOTO_MODE_6;
		} else {
			prot = (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA)
				       ? SPI_FRF_MOTO_MODE_5
				       : SPI_FRF_MOTO_MODE_4;
		}
	} else {
		if (SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) {
			prot = (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA)
				       ? SPI_FRF_MOTO_MODE_3
				       : SPI_FRF_MOTO_MODE_2;
		} else {
			prot = (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA)
				       ? SPI_FRF_MOTO_MODE_1
				       : SPI_FRF_MOTO_MODE_0;
		}
	}

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Applying SPI pinctrl state failed: %d", ret);
		return ret;
	}

	ctx->config = config;

	SPIDisable(cfg->base);

	SPIConfigSetExpClk(cfg->base, CPU_FREQ, prot, spi_op_mode, frequency, 8);

	if (SPI_MODE_GET(config->operation) & SPI_MODE_LOOP) {
		HWREG(cfg->base + SPI_O_CTL1) |= SPI_CTL1_LBM;
	} else {
		HWREG(cfg->base + SPI_O_CTL1) &= ~SPI_CTL1_LBM;
	}

	/*
	 * SPI_CTL1_MSB_ENABLE (0x10) = MSB first (default after SPIConfigSetExpClk).
	 * SPI_CTL1_MSB_DISABLE (0x00) = LSB first.
	 */
	if (config->operation & SPI_TRANSFER_LSB) {
		HWREG(cfg->base + SPI_O_CTL1) &= ~SPI_CTL1_MSB_ENABLE;
	} else {
		HWREG(cfg->base + SPI_O_CTL1) |= SPI_CTL1_MSB_ENABLE;
	}

#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
	/*
	 * Program the FIFO thresholds low on the TX side so the uDMA refills
	 * the TX FIFO early and it stays ahead of the shift register at high
	 * SCK rates. With the reset default (TX <= 1/2 empty) and a burst
	 * length of one, at >= 8 MHz the FIFO drains faster than the uDMA can
	 * refill it one byte at a time and stale/undefined bytes are clocked
	 * out, producing the "every-other-byte wrong" pattern seen on both
	 * controller RX and peripheral TX. Mirrors the CC27xx workaround
	 * (SPI driver, DMA errata UDMA_01).
	 *
	 * TX <= 1/4 empty: uDMA fires when FIFO has >= 6 free entries.
	 * RX >= 1/2 full : uDMA fires when FIFO has >= 4 valid entries.
	 */
	HWREG(cfg->base + SPI_O_IFLS) = SPI_IFLS_TXIFLSEL_LVL_1_2 | SPI_IFLS_RXIFLSEL_LVL_1_2;
#endif

	SPIEnable(cfg->base);

	LOG_DBG("Configured: %s, %u Hz, mode %u, %s first, loopback %d",
		SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(config->operation) ? "peripheral" : "controller",
		(unsigned int)config->frequency,
		(unsigned int)(SPI_MODE_GET(config->operation) & (SPI_MODE_CPOL | SPI_MODE_CPHA)),
		(config->operation & SPI_TRANSFER_LSB) ? "LSB" : "MSB",
		!!(SPI_MODE_GET(config->operation) & SPI_MODE_LOOP));

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_SPI_SLAVE)
	/*
	 * In slave/peripheral mode the master can assert CS at any time, so
	 * the SPI IP must remain powered for the whole session. Acquire the
	 * policy lock once here; it is released in spi_cc13x4_cc26x4_release().
	 */
	if (SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(config->operation) && !data->slave_pm_lock_held) {
		pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		data->slave_pm_lock_held = true;
	}
#endif

	return 0;
}

/**
 * @brief Polled transfer: the CPU moves every frame through the FIFOs.
 *
 * Used when DMA support is not built in, or for an instance that has no
 * "dmas" property.
 */
static int spi_cc13x4_cc26x4_transceive_poll(const struct device *dev,
					     const struct spi_config *config,
					     const struct spi_buf_set *tx_bufs,
					     const struct spi_buf_set *rx_bufs)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	uint32_t txd, rxd;
	int err;

	spi_context_lock(ctx, false, NULL, NULL, config);
	pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);

	err = spi_cc13x4_cc26x4_configure(dev, config);
	if (err) {
		goto done;
	}

	spi_context_buffers_setup(ctx, tx_bufs, rx_bufs, 1);

	if (!SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(config->operation)) {
		spi_context_cs_control(ctx, true);
	}

	do {
		if (spi_context_tx_buf_on(ctx)) {
			txd = *ctx->tx_buf;
		} else {
			txd = 0U;
		}

		SPIDataPut(cfg->base, txd);

		spi_context_update_tx(ctx, 1, 1);

		SPIDataGet(cfg->base, &rxd);

		if (spi_context_rx_buf_on(ctx)) {
			*ctx->rx_buf = rxd;
		}

		spi_context_update_rx(ctx, 1, 1);
	} while (spi_context_tx_on(ctx) || spi_context_rx_on(ctx));

	if (!SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(config->operation)) {
		spi_context_cs_control(ctx, false);
	}

done:
	pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
	spi_context_release(ctx, err);
	return err;
}

#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
/**
 * @brief Flush the TX and RX FIFOs.
 *
 * The CC13x4/CC26x4 SPI has no atomic FIFO reset: ENABLE must be low while
 * CTL1.FIFORST is pulsed.
 */
static void spi_cc13x4_cc26x4_reset_txrx_fifos(const struct device *dev)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;

	SPIDisable(cfg->base);
	HWREG(cfg->base + SPI_O_CTL1) |= SPI_CTL1_FIFORST;
	/* The reset needs a few clock cycles to take effect */
	k_busy_wait(1);
	HWREG(cfg->base + SPI_O_CTL1) &= ~SPI_CTL1_FIFORST;
	SPIEnable(cfg->base);
}

/**
 * @brief Acknowledge the "DMA done" event of a channel.
 *
 * On this device the event is latched in UDMA:REQDONE and is only cleared by
 * writing a 1 to the channel bit there (SPI:ICLR has no effect on it). The SPI
 * DMA request must already be disabled, otherwise the SPI re-raises the request
 * and the bit is set again.
 */
static inline void spi_cc13x4_cc26x4_dma_done_clear(uint8_t dma_channel)
{
	sys_write32(BIT(dma_channel), UDMA0_BASE + UDMA_O_REQDONE);
}

/**
 * @brief Initialize the DMA configuration structs.
 *
 * Fills in everything that does not change from one transfer to the next. The
 * buffer addresses and sizes are set per chunk by the dma_load_*() helpers.
 */
static void spi_cc13x4_cc26x4_dma_xfer_config(const struct device *dev)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	struct spi_cc13x4_cc26x4_data *data = dev->data;

	data->block_cfg_tx.dest_address = cfg->base + SPI_O_TXDATA;
	data->block_cfg_tx.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	data->dma_cfg_tx.dma_slot = 0; /* not used, CC13x4 channels are silicon-fixed */
	data->dma_cfg_tx.channel_direction = MEMORY_TO_PERIPHERAL;
	data->dma_cfg_tx.block_count = 1;
	data->dma_cfg_tx.head_block = &data->block_cfg_tx;
	data->dma_cfg_tx.source_data_size = SPI_CC13X4_CC26X4_DFS;
	data->dma_cfg_tx.dest_data_size = SPI_CC13X4_CC26X4_DFS;
	data->dma_cfg_tx.source_burst_length = SPI_CC13X4_CC26X4_DMA_BURST_LEN;
	data->dma_cfg_tx.dest_burst_length = SPI_CC13X4_CC26X4_DMA_BURST_LEN;
	/* Completion is reported by the SPI interrupt, not by the DMA callback */
	data->dma_cfg_tx.dma_callback = NULL;
	data->dma_cfg_tx.user_data = NULL;

	data->block_cfg_rx.source_address = cfg->base + SPI_O_RXDATA;
	data->block_cfg_rx.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	data->dma_cfg_rx.dma_slot = 0;
	data->dma_cfg_rx.channel_direction = PERIPHERAL_TO_MEMORY;
	data->dma_cfg_rx.block_count = 1;
	data->dma_cfg_rx.head_block = &data->block_cfg_rx;
	data->dma_cfg_rx.source_data_size = SPI_CC13X4_CC26X4_DFS;
	data->dma_cfg_rx.dest_data_size = SPI_CC13X4_CC26X4_DFS;
	data->dma_cfg_rx.source_burst_length = SPI_CC13X4_CC26X4_DMA_BURST_LEN;
	data->dma_cfg_rx.dest_burst_length = SPI_CC13X4_CC26X4_DMA_BURST_LEN;
	data->dma_cfg_rx.dma_callback = NULL;
	data->dma_cfg_rx.user_data = NULL;
}

/** @brief Configure the TX channel for the next chunk (dummy data if no TX buffer). */
static int spi_cc13x4_cc26x4_dma_load_tx(const struct device *dev, size_t len)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	int ret;

	if (spi_context_tx_buf_on(ctx)) {
		data->block_cfg_tx.source_address = (uint32_t)ctx->tx_buf;
		data->block_cfg_tx.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	} else {
		data->block_cfg_tx.source_address = (uint32_t)&dummy_tx_data;
		data->block_cfg_tx.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	}
	data->block_cfg_tx.block_size = len * SPI_CC13X4_CC26X4_DFS;

	LOG_DBG("DMA TX ch%u config: %s 0x%08x -> TXDATA 0x%08x, %u byte(s)", cfg->dma_channel_tx,
		spi_context_tx_buf_on(ctx) ? "buf" : "dummy", data->block_cfg_tx.source_address,
		data->block_cfg_tx.dest_address, data->block_cfg_tx.block_size);

	ret = dma_config(cfg->dma_dev, cfg->dma_channel_tx, &data->dma_cfg_tx);
	if (ret) {
		LOG_ERR("dma_config() failed for TX ch%u: %d", cfg->dma_channel_tx, ret);
	}

	return ret;
}

/** @brief Configure the RX channel for the next chunk (discard data if no RX buffer). */
static int spi_cc13x4_cc26x4_dma_load_rx(const struct device *dev, size_t len)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	int ret;

	if (spi_context_rx_buf_on(ctx)) {
		data->block_cfg_rx.dest_address = (uint32_t)ctx->rx_buf;
		data->block_cfg_rx.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	} else {
		data->block_cfg_rx.dest_address = (uint32_t)&dummy_rx_data;
		data->block_cfg_rx.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	}
	data->block_cfg_rx.block_size = len * SPI_CC13X4_CC26X4_DFS;

	LOG_DBG("DMA RX ch%u config: RXDATA 0x%08x -> %s 0x%08x, %u byte(s)", cfg->dma_channel_rx,
		data->block_cfg_rx.source_address, spi_context_rx_buf_on(ctx) ? "buf" : "dummy",
		data->block_cfg_rx.dest_address, data->block_cfg_rx.block_size);

	ret = dma_config(cfg->dma_dev, cfg->dma_channel_rx, &data->dma_cfg_rx);
	if (ret) {
		LOG_ERR("dma_config() failed for RX ch%u: %d", cfg->dma_channel_rx, ret);
	}

	return ret;
}

/**
 * @brief Stop both DMA channels and mask/clear every DMA related event.
 */
static void spi_cc13x4_cc26x4_dma_halt(const struct device *dev)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;

	/* Stop the SPI from raising requests first, then the channels */
	SPIDMADisable(cfg->base, SPI_DMA_TX | SPI_DMA_RX);
	dma_stop(cfg->dma_dev, cfg->dma_channel_tx);
	dma_stop(cfg->dma_dev, cfg->dma_channel_rx);

	SPIIntDisable(cfg->base, SPI_CC13X4_CC26X4_INT_MASK);
	spi_cc13x4_cc26x4_dma_done_clear(cfg->dma_channel_tx);
	spi_cc13x4_cc26x4_dma_done_clear(cfg->dma_channel_rx);
	SPIIntClear(cfg->base, SPI_CC13X4_CC26X4_INT_MASK);
}

/**
 * @brief Start the DMA transfer of both channels for the chunk that was loaded.
 *
 * Clears and enables interrupts per-channel before starting each DMA channel.
 * This prevents a stale TX-done event from a prior transfer firing spuriously
 * on the slave before the RX DMA is armed (ISR storm fix).
 */
static int spi_cc13x4_cc26x4_dma_start(const struct device *dev)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	int ret;

	/* TX channel: disable trigger, clear stale event, enable interrupt, start DMA */
	SPIDMADisable(cfg->base, SPI_DMA_TX);
	spi_cc13x4_cc26x4_dma_done_clear(cfg->dma_channel_tx);
	SPIIntClear(cfg->base, SPI_DMA_DONE_TX);
	SPIIntEnable(cfg->base, SPI_DMA_DONE_TX);

	ret = dma_start(cfg->dma_dev, cfg->dma_channel_tx);
	if (ret) {
		LOG_ERR("dma_start() failed for TX ch%u: %d", cfg->dma_channel_tx, ret);
		return ret;
	}

	/* RX channel: disable trigger, clear stale event, enable interrupt, start DMA */
	SPIDMADisable(cfg->base, SPI_DMA_RX);
	spi_cc13x4_cc26x4_dma_done_clear(cfg->dma_channel_rx);
	SPIIntClear(cfg->base, SPI_DMA_DONE_RX | SPI_RXFIFO_OVF);
	SPIIntEnable(cfg->base, SPI_DMA_DONE_RX | SPI_RXFIFO_OVF);

	ret = dma_start(cfg->dma_dev, cfg->dma_channel_rx);
	if (ret) {
		LOG_ERR("dma_start() failed for RX ch%u: %d", cfg->dma_channel_rx, ret);
		return ret;
	}

	/*
	 * Both channels are armed. Enable DMA triggers last - RX first so
	 * it drains the FIFO before TX starts the clock (master) or accepts
	 * data (slave).
	 */
	SPIDMAEnable(cfg->base, SPI_DMA_RX);
	SPIDMAEnable(cfg->base, SPI_DMA_TX);

	LOG_DBG("DMA started: TX ch%u + RX ch%u armed, SPI DMACR=0x%x IMASK=0x%x",
		cfg->dma_channel_tx, cfg->dma_channel_rx,
		(unsigned int)HWREG(cfg->base + SPI_O_DMACR),
		(unsigned int)HWREG(cfg->base + SPI_O_IMASK));

	return 0;
}

/**
 * @brief Arm the DMA for the next chunk of the current transaction.
 *
 * @retval 0 a chunk was started
 * @retval 1 there is nothing left to transfer
 * @retval <0 error, DMA was halted
 */
static int spi_cc13x4_cc26x4_dma_prime(const struct device *dev)
{
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	size_t tx_len = MIN(data->ctx.tx_len, SPI_CC13X4_CC26X4_MAX_DMA_XFER);
	size_t rx_len = MIN(data->ctx.rx_len, SPI_CC13X4_CC26X4_MAX_DMA_XFER);
	size_t load_tx_len, load_rx_len;
	int ret;

	if (tx_len == 0 && rx_len == 0) {
		LOG_DBG("No data left, transaction complete");
		return 1;
	}

	if (SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(data->ctx.config->operation)) {
		/*
		 * Slave mode: arm each DMA channel independently for its full
		 * remaining length. Using MIN(tx, rx) for both causes a TX FIFO
		 * underrun when TX > RX (e.g. test_short_rx): the ISR re-arm
		 * latency - driven by the extra UDMA REQDONE latch clear that
		 * CC13x4 requires - exceeds the ~2 us/byte window at 4 MHz
		 * before the master finishes clocking the short side.
		 *
		 * Arming each channel for its real length means no re-arm is
		 * needed mid-transfer: TX_DONE and RX_DONE simply fire
		 * independently and the ISR exhausts each direction naturally.
		 */
		load_tx_len = tx_len ? tx_len : rx_len;
		load_rx_len = rx_len ? rx_len : tx_len;
	} else {
		/*
		 * Controller mode: synchronise on the shorter side so a chunk
		 * boundary never leaves one channel idle while the other runs.
		 * The ISR re-arms both channels for the next chunk together.
		 */
		if (tx_len == 0) {
			load_tx_len = rx_len;
			load_rx_len = rx_len;
		} else if (rx_len == 0) {
			load_tx_len = tx_len;
			load_rx_len = tx_len;
		} else {
			load_tx_len = MIN(tx_len, rx_len);
			load_rx_len = load_tx_len;
		}
	}

	LOG_DBG("Next chunk: tx %u / rx %u frame(s) (ctx tx_len %u, rx_len %u)", load_tx_len,
		load_rx_len, data->ctx.tx_len, data->ctx.rx_len);

	data->dma_tx_xfer_len = load_tx_len;
	data->dma_rx_xfer_len = load_rx_len;
	data->dma_status_flags = 0;

	/* dma_config() refuses a channel that is still enabled */
	spi_cc13x4_cc26x4_dma_halt(dev);

	ret = spi_cc13x4_cc26x4_dma_load_tx(dev, load_tx_len);
	if (ret) {
		return ret;
	}

	ret = spi_cc13x4_cc26x4_dma_load_rx(dev, load_rx_len);
	if (ret) {
		return ret;
	}

	ret = spi_cc13x4_cc26x4_dma_start(dev);
	if (ret) {
		spi_cc13x4_cc26x4_dma_halt(dev);
	}

	return ret;
}

/**
 * @brief Tear a transaction down and report it.
 *
 * Called from the ISR (and from the thread when there was nothing to
 * transfer). Wakes up the sync caller or runs the async callback.
 */
static void spi_cc13x4_cc26x4_dma_finish(const struct device *dev, int status)
{
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	bool is_slave = SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(ctx->config->operation);

	if (status) {
		LOG_ERR("Transfer failed (%d), stopping DMA and flushing FIFOs", status);
	} else {
		LOG_DBG("Transfer done, %s completing", is_slave ? "slave" : "releasing CS and");
	}

	spi_cc13x4_cc26x4_dma_halt(dev);
	if (status) {
		spi_cc13x4_cc26x4_reset_txrx_fifos(dev);
	}
	data->dma_status_flags = 0;

	if (!is_slave) {
		spi_context_cs_control(ctx, false);
	}
	pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
	spi_context_complete(ctx, dev, status);
}

static void spi_cc13x4_cc26x4_isr(const struct device *dev)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	uint32_t status;
	int ret;

	/* Lock interrupts for the entire ISR body so that
	 * dma_status_flags is read/written atomically.
	 */
	unsigned int key = irq_lock();

	status = SPIIntStatus(cfg->base, true);

	LOG_DBG("SPI IRQ: MIS=0x%x (RX done %d, TX done %d, ovf %d)", status,
		!!(status & SPI_DMA_DONE_RX), !!(status & SPI_DMA_DONE_TX),
		!!(status & SPI_RXFIFO_OVF));

	/*
	 * Spurious interrupt with null status: can happen after SPIIntDisable
	 * clears the mask while an edge is still latched. Ignore it.
	 */
	if (!status) {
		irq_unlock(key);
		return;
	}

	if (status & SPI_RXFIFO_OVF) {
		LOG_ERR("RX FIFO overflow");
		spi_cc13x4_cc26x4_dma_finish(dev, -EIO);
		irq_unlock(key);
		return;
	}

	/*
	 * Disable the per-channel interrupt as soon as it fires so the MIS
	 * latch cannot re-trigger the ISR while we are still waiting for the
	 * other channel. Without this, the CC13x4 SPI MIS register keeps the
	 * DMA-done bit set even after SPIDMADisable, causing an ISR storm that
	 * starves the master workqueue thread.
	 */
	if (status & SPI_DMA_DONE_RX) {
		SPIDMADisable(cfg->base, SPI_DMA_RX);
		spi_cc13x4_cc26x4_dma_done_clear(cfg->dma_channel_rx);
		SPIIntDisable(cfg->base, SPI_DMA_DONE_RX);
		data->dma_status_flags |= SPI_CC13X4_CC26X4_DMA_RX_DONE;
	}

	if (status & SPI_DMA_DONE_TX) {
		SPIDMADisable(cfg->base, SPI_DMA_TX);
		spi_cc13x4_cc26x4_dma_done_clear(cfg->dma_channel_tx);
		SPIIntDisable(cfg->base, SPI_DMA_DONE_TX);
		data->dma_status_flags |= SPI_CC13X4_CC26X4_DMA_TX_DONE;
	}

	/*
	 * In controller mode both TX and RX DMA channels complete together, so
	 * wait for both before advancing.
	 *
	 * In peripheral (slave) mode TX_DONE and RX_DONE fire independently:
	 * TX fires when the slave has sent all its bytes; RX fires when the
	 * master has clocked in all bytes. Each direction is re-armed
	 * individually as it completes. The transaction is finished only when
	 * both directions are exhausted.
	 */
	if (SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(ctx->config->operation)) {
		bool rx_done = data->dma_status_flags & SPI_CC13X4_CC26X4_DMA_RX_DONE;
		bool tx_done = data->dma_status_flags & SPI_CC13X4_CC26X4_DMA_TX_DONE;

		/*
		 * Slave mode: re-arm each direction independently from the ISR.
		 * We must NOT call dma_prime (which calls dma_halt + FIFO flush)
		 * because the master is still clocking - halting both DMA channels
		 * between scatter-gather chunks causes TX FIFO underflow and RX data
		 * loss. Instead, reconfigure and restart each direction individually
		 * as it completes.
		 *
		 * Pass 0 when ctx->*_len is already 0 to avoid over-advancing the
		 * context pointer past the end of the buffer.
		 */
		if (tx_done) {
			spi_context_update_tx(ctx, SPI_CC13X4_CC26X4_DFS,
					      ctx->tx_len ? data->dma_tx_xfer_len : 0);
		}

		if (rx_done) {
			spi_context_update_rx(ctx, SPI_CC13X4_CC26X4_DFS,
					      ctx->rx_len ? data->dma_rx_xfer_len : 0);
		}

		/* If both directions exhausted, finish the transaction */
		if (!spi_context_tx_on(ctx) && !spi_context_rx_on(ctx)) {
			LOG_DBG("Slave: all done (flags 0x%x)", data->dma_status_flags);
			spi_cc13x4_cc26x4_dma_finish(dev, 0);
			irq_unlock(key);
			return;
		}

		/*
		 * A direction that is done but has no more data: its DMA trigger
		 * is already disabled (done at interrupt entry above). Leave it
		 * stopped so it does not stall the SPI while the other runs.
		 */
		if (tx_done && !spi_context_tx_buf_on(ctx)) {
			data->dma_status_flags &= ~SPI_CC13X4_CC26X4_DMA_TX_DONE;
			LOG_DBG("Slave: TX exhausted, leaving TX stopped");
		}

		if (rx_done && !spi_context_rx_buf_on(ctx)) {
			data->dma_status_flags &= ~SPI_CC13X4_CC26X4_DMA_RX_DONE;
			LOG_DBG("Slave: RX exhausted, leaving RX stopped");
		}

		/* Re-arm TX if it completed and still has more data */
		if (tx_done && spi_context_tx_buf_on(ctx)) {
			size_t next_tx = MIN(ctx->tx_len, SPI_CC13X4_CC26X4_MAX_DMA_XFER);

			data->dma_tx_xfer_len = next_tx;
			data->dma_status_flags &= ~SPI_CC13X4_CC26X4_DMA_TX_DONE;
			spi_cc13x4_cc26x4_dma_load_tx(dev, next_tx);
			/* CC13x4: clear UDMA REQDONE latch, clear SPI ICLR, re-enable
			 * interrupt, start DMA channel, re-enable SPI DMA trigger.
			 */
			spi_cc13x4_cc26x4_dma_done_clear(cfg->dma_channel_tx);
			SPIIntClear(cfg->base, SPI_DMA_DONE_TX);
			SPIIntEnable(cfg->base, SPI_DMA_DONE_TX);
			dma_start(cfg->dma_dev, cfg->dma_channel_tx);
			SPIDMAEnable(cfg->base, SPI_DMA_TX);
			LOG_DBG("Slave: re-armed TX %u frames", next_tx);
		}

		/* Re-arm RX if it completed and still has more data */
		if (rx_done && spi_context_rx_buf_on(ctx)) {
			size_t next_rx = MIN(ctx->rx_len, SPI_CC13X4_CC26X4_MAX_DMA_XFER);

			data->dma_rx_xfer_len = next_rx;
			data->dma_status_flags &= ~SPI_CC13X4_CC26X4_DMA_RX_DONE;
			spi_cc13x4_cc26x4_dma_load_rx(dev, next_rx);
			spi_cc13x4_cc26x4_dma_done_clear(cfg->dma_channel_rx);
			SPIIntClear(cfg->base, SPI_DMA_DONE_RX);
			SPIIntEnable(cfg->base, SPI_DMA_DONE_RX);
			dma_start(cfg->dma_dev, cfg->dma_channel_rx);
			SPIDMAEnable(cfg->base, SPI_DMA_RX);
			LOG_DBG("Slave: re-armed RX %u frames", next_rx);
		}

		/* Wait for the next RX_DONE (sole reliable completion signal) */
		if (!(data->dma_status_flags & SPI_CC13X4_CC26X4_DMA_RX_DONE)) {
			irq_unlock(key);
			return;
		}

	} else {
		/* Controller: require both TX and RX done */
		if (data->dma_status_flags !=
		    (SPI_CC13X4_CC26X4_DMA_RX_DONE | SPI_CC13X4_CC26X4_DMA_TX_DONE)) {
			LOG_DBG("Chunk half done (flags 0x%x), waiting for the other channel",
				data->dma_status_flags);
			irq_unlock(key);
			return;
		}
		LOG_DBG("Chunk of %u/%u frame(s) done (TX and RX)", data->dma_tx_xfer_len,
			data->dma_rx_xfer_len);

		spi_context_update_tx(ctx, SPI_CC13X4_CC26X4_DFS,
				      ctx->tx_len ? data->dma_tx_xfer_len : 0);
		spi_context_update_rx(ctx, SPI_CC13X4_CC26X4_DFS,
				      ctx->rx_len ? data->dma_rx_xfer_len : 0);

		ret = spi_cc13x4_cc26x4_dma_prime(dev);
		if (ret) {
			/* 1: all chunks done, <0: failed to arm the next one */
			spi_cc13x4_cc26x4_dma_finish(dev, ret < 0 ? ret : 0);
		}
	}

	irq_unlock(key);
}

/**
 * @brief Abort a transaction from thread context (sync timeout).
 */
static void spi_cc13x4_cc26x4_dma_abort(const struct device *dev)
{
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	unsigned int key = irq_lock();

	LOG_WRN("Aborting DMA transfer (flags 0x%x, tx %u / rx %u frame(s) in chunk)",
		data->dma_status_flags, data->dma_tx_xfer_len, data->dma_rx_xfer_len);

	spi_cc13x4_cc26x4_dma_halt(dev);
	spi_cc13x4_cc26x4_reset_txrx_fifos(dev);
	data->dma_status_flags = 0;
	if (ctx->config != NULL && !SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(ctx->config->operation)) {
		spi_context_cs_control(ctx, false);
	}
	pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
#ifdef CONFIG_MULTITHREADING
	/* The ISR may have completed the transfer just before the abort */
	k_sem_reset(&ctx->sync);
#endif

	irq_unlock(key);
}

/**
 * @brief Set up and start a DMA transceive, shared by the sync and async APIs.
 *
 * Returns as soon as the first chunk is armed. The ISR then runs the following
 * chunks and finally signals completion with spi_context_complete().
 *
 * On error the context is released before returning.
 */
static int spi_cc13x4_cc26x4_transceive_dma(const struct device *dev,
					    const struct spi_config *config,
					    const struct spi_buf_set *tx_bufs,
					    const struct spi_buf_set *rx_bufs, spi_callback_t cb,
					    void *userdata)
{
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	bool is_slave = SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(config->operation);
	int err;

	spi_context_lock(ctx, cb != NULL, cb, userdata, config);

	err = spi_cc13x4_cc26x4_configure(dev, config);
	if (err) {
		goto err_release;
	}

	spi_context_buffers_setup(ctx, tx_bufs, rx_bufs, SPI_CC13X4_CC26X4_DFS);

	/*
	 * In slave/peripheral mode the uDMA cannot be re-armed fast enough to
	 * catch back-to-back frames from the master, so the transfer is limited
	 * to one DMA buffer (1024 frames). Rearming mid-transfer would require
	 * DMA ping-pong, which is not yet implemented.
	 */
	if (is_slave) {
		if (spi_context_total_tx_len(ctx) > SPI_CC13X4_CC26X4_MAX_DMA_XFER ||
		    spi_context_total_rx_len(ctx) > SPI_CC13X4_CC26X4_MAX_DMA_XFER) {
			LOG_ERR("Slave DMA transfer exceeds max (%u frames)",
				SPI_CC13X4_CC26X4_MAX_DMA_XFER);
			err = -EINVAL;
			goto err_release;
		}
	}

	LOG_DBG("%s DMA transceive (%s): %u frame(s) TX, %u frame(s) RX, %u Hz",
		cb ? "Async" : "Sync", is_slave ? "peripheral" : "controller",
		(unsigned int)spi_context_total_tx_len(ctx),
		(unsigned int)spi_context_total_rx_len(ctx), (unsigned int)config->frequency);

	pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);

	if (is_slave) {
		/*
		 * Flush any stale data from a previous transaction before arming
		 * the DMA. On CC13x4/CC26x4 this requires temporarily disabling
		 * the SPI (see spi_cc13x4_cc26x4_reset_txrx_fifos).
		 */
		data->dma_status_flags = 0;
		spi_cc13x4_cc26x4_reset_txrx_fifos(dev);
	} else {
		spi_context_cs_control(ctx, true);
	}

	err = spi_cc13x4_cc26x4_dma_prime(dev);
	if (err < 0) {
		if (!is_slave) {
			spi_context_cs_control(ctx, false);
		}
		pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		goto err_release;
	}

	if (err > 0) {
		/* Empty buffers: nothing to move, complete right away */
		spi_cc13x4_cc26x4_dma_finish(dev, 0);
	}

	return 0;

err_release:
	spi_context_release(ctx, err);
	return err;
}
#endif /* CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN */

static int spi_cc13x4_cc26x4_transceive(const struct device *dev, const struct spi_config *config,
					const struct spi_buf_set *tx_bufs,
					const struct spi_buf_set *rx_bufs)
{
#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	int ret;

	if (cfg->dma_dev == NULL) {
		LOG_DBG("No DMA channels in devicetree, polled transceive");
		return spi_cc13x4_cc26x4_transceive_poll(dev, config, tx_bufs, rx_bufs);
	}

	ret = spi_cc13x4_cc26x4_transceive_dma(dev, config, tx_bufs, rx_bufs, NULL, NULL);
	if (ret) {
		LOG_ERR("Failed to start DMA transceive: %d", ret);
		return ret;
	}

	/* The ISR gives the semaphore with spi_context_complete() when all chunks are done */
	ret = spi_context_wait_for_completion(&data->ctx);
	if (ret == -ETIMEDOUT) {
		LOG_ERR("DMA transceive timed out");
		spi_cc13x4_cc26x4_dma_abort(dev);
	} else {
		LOG_DBG("Sync DMA transceive returned %d", ret);
	}

	spi_context_release(&data->ctx, ret);

	return ret;
#else
	return spi_cc13x4_cc26x4_transceive_poll(dev, config, tx_bufs, rx_bufs);
#endif
}

#ifdef CONFIG_SPI_ASYNC
static int spi_cc13x4_cc26x4_transceive_async(const struct device *dev,
					      const struct spi_config *config,
					      const struct spi_buf_set *tx_bufs,
					      const struct spi_buf_set *rx_bufs, spi_callback_t cb,
					      void *userdata)
{
#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;

	if (cb == NULL) {
		LOG_ERR("Asynchronous transfers require a callback function");
		return -EINVAL;
	}

	if (cfg->dma_dev == NULL) {
		LOG_ERR("Asynchronous transfers require DMA channels in devicetree");
		return -ENOTSUP;
	}

	return spi_cc13x4_cc26x4_transceive_dma(dev, config, tx_bufs, rx_bufs, cb, userdata);
#else
	ARG_UNUSED(dev);
	ARG_UNUSED(config);
	ARG_UNUSED(tx_bufs);
	ARG_UNUSED(rx_bufs);
	ARG_UNUSED(cb);
	ARG_UNUSED(userdata);

	LOG_ERR("Asynchronous transfers require CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN");
	return -ENOTSUP;
#endif
}
#endif /* CONFIG_SPI_ASYNC */

static int spi_cc13x4_cc26x4_release(const struct device *dev, const struct spi_config *config)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;

	if (!spi_context_configured(ctx, config)) {
		return -EINVAL;
	}
	if (SPIBusy(cfg->base)) {
		return -EBUSY;
	}

#if defined(CONFIG_PM_DEVICE) && defined(CONFIG_SPI_SLAVE)
	/*
	 * The application is done with the slave session. Release the policy
	 * lock acquired in spi_cc13x4_cc26x4_configure() so the system may
	 * enter standby again.
	 */
	if (SPI_CC13X4_CC26X4_CONFIG_IS_SLAVE(config->operation) && data->slave_pm_lock_held) {
		pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		data->slave_pm_lock_held = false;
	}
#endif

	spi_context_unlock_unconditionally(ctx);
	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int spi_cc13x4_cc26x4_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
#if defined(CONFIG_SPI_SLAVE)
	struct spi_cc13x4_cc26x4_data *data = dev->data;
#endif

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		PRCMPowerDomainOn(cfg->prcm_domain);
		while (PRCMPowerDomainsAllOn(cfg->prcm_domain) != PRCM_DOMAIN_POWER_ON) {
		}
		PRCMPeripheralRunEnable(cfg->prcm_periph);
		PRCMPeripheralSleepEnable(cfg->prcm_periph);
		PRCMPeripheralDeepSleepEnable(cfg->prcm_periph);
		PRCMLoadSet();
		while (!PRCMLoadGet()) {
		}
		break;

	case PM_DEVICE_ACTION_SUSPEND:
#if defined(CONFIG_SPI_SLAVE)
		/*
		 * The pm_policy_state_lock held by the slave configure path
		 * should prevent the PM subsystem from ever requesting SUSPEND
		 * while a slave is active. Refuse defensively in case the lock
		 * was somehow bypassed.
		 */
		if (data->slave_pm_lock_held) {
			return -EBUSY;
		}
#endif
		SPIDisable(cfg->base);
		PRCMPeripheralRunDisable(cfg->prcm_periph);
		PRCMPeripheralSleepDisable(cfg->prcm_periph);
		PRCMPeripheralDeepSleepDisable(cfg->prcm_periph);
		PRCMLoadSet();
		while (!PRCMLoadGet()) {
		}
		break;

	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif /* CONFIG_PM_DEVICE */

static DEVICE_API(spi, spi_cc13x4_cc26x4_driver_api) = {
	.transceive = spi_cc13x4_cc26x4_transceive,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = spi_cc13x4_cc26x4_transceive_async,
#endif
	.release = spi_cc13x4_cc26x4_release,
};

static int spi_cc13x4_cc26x4_init(const struct device *dev)
{
	const struct spi_cc13x4_cc26x4_config *cfg = dev->config;
	struct spi_cc13x4_cc26x4_data *data = dev->data;
	const pinctrl_soc_pin_t *fss;
	int err;

	PRCMPowerDomainOn(cfg->prcm_domain);
	PRCMPeripheralRunEnable(cfg->prcm_periph);
	PRCMPeripheralSleepEnable(cfg->prcm_periph);
	PRCMPeripheralDeepSleepEnable(cfg->prcm_periph);
	PRCMLoadSet();
	while (!PRCMLoadGet()) {
	}
	while (PRCMPowerDomainsAllOn(cfg->prcm_domain) != PRCM_DOMAIN_POWER_ON) {
	}

	/*
	 * All DIOs reset to LOW at power-on, including the SPI FSS (CS) pad, so
	 * a slave on this bus sees CS asserted until the first transfer drives
	 * FSS HIGH. The IOC mux is still at its GPIO reset default here, so
	 * drive FSS HIGH as a GPIO before pinctrl_apply_state() and SPIEnable()
	 * hand the pad over to the SPI IP, which then keeps it HIGH between
	 * transfers.
	 */
	fss = spi_cc13x4_cc26x4_find_fss_pin(dev);
	if (fss != NULL) {
		GPIO_setDio(fss->pin);
		GPIO_setOutputEnableDio(fss->pin, GPIO_OUTPUT_ENABLE);
	}

#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
	if (cfg->dma_dev != NULL) {
		if (!device_is_ready(cfg->dma_dev)) {
			LOG_ERR("DMA not ready");
			return -ENODEV;
		}
		spi_cc13x4_cc26x4_dma_xfer_config(dev);
		cfg->irq_config_func();
		LOG_INF("%s: DMA enabled (TX ch%u, RX ch%u)", dev->name, cfg->dma_channel_tx,
			cfg->dma_channel_rx);
	} else {
		LOG_INF("%s: no \"dmas\" in devicetree, using polled transfers", dev->name);
	}
#endif

	err = spi_context_cs_configure_all(&data->ctx);
	if (err < 0) {
		return err;
	}

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

#define SPI_CC13X4_PRCM_DOMAIN(n) ((n) == 0 ? PRCM_DOMAIN_SERIAL : PRCM_DOMAIN_PERIPH)

#ifdef CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN
/*
 * DMA channels come from the "dmas" property (dma-names "tx", "rx"). The
 * channel numbers are fixed by the silicon, see the SPIn_{TX,RX} rows of the
 * uDMA channel assignment table in the TRM.
 */
#define SPI_CC13X4_CC26X4_DMA_INIT(n)                                                              \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, dmas),                                                \
		    (.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, tx)),                   \
		     .dma_channel_tx = DT_INST_DMAS_CELL_BY_NAME(n, tx, channel),                  \
		     .dma_channel_rx = DT_INST_DMAS_CELL_BY_NAME(n, rx, channel),),                \
		    (.dma_dev = NULL,))

#define SPI_CC13X4_CC26X4_IRQ_CONFIG_FUNC(n)                                                       \
	static void spi_cc13x4_cc26x4_irq_config_##n(void)                                         \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), spi_cc13x4_cc26x4_isr,      \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}

#define SPI_CC13X4_CC26X4_IRQ_CFG_INIT(n) .irq_config_func = spi_cc13x4_cc26x4_irq_config_##n,
#else
#define SPI_CC13X4_CC26X4_DMA_INIT(n)
#define SPI_CC13X4_CC26X4_IRQ_CONFIG_FUNC(n)
#define SPI_CC13X4_CC26X4_IRQ_CFG_INIT(n)
#endif /* CONFIG_SPI_CC13X4_CC26X4_DMA_DRIVEN */

#define SPI_CC13X4_CC26X4_INIT(n)                                                                  \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	SPI_CC13X4_CC26X4_IRQ_CONFIG_FUNC(n)                                                       \
	PM_DEVICE_DT_INST_DEFINE(n, spi_cc13x4_cc26x4_pm_action);                                  \
	static const struct spi_cc13x4_cc26x4_config spi_cc13x4_cc26x4_config_##n = {              \
		.base = DT_INST_REG_ADDR(n),                                                       \
		.prcm_periph = PRCM_PERIPH_SSI##n,                                                 \
		.prcm_domain = SPI_CC13X4_PRCM_DOMAIN(n),                                          \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		SPI_CC13X4_CC26X4_IRQ_CFG_INIT(n) SPI_CC13X4_CC26X4_DMA_INIT(n)};                  \
	static struct spi_cc13x4_cc26x4_data spi_cc13x4_cc26x4_data_##n = {                        \
		SPI_CONTEXT_INIT_LOCK(spi_cc13x4_cc26x4_data_##n, ctx),                            \
		SPI_CONTEXT_INIT_SYNC(spi_cc13x4_cc26x4_data_##n, ctx),                            \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(n), ctx)};                             \
	DEVICE_DT_INST_DEFINE(n, spi_cc13x4_cc26x4_init, PM_DEVICE_DT_INST_GET(n),                 \
			      &spi_cc13x4_cc26x4_data_##n, &spi_cc13x4_cc26x4_config_##n,          \
			      POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,                               \
			      &spi_cc13x4_cc26x4_driver_api)

DT_INST_FOREACH_STATUS_OKAY(SPI_CC13X4_CC26X4_INIT)
