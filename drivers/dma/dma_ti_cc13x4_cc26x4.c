/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_cc13x4_cc26x4_dma

#include <zephyr/logging/log.h>
#include <errno.h>
#include <driverlib/prcm.h>

#include <driverlib/udma.h>
#include <inc/hw_types.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/dma.h>

LOG_MODULE_REGISTER(dma_cc13x4_cc26x4, CONFIG_DMA_LOG_LEVEL);

struct dma_cc13x4_cc26x4_config {
	uint32_t base;
};

struct dma_cc13x4_cc26x4_data {
	__aligned(1024) tDMAControlTable desc[UDMA_NUM_CHANNELS * 2]; /* primary + alternate */
	struct {
		dma_callback_t cb;
		void *user_data;
		uint8_t data_size;
	} channels[UDMA_NUM_CHANNELS];
};

static void dma_cc13x4_cc26x4_isr(const void *arg)
{
	const struct device *dev = arg;
	const struct dma_cc13x4_cc26x4_config *cfg = dev->config;
	struct dma_cc13x4_cc26x4_data *data = dev->data;
	uint32_t status = uDMAIntStatus(cfg->base);

	/*
	 * Only clear REQDONE bits for channels this driver owns (i.e. channels
	 * with a registered callback).  Channels whose cb is NULL are owned by
	 * a peripheral driver (e.g. UART0 TX/RX) that clears REQDONE in its
	 * own ISR.  Clearing those bits here races with the peripheral ISR and
	 * can cause it to miss the completion event entirely.
	 */
	uint32_t owned = 0;

	for (int i = 0; i < UDMA_NUM_CHANNELS; i++) {
		if ((status & BIT(i)) && data->channels[i].cb) {
			owned |= BIT(i);
		}
	}
	if (owned) {
		uDMAIntClear(cfg->base, owned);
	}

	for (int i = 0; i < UDMA_NUM_CHANNELS; i++) {
		if (!(status & BIT(i))) {
			continue;
		}
		if (data->channels[i].cb) {
			data->channels[i].cb(dev, data->channels[i].user_data, i,
					     DMA_STATUS_COMPLETE);
		}
	}
}

static int dma_cc13x4_cc26x4_enable(const struct dma_cc13x4_cc26x4_config *cfg,
				    struct dma_cc13x4_cc26x4_data *data)
{
	PRCMPeripheralRunEnable(PRCM_PERIPH_UDMA);
	PRCMLoadSet();

	/* Wait until PRCM has applied the clock enable to the DMA peripheral */
	while (!PRCMLoadGet()) {
	}

	uDMAEnable(cfg->base);

	/* Set base address for channel control table (descriptors) */
	uDMAControlBaseSet(cfg->base, data->desc);

	return 0;
}

static int dma_cc13x4_cc26x4_set_addr_adj(uint32_t *control, uint16_t addr_adj,
					  uint32_t addr_inc_flags, uint32_t addr_no_inc_flags,
					  uint32_t inc_mask)
{
	*control = *control & ~inc_mask;
	switch (addr_adj) {
	case DMA_ADDR_ADJ_INCREMENT:
		*control |= addr_inc_flags;
		break;
	case DMA_ADDR_ADJ_NO_CHANGE:
		*control |= addr_no_inc_flags;
		break;
	default:
		LOG_ERR("Invalid DMA address adjustment configuration !");
		return -EINVAL;
	}

	return 0;
}

static int dma_cc13x4_cc26x4_config(const struct device *dev, uint32_t channel,
				    struct dma_config *config)
{
	struct dma_block_config *block = config->head_block;
	const struct dma_cc13x4_cc26x4_config *cfg = dev->config;
	struct dma_cc13x4_cc26x4_data *data = dev->data;
	uint32_t src_inc_flags;
	uint32_t dst_inc_flags;
	uint32_t data_size;
	uint32_t control;
	uint32_t xfer_size;
	uint32_t burst_len;
	int rtn;

	if (channel >= UDMA_NUM_CHANNELS) {
		LOG_ERR("Invalid channel %d", channel);
		return -EINVAL;
	}

	if (config->block_count != 1) {
		LOG_ERR("Only single-block transfers are supported (block_count=%d)",
			config->block_count);
		return -ENOTSUP;
	}

	switch (config->source_data_size) {
	case 1:
		src_inc_flags = UDMA_SRC_INC_8;
		break;
	case 2:
		src_inc_flags = UDMA_SRC_INC_16;
		break;
	case 4:
		src_inc_flags = UDMA_SRC_INC_32;
		break;
	default:
		LOG_ERR("Invalid source data size");
		return -EINVAL;
	}

	switch (config->dest_data_size) {
	case 1:
		dst_inc_flags = UDMA_DST_INC_8;
		break;
	case 2:
		dst_inc_flags = UDMA_DST_INC_16;
		break;
	case 4:
		dst_inc_flags = UDMA_DST_INC_32;
		break;
	default:
		LOG_ERR("Invalid destination data size");
		return -EINVAL;
	}

	/* max will cause overflow or corrupt data */
	data_size = MIN(config->source_data_size, config->dest_data_size);
	switch (data_size) {
	case 1:
		control = UDMA_SIZE_8;
		break;
	case 2:
		control = UDMA_SIZE_16;
		break;
	case 4:
		control = UDMA_SIZE_32;
		break;
	default:
		LOG_ERR("Invalid data_size");
		return -EINVAL;
	}

	/* modifies control word according to source_addr_adj */
	rtn = dma_cc13x4_cc26x4_set_addr_adj(&control, block->source_addr_adj, src_inc_flags,
					     UDMA_SRC_INC_NONE, UDMA_SRC_INC_M);
	if (rtn) {
		LOG_ERR("Invalid source address adjustment type");
		return rtn;
	}

	/* modifies control word according to dest_addr_adj */
	rtn = dma_cc13x4_cc26x4_set_addr_adj(&control, block->dest_addr_adj, dst_inc_flags,
					     UDMA_DST_INC_NONE, UDMA_DST_INC_M);
	if (rtn) {
		LOG_ERR("Invalid dest address adjustment type");
		return rtn;
	}

	/* block size in bytes / data size in bytes */
	xfer_size = block->block_size / data_size;
	if (!xfer_size || xfer_size > UDMA_XFER_SIZE_MAX) {
		LOG_ERR("Invalid block size (must be in range %d to %d)", data_size,
			data_size * UDMA_XFER_SIZE_MAX);
		return -EINVAL;
	}

	burst_len = config->source_burst_length / data_size;
	if (config->source_burst_length != data_size * burst_len) {
		LOG_ERR("Source burst length is not a multiple of data size");
		return -EINVAL;
	} else if (config->source_burst_length != config->dest_burst_length) {
		LOG_ERR("Source and destination burst lengths are not equal");
		return -EINVAL;
	} else if ((burst_len <= UDMA_XFER_SIZE_MAX) && IS_POWER_OF_TWO(burst_len)) {
		/* setting arbitration size in control word */
		control |= LOG2(burst_len) << UDMA_ARB_S;
	} else {
		LOG_ERR("Computed burst length must be a power of 2 between %d and %d)", data_size,
			data_size * UDMA_XFER_SIZE_MAX);
		return -EINVAL;
	}

	/* channel enable status */
	if (uDMAChannelIsEnabled(cfg->base, channel)) {
		LOG_ERR("DMA channel is already occupied");
		return -EBUSY;
	}

	/* Save callback before arming the descriptor */
	data->channels[channel].cb = config->dma_callback;
	data->channels[channel].user_data = config->user_data;
	data->channels[channel].data_size = data_size;

	uDMAChannelControlSet(cfg->base, channel | UDMA_PRI_SELECT, control);

	uDMAChannelTransferSet(cfg->base, channel | UDMA_PRI_SELECT, UDMA_MODE_BASIC,
			       (void *)block->source_address, (void *)block->dest_address,
			       xfer_size);

	return 0;
}

static int dma_cc13x4_cc26x4_init(const struct device *dev)
{
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), dma_cc13x4_cc26x4_isr,
		    DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));

	return dma_cc13x4_cc26x4_enable(dev->config, dev->data);
}

static int dma_cc13x4_cc26x4_start(const struct device *dev, uint32_t channel)
{
	const struct dma_cc13x4_cc26x4_config *cfg = dev->config;

	uDMAChannelEnable(cfg->base, channel);
	return 0;
}

static int dma_cc13x4_cc26x4_stop(const struct device *dev, uint32_t channel)
{
	const struct dma_cc13x4_cc26x4_config *cfg = dev->config;

	uDMAChannelDisable(cfg->base, channel);
	return 0;
}

static int dma_cc13x4_cc26x4_reload(const struct device *dev, uint32_t channel, uint32_t src,
				    uint32_t dst, size_t size)
{
	const struct dma_cc13x4_cc26x4_config *cfg = dev->config;
	struct dma_cc13x4_cc26x4_data *data = dev->data;
	uint32_t xfer_size;

	if (channel >= UDMA_NUM_CHANNELS) {
		return -EINVAL;
	}

	if (data->channels[channel].data_size == 0) {
		LOG_ERR("data_size is zero");
		return -EINVAL;
	}

	xfer_size = size / data->channels[channel].data_size;

	if (!xfer_size || xfer_size > UDMA_XFER_SIZE_MAX) {
		LOG_ERR("Invalid reload size %u for channel %d", size, channel);
		return -EINVAL;
	}

	if (uDMAChannelIsEnabled(cfg->base, channel)) {
		return -EBUSY;
	}

	uDMAChannelTransferSet(cfg->base, channel | UDMA_PRI_SELECT, UDMA_MODE_BASIC, (void *)src,
			       (void *)dst, xfer_size);

	LOG_DBG("Reloaded channel %d for %08x to %08x (%u bytes)", channel, src, dst, size);

	return 0;
}

static int dma_cc13x4_cc26x4_get_status(const struct device *dev, uint32_t channel,
					struct dma_status *status)
{
	const struct dma_cc13x4_cc26x4_config *cfg = dev->config;
	struct dma_cc13x4_cc26x4_data *data = dev->data;
	uint32_t control;
	uint32_t xfer_remaining;

	if (channel >= UDMA_NUM_CHANNELS) {
		return -EINVAL;
	}

	/*
	 * busy: hardware auto-disables the channel when MODE transitions to
	 * Stop at the end of a transfer, so IsEnabled() == true means active.
	 */
	status->busy = uDMAChannelIsEnabled(cfg->base, channel);

	/*
	 * Read the live control word from the primary descriptor.
	 * The hardware decrements XFERSIZE [13:4] after each item moved and
	 * writes MODE=Stop [2:0] when the transfer finishes.
	 */
	control = data->desc[channel].ui32Control;

	if ((control & UDMA_MODE_M) == UDMA_MODE_STOP) {
		/* Transfer complete (or channel not yet started) */
		status->pending_length = 0;
	} else {
		/* XFERSIZE holds (remaining_items - 1) */
		xfer_remaining = ((control & UDMA_XFER_SIZE_M) >> UDMA_XFER_SIZE_S) + 1;
		status->pending_length = xfer_remaining * data->channels[channel].data_size;
	}

	return 0;
}

static DEVICE_API(dma, dma_cc13x4_cc26x4_api) = {
	.config = dma_cc13x4_cc26x4_config,
	.start = dma_cc13x4_cc26x4_start,
	.stop = dma_cc13x4_cc26x4_stop,
	.reload = dma_cc13x4_cc26x4_reload,
	.get_status = dma_cc13x4_cc26x4_get_status,
};

static const struct dma_cc13x4_cc26x4_config cc13x4_cc26x4_cfg = {
	.base = DT_INST_REG_ADDR(0),
};

static struct dma_cc13x4_cc26x4_data cc13x4_cc26x4_data;

DEVICE_DT_INST_DEFINE(0, &dma_cc13x4_cc26x4_init, NULL, &cc13x4_cc26x4_data, &cc13x4_cc26x4_cfg,
		      PRE_KERNEL_1, CONFIG_DMA_INIT_PRIORITY, &dma_cc13x4_cc26x4_api);
