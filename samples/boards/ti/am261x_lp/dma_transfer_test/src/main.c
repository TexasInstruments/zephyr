/*
 * Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include <zephyr/cache.h>

#define DMA_DUT_NODE DT_ALIAS(dma_dut)
#if !DT_NODE_HAS_STATUS_OKAY(DMA_DUT_NODE)
#error "Unsupported board: dma_dut devicetree alias is not defined"
#endif

/* Define source and destination buffers */
static char tx_data[64] __aligned(CONFIG_DCACHE_LINE_SIZE) =
	"Hello, testing DMA memory-to-memory transfer!";
static char rx_data[64] __aligned(CONFIG_DCACHE_LINE_SIZE) = {0};

/* DMA transfer completion callback */
static void dma_transfer_complete(const struct device *dma_dev, void *user_data, uint32_t channel,
				  int status)
{
	if (status >= 0) {
		printk("DMA transfer completed successfully!\n");
	} else {
		printk("DMA transfer failed with status %d\n", status);
	}

	/* Signal the main thread that transfer is complete */
	struct k_sem *transfer_sem = (struct k_sem *)user_data;

	k_sem_give(transfer_sem);
}

int main(void)
{
	/* Semaphore to synchronize with DMA completion */
	struct k_sem transfer_sem;
	int ret;

	k_sem_init(&transfer_sem, 0, 1);

	const struct device *dma_dev = DEVICE_DT_GET(DMA_DUT_NODE);

	if (!device_is_ready(dma_dev)) {
		printk("DMA device not ready\n");
		return -1;
	}

	/* Configure DMA channel */
	struct dma_config dma_cfg = {0};
	struct dma_block_config dma_block_cfg = {0};

	/* Define the DMA channel to use */
	uint32_t dma_channel = CONFIG_DMA_TRANSFER_CHANNEL;

	/* Setup DMA configuration */
	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = 1;    /* 1 byte */
	dma_cfg.dest_data_size = 1;      /* 1 byte */
	dma_cfg.source_burst_length = 8; /* Burst length of 8 bytes */
	dma_cfg.dest_burst_length = 8;   /* Burst length of 8 bytes */
	dma_cfg.dma_callback = dma_transfer_complete;
	dma_cfg.user_data = &transfer_sem;
	dma_cfg.complete_callback_en = 0;
	dma_cfg.error_callback_dis = 0;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_block_cfg;

	/* Setup block configuration */
	dma_block_cfg.block_size = sizeof(tx_data);
#ifdef CONFIG_DMA_64BIT
	dma_block_cfg.source_address = (uint64_t)tx_data;
	dma_block_cfg.dest_address = (uint64_t)rx_data;
#else
	dma_block_cfg.source_address = (uint32_t)tx_data;
	dma_block_cfg.dest_address = (uint32_t)rx_data;
#endif

	/* Clear the destination buffer */
	memset(rx_data, 0, sizeof(rx_data));

	printk("Starting DMA memory-to-memory transfer\n");
	printk("Source data: %s\n", tx_data);

	/* Configure DMA */
	ret = dma_config(dma_dev, dma_channel, &dma_cfg);
	if (ret != 0) {
		printk("Error: DMA configuration failed with error code %d\n", ret);
		return ret;
	}

	/* Start DMA transfer */
	ret = dma_start(dma_dev, dma_channel);
	if (ret != 0) {
		printk("Error: DMA transfer failed to start with error code %d\n", ret);
		return ret;
	}

	/* Wait for transfer completion */
	if (k_sem_take(&transfer_sem, K_MSEC(5000)) != 0) {
		printk("Error: DMA transfer timed out\n");
		dma_stop(dma_dev, dma_channel);
		return -1;
	}

	sys_cache_data_invd_range((void *)rx_data, sizeof(rx_data));

	printk("Received data: %s\n", rx_data);

	/* Verify the transfer */
	if (memcmp(tx_data, rx_data, sizeof(tx_data)) == 0) {
		printk("Memory-to-memory DMA transfer verified successfully!\n");
		printk("Test passed!\n");
	} else {
		printk("Error: Data verification failed\n");
		return -1;
	}

	return 0;
}
