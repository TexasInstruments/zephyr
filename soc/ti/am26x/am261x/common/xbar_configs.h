/*
 * Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __AM26X_XBAR_ENABLEMENT_H_
#define __AM26X_XBAR_ENABLEMENT_H_

#include <drivers/soc/am261x/soc_xbar.h>

#ifdef CONFIG_DMA
/* Connect DMA XBARs for peripherals whose statuses are OK */
void configure_dma_xbars(void);
#endif /* CONFIG_DMA */

#endif /* __AM26X_XBAR_ENABLEMENT_H_ */
