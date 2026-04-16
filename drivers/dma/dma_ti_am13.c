/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

LOG_MODULE_REGISTER(dma_ti_am13, CONFIG_DMA_LOG_LEVEL);

#define DT_DRV_COMPAT ti_am13_dma

/* Register offsets from peripheral base */
#define DMA_IIDX_OFS            0x1020U
#define DMA_IMASK_OFS           0x1028U
#define DMA_ICLR_OFS            0x1048U
#define DMA_DMATCTL_OFS(ch)     (0x1110U + 4U * (ch))
#define DMA_DMACTL_OFS(ch)      (0x1200U + 16U * (ch))
#define DMA_DMASA_OFS(ch)       (0x1204U + 16U * (ch))
#define DMA_DMADA_OFS(ch)       (0x1208U + 16U * (ch))
#define DMA_DMASZ_OFS(ch)       (0x120CU + 16U * (ch))

/* DMACTL bits */
#define DMACTL_DMAREQ           BIT(0)   /* software trigger (self-clearing) */
#define DMACTL_DMAEN            BIT(1)   /* channel enable; HW clears on block done */
#define DMACTL_DMASRCWDTH_MSK   (0x7U << 8)
#define DMACTL_DMADSTWDTH_MSK   (0x7U << 12)
#define DMACTL_DMASRCINCR_MSK   (0xFU << 16)
#define DMACTL_DMADSTINCR_MSK   (0xFU << 20)
#define DMACTL_DMAEM_MSK        (0x3U << 24)
#define DMACTL_DMATM_MSK        (0x3U << 28)
#define DMACTL_DMATM_BLOCK      (0x1U << 28)

/* Source width (bits [10:8]) */
#define SRCWDTH_BYTE            (0x0U << 8)
#define SRCWDTH_HALF            (0x1U << 8)
#define SRCWDTH_WORD            (0x2U << 8)
#define SRCWDTH_LONG            (0x3U << 8)

/* Destination width (bits [14:12]) */
#define DSTWDTH_BYTE            (0x0U << 12)
#define DSTWDTH_HALF            (0x1U << 12)
#define DSTWDTH_WORD            (0x2U << 12)
#define DSTWDTH_LONG            (0x3U << 12)

/* Source increment (bits [19:16]): 0=unchanged, 2=decrement, 3=increment */
#define SRCINCR_UNCHANGED       (0x0U << 16)
#define SRCINCR_DECREMENT       (0x2U << 16)
#define SRCINCR_INCREMENT       (0x3U << 16)

/* Destination increment (bits [23:20]) */
#define DSTINCR_UNCHANGED       (0x0U << 20)
#define DSTINCR_DECREMENT       (0x2U << 20)
#define DSTINCR_INCREMENT       (0x3U << 20)

/* DMATCTL = 0: trigger source 0 (DMAREQ), external → software trigger */
#define DMATCTL_SW_TRIGGER      0U

/* IIDX values: channel N done = N + 1 */
#define IIDX_CHAN_DONE(n)       ((n) + 1U)
#define IIDX_ADDR_ERR           0x19U

struct ti_am13_dma_chan {
	dma_callback_t callback;
	void *user_data;
};

struct ti_am13_dma_config {
	uintptr_t base;
	uint8_t num_channels;
	void (*irq_config)(const struct device *dev);
};

struct ti_am13_dma_data {
	struct ti_am13_dma_chan chan[12];
};

static inline uint32_t dma_rd(uintptr_t base, uint32_t ofs)
{
	return sys_read32(base + ofs);
}

static inline void dma_wr(uintptr_t base, uint32_t ofs, uint32_t val)
{
	sys_write32(val, base + ofs);
}

static uint32_t data_size_to_src_width(uint32_t bytes)
{
	switch (bytes) {
	case 2:  return SRCWDTH_HALF;
	case 4:  return SRCWDTH_WORD;
	case 8:  return SRCWDTH_LONG;
	default: return SRCWDTH_BYTE;
	}
}

static uint32_t data_size_to_dst_width(uint32_t bytes)
{
	switch (bytes) {
	case 2:  return DSTWDTH_HALF;
	case 4:  return DSTWDTH_WORD;
	case 8:  return DSTWDTH_LONG;
	default: return DSTWDTH_BYTE;
	}
}

static uint32_t addr_adj_to_src_incr(uint16_t adj)
{
	switch (adj) {
	case DMA_ADDR_ADJ_DECREMENT: return SRCINCR_DECREMENT;
	case DMA_ADDR_ADJ_NO_CHANGE: return SRCINCR_UNCHANGED;
	default:                     return SRCINCR_INCREMENT;
	}
}

static uint32_t addr_adj_to_dst_incr(uint16_t adj)
{
	switch (adj) {
	case DMA_ADDR_ADJ_DECREMENT: return DSTINCR_DECREMENT;
	case DMA_ADDR_ADJ_NO_CHANGE: return DSTINCR_UNCHANGED;
	default:                     return DSTINCR_INCREMENT;
	}
}

static void ti_am13_dma_isr(const struct device *dev)
{
	const struct ti_am13_dma_config *cfg = dev->config;
	struct ti_am13_dma_data *data = dev->data;
	uint32_t iidx;

	/* IIDX returns the highest-priority pending interrupt index and
	 * auto-advances; loop until all pending interrupts are serviced. */
	while ((iidx = dma_rd(cfg->base, DMA_IIDX_OFS)) != 0) {
		if (iidx >= 1U && iidx <= (uint32_t)cfg->num_channels) {
			uint32_t ch = iidx - 1U;

			dma_wr(cfg->base, DMA_ICLR_OFS, BIT(ch));

			if (data->chan[ch].callback) {
				data->chan[ch].callback(dev,
							data->chan[ch].user_data,
							ch, 0);
			}
		} else if (iidx == IIDX_ADDR_ERR) {
			LOG_ERR("DMA address error");
			dma_wr(cfg->base, DMA_ICLR_OFS, BIT(IIDX_ADDR_ERR - 1U));
		}
	}
}

static int ti_am13_dma_configure(const struct device *dev, uint32_t channel,
				 struct dma_config *config)
{
	const struct ti_am13_dma_config *cfg = dev->config;
	struct ti_am13_dma_data *data = dev->data;
	struct dma_block_config *block = config->head_block;
	uint32_t dmactl;
	uint32_t xfer_count;

	if (channel >= cfg->num_channels) {
		LOG_ERR("channel %u out of range", channel);
		return -EINVAL;
	}

	if (config->block_count != 1) {
		LOG_ERR("only single-block transfers supported");
		return -ENOTSUP;
	}

	if (config->channel_direction != MEMORY_TO_MEMORY) {
		LOG_ERR("only memory-to-memory supported");
		return -ENOTSUP;
	}

	if (config->source_data_size != config->dest_data_size) {
		LOG_ERR("source and dest data sizes must match");
		return -EINVAL;
	}

	xfer_count = block->block_size / config->source_data_size;
	if (xfer_count == 0 || xfer_count > 0xFFFFU) {
		LOG_ERR("invalid transfer count %u", xfer_count);
		return -EINVAL;
	}

	data->chan[channel].callback  = config->dma_callback;
	data->chan[channel].user_data = config->user_data;

	dmactl = DMACTL_DMATM_BLOCK
		| data_size_to_src_width(config->source_data_size)
		| data_size_to_dst_width(config->dest_data_size)
		| addr_adj_to_src_incr(block->source_addr_adj)
		| addr_adj_to_dst_incr(block->dest_addr_adj);

	/* Software trigger: trigger source 0 (DMAREQ), external */
	dma_wr(cfg->base, DMA_DMATCTL_OFS(channel), DMATCTL_SW_TRIGGER);
	dma_wr(cfg->base, DMA_DMACTL_OFS(channel), dmactl);
	dma_wr(cfg->base, DMA_DMASA_OFS(channel),  block->source_address);
	dma_wr(cfg->base, DMA_DMADA_OFS(channel),  block->dest_address);
	dma_wr(cfg->base, DMA_DMASZ_OFS(channel),  xfer_count);

	/* Enable interrupt for this channel */
	uint32_t imask = dma_rd(cfg->base, DMA_IMASK_OFS);

	imask |= BIT(channel);
	dma_wr(cfg->base, DMA_IMASK_OFS, imask);

	return 0;
}

static int ti_am13_dma_start(const struct device *dev, uint32_t channel)
{
	const struct ti_am13_dma_config *cfg = dev->config;
	uint32_t dmactl;

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	/* Enable channel and trigger the block transfer; ISR handles completion */
	dmactl = dma_rd(cfg->base, DMA_DMACTL_OFS(channel));
	dmactl |= DMACTL_DMAEN | DMACTL_DMAREQ;
	dma_wr(cfg->base, DMA_DMACTL_OFS(channel), dmactl);

	return 0;
}

static int ti_am13_dma_stop(const struct device *dev, uint32_t channel)
{
	const struct ti_am13_dma_config *cfg = dev->config;
	uint32_t dmactl;

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	/* Disable channel and its interrupt */
	dmactl = dma_rd(cfg->base, DMA_DMACTL_OFS(channel));
	dmactl &= ~DMACTL_DMAEN;
	dma_wr(cfg->base, DMA_DMACTL_OFS(channel), dmactl);

	uint32_t imask = dma_rd(cfg->base, DMA_IMASK_OFS);

	imask &= ~BIT(channel);
	dma_wr(cfg->base, DMA_IMASK_OFS, imask);

	return 0;
}

static int ti_am13_dma_get_status(const struct device *dev, uint32_t channel,
				  struct dma_status *status)
{
	const struct ti_am13_dma_config *cfg = dev->config;
	uint32_t dmactl, dmasz;

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	dmactl = dma_rd(cfg->base, DMA_DMACTL_OFS(channel));
	dmasz  = dma_rd(cfg->base, DMA_DMASZ_OFS(channel));

	status->busy           = !!(dmactl & DMACTL_DMAEN);
	status->dir            = MEMORY_TO_MEMORY;
	status->pending_length = dmasz & 0xFFFFU;

	return 0;
}

static int ti_am13_dma_init(const struct device *dev)
{
	const struct ti_am13_dma_config *cfg = dev->config;

	cfg->irq_config(dev);
	return 0;
}

static DEVICE_API(dma, ti_am13_dma_driver_api) = {
	.config     = ti_am13_dma_configure,
	.start      = ti_am13_dma_start,
	.stop       = ti_am13_dma_stop,
	.get_status = ti_am13_dma_get_status,
};

#define TI_AM13_DMA_INIT(inst)                                                  \
	static void ti_am13_dma_irq_config_##inst(const struct device *dev)     \
	{                                                                        \
		IRQ_CONNECT(DT_INST_IRQN(inst),                                  \
			    DT_INST_IRQ(inst, priority),                         \
			    ti_am13_dma_isr,                                     \
			    DEVICE_DT_INST_GET(inst), 0);                        \
		irq_enable(DT_INST_IRQN(inst));                                  \
	}                                                                        \
	static struct ti_am13_dma_config dma_config_##inst = {                  \
		.base         = DT_INST_REG_ADDR(inst),                         \
		.num_channels = DT_INST_PROP(inst, dma_channels),               \
		.irq_config   = ti_am13_dma_irq_config_##inst,                  \
	};                                                                       \
	static struct ti_am13_dma_data dma_data_##inst;                         \
	DEVICE_DT_INST_DEFINE(inst, ti_am13_dma_init, NULL, &dma_data_##inst,  \
			      &dma_config_##inst, PRE_KERNEL_1,                 \
			      CONFIG_DMA_INIT_PRIORITY, &ti_am13_dma_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TI_AM13_DMA_INIT);
