/* Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __TI_EDMA_HEADERS__
#define __TI_EDMA_HEADERS__

#include <zephyr/drivers/dma.h>
#include <zephyr/sys/util_macro.h>

#include <edma.h>
#include <hw_include/am261x/cslr_soc_defines.h>
#include <hw_include/hw_types.h>

enum EDMA_Resource_Type {
	CORE_DMA_CHANNEL = 0,
	CORE_PARAM = 1,
};

struct EDMA_Resource {
	enum EDMA_Resource_Type resource_type;
	uint16_t start;
	uint16_t end;
};

struct EDMA_ISR_Data {
	struct device *dev; /* Contains pointer to EDMA device */
	dma_callback_t cb;  /* ISR Callback function */
	void *args;         /* User-defined args */
	uint32_t channel;
};

struct EDMA_Channel {
	enum dma_channel_direction chan_dir;
	struct EDMA_ISR_Data isr_data;
};

#endif /* __TI_EDMA_HEADERS__ */
