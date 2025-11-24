/* Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dma/dma_ti_edma.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <zephyr/irq.h>

LOG_MODULE_REGISTER(ti_edma, CONFIG_DMA_LOG_LEVEL);

#define DT_DRV_COMPAT ti_edma

struct ti_edma_config {
	uint16_t max_num_params;
	uint16_t *edma_resources;
	uint16_t num_edma_resources;

	EDMA_Params gEdmaParams; /* Performing static allocation of this array */
	EDMA_Attrs gEdmaAttrs;   /* Stores eDMA configuration info */

	void (*register_isr)();
};

struct ti_edma_data {
	struct dma_context dma_ctx;        /* DMA Context struct */
	struct EDMA_Channel *channel_data; /* Stores necessary information per channel */
	uint32_t instNum;                  /* Instance number in devicetree */
	EDMA_Object gEdmaObject;           /* Stores run-time eDMA configs */
	EDMA_Handle gEdmaHandle;           /* Stores all DMA configuration information */
};

/* Define gEdmaHandle globally so that HAL APIs can use the structure */
uint32_t gEdmaConfigNum = CONFIG_EDMA_NUM_INSTANCES;
EDMA_Config gEdmaConfig[CONFIG_EDMA_NUM_INSTANCES]; /* points to a gEdmaConfig */

/* Defining all Static functions here */
static void write_reg_fields(uint32_t data, uint32_t reg, uint32_t mask, uint32_t shift);
static uint32_t get_pending_length(const struct device *dev, uint32_t channel);
static int populate_PARAM_set(const struct device *dev, uint32_t channel, struct dma_config *config,
			      struct dma_block_config *block, EDMACCPaRAMEntry *edmaParam);
static void EDMA_dummyIsrFxn(void *args);
static void ti_edma_resource_alloc(uint32_t *arr, uint32_t len, uint32_t start_index,
				   uint32_t end_index);
static int edma_resource_validate_and_alloc(const struct device *dev, int idx);
static int ti_edma_deconfigure(const struct device *dev, uint32_t channel);
static int ti_edma_init(const struct device *dev);
static int ti_edma_configure(const struct device *dev, uint32_t channel, struct dma_config *config);
static int ti_edma_start(const struct device *dev, uint32_t channel);
static void ti_edma_get_status(const struct device *dev, uint32_t channel,
			       struct dma_status *status);
static int ti_edma_stop(const struct device *dev, uint32_t channel);
/* All static functions have been defined */

static void write_reg_fields(uint32_t data, uint32_t reg, uint32_t mask, uint32_t shift)
{
	uint32_t reg_val;

	reg_val = sys_read32(reg);
	reg_val = (reg_val & ~(mask)) | (data << shift);
	sys_write32(reg_val, reg);
}

static uint32_t get_pending_length(const struct device *dev, uint32_t channel)
{

	struct ti_edma_data *dev_data = dev->data;
	EDMA_Handle edma_handle = dev_data->gEdmaHandle;
	uint32_t baseAddr = EDMA_getBaseAddr(edma_handle);
	uint32_t pending_length;
	uint16_t EDMA_A_COUNT, EDMA_B_COUNT, EDMA_C_COUNT;
	EDMACCPaRAMEntry paramSet;
	uint32_t param;

	uint32_t testStatus = EDMA_getMappedPaRAM(baseAddr, channel, EDMA_CHANNEL_TYPE_DMA, &param);

	if (!testStatus) {
		LOG_ERR("Unable to get PARAM set linked to channel %d\n", channel);
	}

	EDMA_getPaRAM(baseAddr, param, &paramSet);

	EDMA_A_COUNT = paramSet.aCnt;
	EDMA_B_COUNT = paramSet.bCnt;
	EDMA_C_COUNT = paramSet.cCnt;
	pending_length = EDMA_A_COUNT * EDMA_B_COUNT * EDMA_C_COUNT;

	return pending_length;
}

static int populate_PARAM_set(const struct device *dev, uint32_t channel, struct dma_config *config,
			      struct dma_block_config *block, EDMACCPaRAMEntry *edmaParam)
{

	const struct ti_edma_config *dev_config = dev->config;
	struct ti_edma_data *dev_data = dev->data;
	EDMA_Handle edma_handle = dev_data->gEdmaHandle;

	edmaParam->srcAddr = (uint32_t)block->source_address;
	edmaParam->destAddr = (uint32_t)block->dest_address;

	uint32_t tcc = channel;
	uint16_t EDMA_A_COUNT, EDMA_B_COUNT, EDMA_C_COUNT;
	uint32_t opt_flags = 0;

	switch (config->channel_direction) {
	case MEMORY_TO_MEMORY:
		/* For m2m transfer, we put A_CNT = data_size, B_CNT = block_size/A_CNT, C_CNT=1 */
		LOG_DBG("Configuring memory to memory transfer for %s, channel %d\n", dev->name,
			channel);

		if (config->source_data_size != config->dest_data_size) {
			LOG_ERR("Source and destination data size mismatch for %s. Exiting...\n",
				dev->name);
			return -ENOTSUP;
		}
		if (block->block_size % config->source_data_size != 0) {
			LOG_ERR("Block Size MUST BE A MULTIPLE of Data Size. Exiting...\n");
			return -ENOTSUP;
		}
		if ((block->block_size / config->source_data_size) > UINT16_MAX) {
			LOG_ERR("Block Size / Data Size should be lesser than OR equal to %d. "
				"Exiting...\n",
				UINT16_MAX);
			return -ENOTSUP;
		}

		dev_data->channel_data[channel].chan_dir = MEMORY_TO_MEMORY;
		uint32_t transfer_memory_size = block->block_size;

		EDMA_A_COUNT = config->source_data_size;
		EDMA_B_COUNT = (uint16_t)(block->block_size / EDMA_A_COUNT);
		EDMA_C_COUNT = 1;

		edmaParam->aCnt = (uint16_t)EDMA_A_COUNT;
		edmaParam->bCnt = (uint16_t)EDMA_B_COUNT;
		edmaParam->cCnt = (uint16_t)EDMA_C_COUNT;
		edmaParam->bCntReload = (uint16_t)EDMA_B_COUNT;

		edmaParam->srcBIdx = (int16_t)EDMA_PARAM_BIDX(EDMA_A_COUNT);
		edmaParam->destBIdx = (int16_t)EDMA_PARAM_BIDX(EDMA_A_COUNT);
		edmaParam->srcBIdxExt = (int8_t)EDMA_PARAM_BIDX_EXT(EDMA_A_COUNT);
		edmaParam->destBIdxExt = (int8_t)EDMA_PARAM_BIDX_EXT(EDMA_A_COUNT);

		edmaParam->srcCIdx = (int16_t)EDMA_A_COUNT;
		edmaParam->destCIdx = (int16_t)EDMA_A_COUNT;
		edmaParam->linkAddr = 0xFFFFU;

		edmaParam->opt |= (EDMA_OPT_TCINTEN_MASK | EDMA_OPT_SYNCDIM_MASK |
				   ((((uint32_t)tcc) << EDMA_OPT_TCC_SHIFT) & EDMA_OPT_TCC_MASK));

		break;

	case MEMORY_TO_PERIPHERAL:
		LOG_DBG("Configuring memory to peripheral transfer for %s, channel %d\n", dev->name,
			channel);

		if (config->source_data_size != config->dest_data_size) {
			LOG_ERR("Source and destination data size mismatch for %s. Exiting...\n",
				dev->name);
			return -ENOTSUP;
		}
		if (config->source_burst_length != config->dest_burst_length) {
			LOG_ERR("Source and destination burst length mismatch for %s. Exiting...\n",
				dev->name);
			return -ENOTSUP;
		}
		if (config->source_burst_length % config->source_data_size != 0) {
			LOG_ERR("Burst length MUST BE A MULTIPLE of Data size. Exiting...\n");
			return -ENOTSUP;
		}
		if (block->block_size % config->source_burst_length != 0) {
			LOG_ERR("Block Size MUST BE A MULTIPLE of Burst Length. Exiting...\n");
			return -ENOTSUP;
		}

		dev_data->channel_data[channel].chan_dir = MEMORY_TO_PERIPHERAL;

		EDMA_A_COUNT = config->source_data_size;
		EDMA_B_COUNT = config->source_burst_length / config->source_data_size;
		EDMA_C_COUNT = (uint16_t)(block->block_size / config->source_burst_length);

		edmaParam->aCnt = EDMA_A_COUNT;
		edmaParam->bCnt = EDMA_B_COUNT;
		edmaParam->cCnt = EDMA_C_COUNT;
		edmaParam->bCntReload = EDMA_B_COUNT;

		edmaParam->srcBIdx = (int16_t)EDMA_PARAM_BIDX(EDMA_A_COUNT);
		edmaParam->destBIdx = 0;
		edmaParam->srcBIdxExt = (int8_t)EDMA_PARAM_BIDX_EXT(EDMA_A_COUNT);
		edmaParam->destBIdxExt = 0;

		edmaParam->srcCIdx = (int16_t)config->source_burst_length;
		edmaParam->destCIdx = 0;
		edmaParam->linkAddr = 0xFFFFU;

		edmaParam->opt |= (EDMA_OPT_TCINTEN_MASK | EDMA_OPT_SYNCDIM_MASK |
				   ((((uint32_t)tcc) << EDMA_OPT_TCC_SHIFT) & EDMA_OPT_TCC_MASK));

		break;

	case PERIPHERAL_TO_MEMORY:
		LOG_DBG("Configuring peripheral to memory transfer for %s, channel %d\n", dev->name,
			channel);

		if (config->source_data_size != config->dest_data_size) {
			LOG_ERR("Source and destination data size mismatch for %s. Exiting...\n",
				dev->name);
			return -ENOTSUP;
		}
		if (config->source_burst_length != config->dest_burst_length) {
			LOG_ERR("Source and destination burst length mismatch for %s. Exiting...\n",
				dev->name);
			return -ENOTSUP;
		}
		if (config->source_burst_length % config->source_data_size != 0) {
			LOG_ERR("Burst length MUST BE A MULTIPLE of Data size. Exiting...\n");
			return -ENOTSUP;
		}
		if (block->block_size % config->source_burst_length != 0) {
			LOG_ERR("Block Size MUST BE A MULTIPLE of Burst Length. Exiting...\n");
			return -ENOTSUP;
		}

		dev_data->channel_data[channel].chan_dir = PERIPHERAL_TO_MEMORY;

		EDMA_A_COUNT = config->source_data_size;
		EDMA_B_COUNT = config->source_burst_length / config->source_data_size;
		EDMA_C_COUNT = (uint16_t)(block->block_size / config->source_burst_length);

		edmaParam->aCnt = EDMA_A_COUNT;
		edmaParam->bCnt = EDMA_B_COUNT;
		edmaParam->cCnt = EDMA_C_COUNT;
		edmaParam->bCntReload = EDMA_B_COUNT;

		edmaParam->srcBIdx = 0;
		edmaParam->destBIdx = (int16_t)EDMA_PARAM_BIDX(EDMA_A_COUNT);
		edmaParam->srcBIdxExt = 0;
		edmaParam->destBIdxExt = (int8_t)EDMA_PARAM_BIDX_EXT(EDMA_A_COUNT);

		edmaParam->srcCIdx = 0;
		edmaParam->destCIdx = (int16_t)config->source_burst_length;
		edmaParam->linkAddr = 0xFFFFU;

		edmaParam->opt |= (EDMA_OPT_TCINTEN_MASK | EDMA_OPT_SYNCDIM_MASK |
				   ((((uint32_t)tcc) << EDMA_OPT_TCC_SHIFT) & EDMA_OPT_TCC_MASK));

		break;

	default:
		LOG_ERR("Unsupported channel direction\n");
		return -ENOTSUP;
	}

	return 0;
}

static void EDMA_dummyIsrFxn(void *args)
{
	struct EDMA_ISR_Data *isr_data = (struct EDMA_ISR_Data *)args;

	uint32_t status;
	uint32_t pending_len = get_pending_length(isr_data->dev, isr_data->channel);

	if (pending_len != 0) {
		status = DMA_STATUS_BLOCK;
	} else {
		status = DMA_STATUS_COMPLETE;
	}

	isr_data->cb(isr_data->dev, isr_data->args, isr_data->channel, status);
}

void ti_edma_masterISR(const void *args)
{
	const struct device *dev = (const struct device *)args;
	struct ti_edma_data *dev_data = dev->data;
	EDMA_Config *config = (EDMA_Config *)dev_data->gEdmaHandle;
	const EDMA_Attrs *attrs = config->attrs;
	uint32_t baseAddr, regionId;
	uint32_t intrLow, intrHigh;

	baseAddr = attrs->baseAddr;
	regionId = attrs->initPrms.regionId;

	intrLow = EDMA_getIntrStatusRegion(baseAddr, regionId);
	intrHigh = EDMA_intrStatusHighGetRegion(baseAddr, regionId);

	for (int tcc = 0; tcc < 32; tcc++) {
		if ((intrLow & (1U << tcc)) != 0U) {
			EDMA_clrIntrRegion(baseAddr, regionId, tcc);
			intrLow &= ~(1U << tcc);
			EDMA_dummyIsrFxn(&(dev_data->channel_data[tcc].isr_data));
		}
	}
	for (int tcc = 32; tcc < 64; tcc++) {
		if ((intrLow & (1U << (tcc - 32))) != 0U) {
			EDMA_clrIntrRegion(baseAddr, regionId, tcc);
			intrLow &= ~(1U << (tcc - 32));
			EDMA_dummyIsrFxn(&(dev_data->channel_data[tcc].isr_data));
		}
	}

	/* Clear the aggregator interrupt */
	sys_write32(attrs->intrAggStatusAddr, attrs->intrAggClearMask);

	/* re evaluate the edma interrupt. */
	write_reg_fields(1, baseAddr + EDMA_TPCC_IEVAL_RN(regionId), EDMA_TPCC_IEVAL_RN_EVAL_MASK,
			 EDMA_TPCC_IEVAL_RN_EVAL_SHIFT);
}

/**
 * @brief Allocate resources in a bitmap array based on specified index range
 *
 * Sets bits in a uint32_t array to mark resources as allocated. Each bit
 * represents one resource, with the array acting as a bitmap. For example,
 * to reserve resources 0-20, the function will set the corresponding 21 bits
 * in the bitmap.
 *
 * @param arr          Pointer to uint32_t array used as resource bitmap
 * @param len          Length of the array in uint32_t elements
 * @param start_index  First resource index to allocate (inclusive)
 * @param end_index    Last resource index to allocate (inclusive)
 *
 */
static void ti_edma_resource_alloc(uint32_t *arr, uint32_t len, uint32_t start_index,
				   uint32_t end_index)
{

	if (arr == NULL || start_index > end_index || end_index >= (len * 32)) {
		LOG_WRN("Invalid values detected during edma resource allocation\n");
		LOG_WRN("array len = %d, start index = %d, end index = %d\n", len, start_index,
			end_index);
		return;
	}
	uint32_t start_elem = start_index / 32;
	uint32_t end_elem = end_index / 32;
	uint32_t start_bit = start_index % 32;
	uint32_t end_bit = end_index % 32;
	uint32_t mask;

	if (start_elem == end_elem) {
		/* All bits are in the same array element */
		mask = ((1U << (end_bit - start_bit + 1)) - 1) << start_bit;
		arr[start_elem] |= mask;
		return;
	}
	/* ELSE */
	/* First element: set bits from start_bit to 31 */
	if (start_bit > 0) {
		arr[start_elem] |= (0xFFFFFFFFU << start_bit);
	} else {
		arr[start_elem] = 0xFFFFFFFFU;
	}

	/* Middle elements: set all bits */
	for (uint32_t i = start_elem + 1; i < end_elem; i++) {
		arr[i] = 0xFFFFFFFFU;
	}

	/* Last element: set bits from 0 to end_bit */
	arr[end_elem] |= (1U << (end_bit + 1)) - 1;
}

/**
 * Validate and allocate EDMA resources
 *
 * @param resource_type Type of EDMA resource (CORE_DMA_CHANNEL, etc.)
 * @param start_index Starting index of the resource range
 * @param end_index Ending index of the resource range
 * @param ownResource Pointer to resource structure to store allocation
 *
 * @return 0 on success, negative errno on failure
 */
static int edma_resource_validate_and_alloc(const struct device *dev, int idx)
{
	const struct ti_edma_config *dev_config = dev->config;
	const struct ti_edma_data *dev_data = dev->data;

	enum EDMA_Resource_Type resource_type = dev_config->edma_resources[idx];
	uint16_t start_index = dev_config->edma_resources[idx + 1];
	uint16_t end_index = dev_config->edma_resources[idx + 2];
	EDMA_ResourceObject *ownResource = &(dev_config->gEdmaAttrs.initPrms.ownResource);

	switch (resource_type) {

	case CORE_DMA_CHANNEL:
		if (start_index < 0 || start_index > end_index ||
		    end_index >= dev_data->dma_ctx.dma_channels) {
			LOG_ERR("Invalid DMA resource with start_index %d and end_index %d\n",
				start_index, end_index);
			return -EINVAL; /* Invalid range */
		}

		ti_edma_resource_alloc(&(ownResource->dmaCh),
				       (dev_data->dma_ctx.dma_channels % 32 == 0)
					       ? (dev_data->dma_ctx.dma_channels / 32U)
					       : (dev_data->dma_ctx.dma_channels / 32U) + 1,
				       start_index, end_index);

		/* NOTE: Assuming TCC number = Dma Channel number */
		ti_edma_resource_alloc(&(ownResource->tcc),
				       (dev_data->dma_ctx.dma_channels % 32 == 0)
					       ? (dev_data->dma_ctx.dma_channels / 32U)
					       : (dev_data->dma_ctx.dma_channels / 32U) + 1,
				       start_index, end_index);

		break;

	case CORE_PARAM:
		if (start_index < 0 || start_index > end_index ||
		    end_index >= dev_config->max_num_params) {
			LOG_ERR("Invalid PARAM resource with start_index %d and end_index %d\n",
				start_index, end_index);
			return -EINVAL; /* Invalid range */
		}

		ti_edma_resource_alloc(&(ownResource->paramSet),
				       (dev_config->max_num_params % 32 == 0)
					       ? (dev_config->max_num_params / 32U)
					       : (dev_config->max_num_params / 32U) + 1,
				       start_index, end_index);

		break;

	default:
		LOG_ERR("Invalid Resource type used for DMA configuration\n");
		return -EINVAL; /* Invalid resource type */
	}
	return 0;
}

static int ti_edma_deconfigure(const struct device *dev, uint32_t channel)
{

	LOG_DBG("Deconfiguring Resources of channel %d, device %s\n", channel, dev->name);

	struct ti_edma_data *dev_data = dev->data;
	EDMA_Handle edma_handle = dev_data->gEdmaHandle;

	uint32_t baseAddr, regionId, queNum;
	uint32_t dmaCh, tcc, param;
	int32_t testStatus = SystemP_SUCCESS;

	dmaCh = tcc = channel;

	/* Disable triggers and clear pending events/interrupts */
	ti_edma_stop(dev, channel);

	baseAddr = EDMA_getBaseAddr(edma_handle);
	regionId = EDMA_getRegionId(edma_handle);
	queNum = gEdmaConfig[dev_data->instNum].attrs->initPrms.queNum;

	testStatus = EDMA_getMappedPaRAM(baseAddr, channel, EDMA_CHANNEL_TYPE_DMA, &param);
	if (!testStatus) {
		LOG_ERR("Unable to get PARAM set linked to channel %d\n", channel);
		return -ECANCELED;
	}

	EDMA_freeChannelRegion(baseAddr, regionId, EDMA_CHANNEL_TYPE_DMA, dmaCh,
			       EDMA_TRIG_MODE_MANUAL, tcc, queNum);

	testStatus = EDMA_freeDmaChannel(edma_handle, &dmaCh);
	if (testStatus != SystemP_SUCCESS) {
		LOG_ERR("Channel deallocation failed for %s\n", dev->name);
		return -ECANCELED;
	}

	testStatus = EDMA_freeTcc(edma_handle, &tcc);
	if (testStatus != SystemP_SUCCESS) {
		LOG_ERR("TCC deallocation failed for %s\n", dev->name);
		return -ECANCELED;
	}

	testStatus = EDMA_freeParam(edma_handle, &param);
	if (testStatus != SystemP_SUCCESS) {
		LOG_ERR("PARAM deallocation failed for %s\n", dev->name);
		return -ECANCELED;
	}

	dev_data->channel_data[channel].chan_dir =
		DMA_CHANNEL_DIRECTION_MAX; /* Set back to default */
	/* Clear isr_data */
	dev_data->channel_data[channel].isr_data.dev = NULL;
	dev_data->channel_data[channel].isr_data.cb = NULL;
	dev_data->channel_data[channel].isr_data.args = NULL;
	dev_data->channel_data[channel].isr_data.channel = -1;

	atomic_clear_bit(dev_data->dma_ctx.atomic, channel);

	return 0;
}

static int ti_edma_init(const struct device *dev)
{

	struct ti_edma_config *dev_config = dev->config;
	struct ti_edma_data *dev_data = dev->data;

	uint8_t instNum = dev_data->instNum;

	gEdmaConfig[instNum].attrs = &(dev_config->gEdmaAttrs);
	gEdmaConfig[instNum].object = &(dev_data->gEdmaObject);

	for (int idx = 0; idx < dev_config->num_edma_resources; idx += 3) {
		edma_resource_validate_and_alloc(dev, idx);
	}

	dev_data->gEdmaHandle = EDMA_open(instNum, &(dev_config->gEdmaParams));

	if (dev_data->gEdmaHandle == NULL) {
		LOG_ERR("Configuration of %s failed\n", dev->name);
		return -ENOTSUP;
	}

	for (int chNum = 0; chNum < dev_data->dma_ctx.dma_channels; chNum++) {
		dev_data->channel_data[chNum].chan_dir = DMA_CHANNEL_DIRECTION_MAX;
	}

	dev_config->register_isr();

	return 0;
}

static int ti_edma_configure(const struct device *dev, uint32_t channel, struct dma_config *config)
{

	LOG_DBG("Staring DMA configuration for %s, channel %d\n", dev->name, channel);

	const struct ti_edma_config *dev_config = dev->config;
	struct ti_edma_data *dev_data = dev->data;
	EDMA_Handle edma_handle = dev_data->gEdmaHandle;

	if (channel > dev_data->dma_ctx.dma_channels) {
		LOG_ERR("Channel has to be a number from 0 to %d\n",
			dev_data->dma_ctx.dma_channels);
		return -EINVAL;
	}

	uint32_t baseAddr, regionId;
	int32_t testStatus = SystemP_SUCCESS;
	uint32_t dmaCh, tcc, param;
	uint32_t queNum = gEdmaConfig[dev_data->instNum].attrs->initPrms.queNum;
	EDMACCPaRAMEntry edmaParam;

	baseAddr = EDMA_getBaseAddr(edma_handle);
	regionId = EDMA_getRegionId(edma_handle);
	dmaCh = tcc = channel;

	/* If channel is already configured, deconfigure first */
	if (atomic_test_bit(dev_data->dma_ctx.atomic, channel)) {
		LOG_DBG("Deconfiguring and re-configuring channel %d of %s\n", channel, dev->name);

		testStatus = ti_edma_deconfigure(dev, channel);
		if (testStatus) { /* If dealloc failed */
			LOG_ERR("Failed to deconfigure channel %d of %s\n", channel, dev->name);
			return testStatus;
		}
		LOG_DBG("Deconfigured channel %d for %s\n", channel, dev->name);
	}

	testStatus = EDMA_allocDmaChannel(edma_handle, &dmaCh);
	atomic_set_bit(dev_data->dma_ctx.atomic, channel);
	if (testStatus != SystemP_SUCCESS) {
		LOG_ERR("DMA Channel allocation failed for %s\n", dev->name);
		return -ENOTSUP;
	}

	testStatus = EDMA_allocTcc(edma_handle, &tcc);
	if (testStatus != SystemP_SUCCESS) {
		LOG_ERR("TCC allocation failed for %s\n", dev->name);
		return -ENOTSUP;
	}

	param = EDMA_RESOURCE_ALLOC_ANY;
	testStatus = EDMA_allocParam(edma_handle, &param);
	if (testStatus != SystemP_SUCCESS) {
		LOG_ERR("PARAM allocation failed for %s\n", dev->name);
		return -ENOTSUP;
	}

	EDMA_configureChannelRegion(baseAddr, regionId, EDMA_CHANNEL_TYPE_DMA, dmaCh, tcc, param,
				    queNum);

	/* Program Param Set */
	EDMA_ccPaRAMEntry_init(&edmaParam);

	/* Map struct dma_config to PARAM */
	if (config->block_count > 1) {
		LOG_WRN("This EDMA driver supports only configuration of head block");
	}

	testStatus = populate_PARAM_set(dev, channel, config, config->head_block, &edmaParam);
	if (testStatus != 0) {
		return testStatus;
	}

	EDMA_setPaRAM(baseAddr, param, &edmaParam);

	/* Register ISR */
	if (config->complete_callback_en && (config->dma_callback != NULL)) {

		LOG_DBG("Registering DMA callback ISR...\n");
		irq_disable(dev_config->gEdmaAttrs.compIntrNumber);

		struct EDMA_ISR_Data *isr_data = &(dev_data->channel_data[channel].isr_data);

		isr_data->cb = config->dma_callback;
		isr_data->args = config->user_data;
		isr_data->dev = dev;
		isr_data->channel = channel;

		EDMA_enableEvtIntrRegion(baseAddr, dev_config->gEdmaAttrs.initPrms.regionId,
					 channel);

		irq_enable(dev_config->gEdmaAttrs.compIntrNumber);
		LOG_DBG("Interrupt registration done.\n");
	}

	return 0;
}

static int ti_edma_start(const struct device *dev, uint32_t channel)
{

	const struct ti_edma_data *dev_data = dev->data;
	EDMA_Handle edma_handle = dev_data->gEdmaHandle;

	if (channel > dev_data->dma_ctx.dma_channels) {
		LOG_ERR("Channel has to be a number from 0 to %d\n",
			dev_data->dma_ctx.dma_channels);
		return -EINVAL;
	}
	if (!atomic_test_bit(dev_data->dma_ctx.atomic, channel)) {
		LOG_ERR("Channel %d is not allocated", channel);
		return -EINVAL;
	}

	uint32_t baseAddr = EDMA_getBaseAddr(edma_handle);
	uint32_t regionId = EDMA_getRegionId(edma_handle);

	switch (dev_data->channel_data[channel].chan_dir) {
	case MEMORY_TO_MEMORY:
		/* Trigger single burst of m2m transfer */
		EDMA_clrEvtRegion(baseAddr, regionId, channel);
		EDMA_clrIntrRegion(baseAddr, regionId, channel);

		EDMA_enableTransferRegion(baseAddr, regionId, channel, EDMA_TRIG_MODE_MANUAL);
		break;

	case PERIPHERAL_TO_MEMORY:
		/* Clear any previous events or interrupts */
		EDMA_clrEvtRegion(baseAddr, regionId, channel);
		EDMA_clrIntrRegion(baseAddr, regionId, channel);

		EDMA_enableTransferRegion(baseAddr, regionId, channel, EDMA_TRIG_MODE_EVENT);
		break;

	case MEMORY_TO_PERIPHERAL:
		/* Clear any previous events or interrupts */
		EDMA_clrEvtRegion(baseAddr, regionId, channel);
		EDMA_clrIntrRegion(baseAddr, regionId, channel);

		EDMA_enableTransferRegion(baseAddr, regionId, channel, EDMA_TRIG_MODE_EVENT);
		EDMA_enableTransferRegion(baseAddr, regionId, channel, EDMA_TRIG_MODE_MANUAL);
		break;

	default:
		/* DO NOT Support dma_start if channel direction isn't specified */
		LOG_ERR("Unsupported Channel direction\n");
		return -ENOTSUP;
	}
	return 0;
}

static void ti_edma_get_status(const struct device *dev, uint32_t channel,
			       struct dma_status *status)
{

	struct ti_edma_data *dev_data = dev->data;
	EDMA_Handle edma_handle = dev_data->gEdmaHandle;

	uint32_t baseAddr = EDMA_getBaseAddr(edma_handle);
	uint32_t regionId = EDMA_getRegionId(edma_handle);

	bool transfer_complete = false;
	bool has_pending_events;
	uint32_t intr_status;

	if (channel < 32) {
		intr_status = EDMA_getIntrStatusRegion(baseAddr, regionId);
	} else {
		intr_status = EDMA_intrStatusHighGetRegion(baseAddr, regionId);
	}
	transfer_complete = (intr_status & (1U << (channel % 32))) != 0;

	/* Check if events are still pending */
	has_pending_events = (bool)EDMA_readEventStatusRegion(baseAddr, channel);

	switch (dev_data->channel_data[channel].chan_dir) {
	case MEMORY_TO_MEMORY:
		status->busy = !transfer_complete;
		break;

	case PERIPHERAL_TO_MEMORY:
	case MEMORY_TO_PERIPHERAL:
		status->busy = !transfer_complete && has_pending_events;
		break;

	default:
		return -ENOTSUP;
	}

	status->dir = dev_data->channel_data[channel].chan_dir;

	status->pending_length = get_pending_length(dev, channel);

	/* Unsupported data */
	status->free = 0;
	status->total_copied = 0;
	status->write_position = 0;
	status->read_position = 0;
}

static int ti_edma_stop(const struct device *dev, uint32_t channel)
{
	struct ti_edma_data *dev_data = dev->data;
	EDMA_Handle edma_handle = dev_data->gEdmaHandle;

	/* Validate channel */
	if (channel >= dev_data->dma_ctx.dma_channels) {
		LOG_ERR("Channel has to be a number from 0 to %d",
			dev_data->dma_ctx.dma_channels - 1);
		return -EINVAL;
	}
	/* Check if channel is allocated */
	if (!atomic_test_bit(dev_data->dma_ctx.atomic, channel)) {
		LOG_ERR("Channel %d is not allocated", channel);
		return -EINVAL;
	}

	/* Get required EDMA parameters */
	uint32_t baseAddr = EDMA_getBaseAddr(edma_handle);
	uint32_t regionId = EDMA_getRegionId(edma_handle);

	switch (dev_data->channel_data[channel].chan_dir) {
	case MEMORY_TO_MEMORY:
		/* Disable manual trigger mode */
		EDMA_disableTransferRegion(baseAddr, regionId, channel, EDMA_TRIG_MODE_MANUAL);
		break;

	case PERIPHERAL_TO_MEMORY:
	case MEMORY_TO_PERIPHERAL:
		/* Disable event trigger mode */
		EDMA_disableTransferRegion(baseAddr, regionId, channel, EDMA_TRIG_MODE_EVENT);
		break;

	default:
		LOG_ERR("Unsupported channel direction");
		return -ENOTSUP;
	}
	/* Clear any pending events and interrupts */
	EDMA_clrEvtRegion(baseAddr, regionId, channel);
	EDMA_clrIntrRegion(baseAddr, regionId, channel);

	/* Clear any error status */
	uint32_t queNum = gEdmaConfig[dev_data->instNum].attrs->initPrms.queNum;

	LOG_DBG("Stopped DMA transfer on channel %d", channel);
	return 0;
}

static DEVICE_API(dma, ti_edma_driver_api) = {
	.config = ti_edma_configure,
	.start = ti_edma_start,
	.chan_release = ti_edma_deconfigure,
	.get_status = ti_edma_get_status,
	.stop = ti_edma_stop,
};

/* Helper macros to get a specific resource value from edma-resources */
#define EDMA_RESOURCE_GET(elem_idx, inst) DT_INST_PROP_BY_IDX(inst, edma_resources, elem_idx)
#define EDMA_RESOURCE_NUM(i, _)           i
#define NUM_EDMA_RESOURCES(inst)          DT_INST_PROP_LEN(inst, edma_resources)

#define DEFINE_TI_EDMA_ISR_FUNC(inst)                                                              \
	static void ti_edma_register_isr_##inst(void)                                              \
	{                                                                                          \
		irq_disable(DT_INST_IRQ_BY_NAME(inst, comp_intr, irq));                            \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, comp_intr, irq),                             \
			    DT_INST_IRQ_BY_NAME(inst, comp_intr, priority), ti_edma_masterISR,     \
			    DEVICE_DT_INST_GET(inst),                                              \
			    DT_INST_IRQ_BY_NAME(inst, comp_intr, flags));                          \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, comp_intr, irq));                             \
	}

#define TI_EDMA_INIT(inst)                                                                         \
	BUILD_ASSERT(DT_INST_PROP(inst, dma_channels) >= 0,                                        \
		     "Number of DMA Channels cannot be negative");                                 \
	BUILD_ASSERT(DT_INST_PROP(inst, edma_params) >= 0,                                         \
		     "Number of EDMA PARAM sets cannot be negative");                              \
	BUILD_ASSERT(DT_INST_PROP(inst, edma_regions) >= 0,                                        \
		     "Number of EDMA Regions cannot be negative");                                 \
	BUILD_ASSERT(DT_INST_PROP(inst, edma_queues) >= 0,                                         \
		     "Number of Event Queues cannot be negative");                                 \
	BUILD_ASSERT(DT_INST_PROP(inst, region_id) >= 0 &&                                         \
			     DT_INST_PROP(inst, region_id) < DT_INST_PROP(inst, edma_regions),     \
		     "Invalid region ID property\n");                                              \
	BUILD_ASSERT(DT_INST_PROP(inst, queue_number) >= 0 &&                                      \
			     DT_INST_PROP(inst, queue_number) < DT_INST_PROP(inst, edma_queues),   \
		     "Invalid queue number property\n");                                           \
                                                                                                   \
	BUILD_ASSERT((NUM_EDMA_RESOURCES(inst) % 3 == 0),                                          \
		     "EDMA Resources need to be passed in groups of 3: resource_type, "            \
		     "start_index, end_index\n");                                                  \
	uint16_t edma_resources_##inst[NUM_EDMA_RESOURCES(inst)] = {                               \
		FOR_EACH_FIXED_ARG(EDMA_RESOURCE_GET, (,), inst,                               \
				   LISTIFY(NUM_EDMA_RESOURCES(inst), EDMA_RESOURCE_NUM, (,)))};    \
                                                                                                   \
	DEFINE_TI_EDMA_ISR_FUNC(inst)                                                              \
	static struct ti_edma_config edma_config_##inst = {                                        \
                                                                                                   \
		.gEdmaAttrs =                                                                      \
			{                                                                          \
				.baseAddr = DT_INST_REG_ADDR(inst),                                \
				.compIntrNumber = DT_INST_IRQ_BY_NAME(inst, comp_intr, irq),       \
				.intrPriority = DT_INST_IRQ_BY_NAME(inst, comp_intr, priority),    \
				.intrAggEnableAddr =                                               \
					DT_INST_REG_ADDR(inst) + CSL_MSS_CTRL_TPCC0_INTAGG_MASK,   \
				.intrAggEnableMask =                                               \
					0x1FF & (~(2U << DT_INST_PROP(inst, region_id))),          \
				.intrAggStatusAddr =                                               \
					DT_INST_REG_ADDR(inst) + CSL_MSS_CTRL_TPCC0_INTAGG_STATUS, \
				.intrAggClearMask = (2U << DT_INST_PROP(inst, region_id)),         \
                                                                                                   \
				.initPrms =                                                        \
					{                                                          \
						.regionId = DT_INST_PROP(inst, region_id),         \
						.queNum = DT_INST_PROP(inst, queue_number),        \
						.initParamSet = TRUE,                              \
					},                                                         \
			},                                                                         \
		.max_num_params = DT_INST_PROP(inst, edma_params),                                 \
		.edma_resources = &edma_resources_##inst,                                          \
		.num_edma_resources = NUM_EDMA_RESOURCES(inst),                                    \
		.register_isr = &ti_edma_register_isr_##inst,                                      \
	};                                                                                         \
                                                                                                   \
	static ATOMIC_DEFINE(dma_channels_atomic_##inst, DT_INST_PROP(inst, dma_channels));        \
	static struct EDMA_Channel dma_channel_data_##inst[DT_INST_PROP(inst, dma_channels)];      \
                                                                                                   \
	static struct ti_edma_data edma_data_##inst = {                                            \
		.gEdmaHandle = NULL,                                                               \
		.dma_ctx.magic = DMA_MAGIC,                                                        \
		.dma_ctx.dma_channels = DT_INST_PROP(inst, dma_channels),                          \
		.dma_ctx.atomic = dma_channels_atomic_##inst,                                      \
		.channel_data = dma_channel_data_##inst,                                           \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, ti_edma_init, NULL, &edma_data_##inst, &edma_config_##inst,    \
			      PRE_KERNEL_1, CONFIG_DMA_INIT_PRIORITY, &ti_edma_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TI_EDMA_INIT);
