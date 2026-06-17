/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_unicomm_spi

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_ti_unicomm, CONFIG_SPI_LOG_LEVEL);

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "spi_context.h"

/*
 * UNICOMM Register Offsets
 */

#define UNICOMM_POWER_EN  0x00000800U
#define UNICOMM_RESET_CTL 0x00000804U
#define UNICOMM_CLOCK_CFG 0x00000808U
#define UNICOMM_STATUS    0x00000814U
#define UNICOMM_MODE      0x00001100U

/*
 * Peripheral Configuration Values
 */

#define PWREN_ENABLE  0x00000001U
#define PWREN_DISABLE 0x00000000U
#define PWREN_KEY     0x26000000U
#define IPMODE_SPI    0x00000001U

#define RESET_CTL_KEY_UNLOCK       0xB1000000U
#define RESET_CTL_STICKY_BIT_CLEAR 0x00000002U
#define RESET_CTL_ASSERT_RESET     0x00000001U

/* UNICOMM SPI Regs */
#define UNICOMM_SPI_CLKDIV 0x0
#define UNICOMM_SPI_CLKSEL 0x8
#define UNICOMM_SPI_CTL0   0x100 /* frame format, data size */
#define UNICOMM_SPI_CTL1   0x14C /* parity, bit order, mode */
#define UNICOMM_SPI_CLKCTL 0x110

#define UNICOMMSPI_CTL0_DSS_MASK   ((uint32_t)0x0000001FU) /* !< Data Size Select. */
#define UNICOMMSPI_CTL0_FRF_MASK   ((uint32_t)0x00000060U) /* !< Frame format Select */
#define UNICOMMSPI_CTL0_SPO_MASK   ((uint32_t)0x00000100U) /* !< CLKOUT polarity */
#define UNICOMMSPI_CTL0_SPH_MASK   ((uint32_t)0x00000200U) /* !< CLKOUT phase */
#define UNICOMMSPI_CTL0_CSCLR_MASK ((uint32_t)0x00004000U) /* !< Clear on CS inactive */

#define UNICOMMSPI_CTL1_LBM_MASK    ((uint32_t)0x00000002U) /* !< Loop back mode enable */
#define UNICOMMSPI_CTL1_CP_MASK     ((uint32_t)0x00000004U) /* !< Controller or peripheral mode */
#define UNICOMMSPI_CTL1_POD_MASK    ((uint32_t)0x00000008U) /* !< Peripheral-mode: Data output */
#define UNICOMMSPI_CTL1_MSB_MASK    ((uint32_t)0x00000010U) /* !< MSB first select */
#define UNICOMMSPI_CTL1_PREN_MASK   ((uint32_t)0x00000020U) /* !< Parity Receive Enable */
#define UNICOMMSPI_CTL1_PES_MASK    ((uint32_t)0x00000040U) /* !< Even Parity Select */
#define UNICOMMSPI_CTL1_ENABLE_MASK ((uint32_t)0x00000001U) /* !< SPI enable */
#define UNICOMMSPI_CTL1_PTEN_MASK   ((uint32_t)0x00000100U) /* !< Parity Transmit Enable */
#define UNICOMMSPI_CTL1_SUSPEND_MASK                                                               \
	((uint32_t)0x00000200U) /* !< Suspend external communication                               \
				 */

#define UNICOMMSPI_CLKCTL_SCR_MASK ((uint32_t)0x000003FFU) /* !< Serial clock divider */

#define UNICOMM_SPI_STAT   0x108
#define UNICOMM_SPI_TXDATA 0x120
#define UNICOMM_SPI_RXDATA 0x124

#define UNICOMMSPI_STAT_TXFF_MASK ((uint32_t)0x00000040U) /* TX FIFO full */
#define UNICOMMSPI_STAT_RXFE_MASK ((uint32_t)0x00000004U) /* RX FIFO empty */
#define UNICOMMSPI_STAT_BUSY_MASK ((uint32_t)0x00000100U) /* SPI busy */

/* Configuration Values */
#define SPI_CLKDIV_DIVIDE_BY_1   0
#define SPI_CLKSEL_BUSCLK_ENABLE 0x00000008U

#define SPI_CTL1_ENABLE  0x00000001U
#define SPI_CTL1_DISABLE 0x00000000U

#define SPI_CTL0_FRF_MOTOROLA_3WIRE 0x00000000U
#define SPI_CTL0_FRF_MOTOROLA_4WIRE 0x00000020U
#define SPI_CTL0_FRF_TI             0x00000040U
#define SPI_CTL0_SPO_CPOL           0x00000100U
#define SPI_CTL0_SPH_CPHA           0x00000200U
#define SPI_CTL0_DSS_8BIT           0x00000007U

/* Helper macros */
#define UPDATE_REG(reg_offset, value, mask)                                                        \
	{                                                                                          \
		uint32_t tmp = sys_read32(reg_offset);                                             \
		tmp = tmp & ~(mask);                                                               \
		sys_write32(tmp | ((value) & (mask)), reg_offset);                                 \
	}

struct spi_ti_unicomm_config {
	const struct pinctrl_dev_config *pcfg;

	uint32_t unicomm_inst_base;
	uint32_t unicomm_spi_base;

	uint8_t clkdiv;     /* Clock divide ratio. Register value: 0=div1, 1=div2, ... 7=div8 */
	uint32_t busclk_hz; /* BUSCLK input frequency in Hz (before clkdiv) */

	uint32_t tx_fifo_threshold;
	uint32_t rx_fifo_threshold;

	void (*irq_config_func)(const struct device *dev);
};

struct spi_ti_unicomm_data {
	struct spi_context ctx;
};

/*
 * Helper functions
 */

/* Reset unicomm instance */
static inline void unicomm_reset(uint32_t base)
{
	sys_write32(RESET_CTL_KEY_UNLOCK | RESET_CTL_STICKY_BIT_CLEAR | RESET_CTL_ASSERT_RESET,
		    base + UNICOMM_RESET_CTL);
}

/* Enable power for UNICOMM instance */
static inline void unicomm_enable_power(uint32_t base)
{
	sys_write32(PWREN_KEY | PWREN_ENABLE, base + UNICOMM_POWER_EN);
	k_sleep(K_CYC(20));
}

static int spi_ti_unicomm_configure(const struct device *dev, const struct spi_config *config)
{
	const struct spi_ti_unicomm_config *cfg = dev->config;
	struct spi_ti_unicomm_data *data = dev->data;

	uint32_t frame_format;

	uint32_t ctl0 = 0;
	uint32_t ctl1 = 0;

	if (spi_context_configured(&data->ctx, config)) {
		/* Nothing to do */
		return 0;
	}

	/* Only master mode is supported */
	if (SPI_OP_MODE_GET(config->operation) != SPI_OP_MODE_MASTER) {
		return -ENOTSUP;
	}

	/* Only single line mode is supported */
	if (IS_ENABLED(CONFIG_SPI_EXTENDED_MODES) &&
	    (config->operation & SPI_LINES_MASK) != SPI_LINES_SINGLE) {
		return -EINVAL;
	}

	/* Set Frame Format*/
	if (config->operation & SPI_FRAME_FORMAT_TI) {
		ctl0 = SPI_CTL0_FRF_TI;

		/* Half duplex mode is not supported by TI Frame Format */
		if (config->operation & SPI_HALF_DUPLEX) {
			return -ENOTSUP;
		}
	} else {
		ctl0 = SPI_CTL0_FRF_MOTOROLA_4WIRE;

		if (config->operation & SPI_HALF_DUPLEX) {
			ctl0 = SPI_CTL0_FRF_MOTOROLA_3WIRE;
		}

		if (config->operation & SPI_MODE_CPOL) {
			ctl0 |= SPI_CTL0_SPO_CPOL;
		}

		if (config->operation & SPI_MODE_CPHA) {
			ctl0 |= SPI_CTL0_SPH_CPHA;
		}
	}

	/* Data size */
	if (SPI_WORD_SIZE_GET(config->operation) != 8) {
		return -ENOTSUP;
	}
	ctl0 |= SPI_CTL0_DSS_8BIT;

	/* MSB bit = 1 means LSB first; leave clear for MSB first */
	if (!(config->operation & SPI_TRANSFER_MSB)) {
		ctl1 |= UNICOMMSPI_CTL1_MSB_MASK;
	}

	if (config->operation & SPI_MODE_LOOP) {
		ctl1 |= UNICOMMSPI_CTL1_LBM_MASK;
	}

	/* Set controller mode */
	ctl1 |= UNICOMMSPI_CTL1_CP_MASK;

	/* Disable peripheral, apply settings and enable again */
	unicomm_reset(cfg->unicomm_inst_base);
	unicomm_enable_power(cfg->unicomm_inst_base);

	/* Set instance mode to SPI */
	sys_write32(IPMODE_SPI, cfg->unicomm_inst_base + UNICOMM_MODE);

	/* Configure clock divide ratio and select BUSCLK as clock source */
	sys_write32(cfg->clkdiv, cfg->unicomm_spi_base + UNICOMM_SPI_CLKDIV);
	sys_write32(SPI_CLKSEL_BUSCLK_ENABLE, cfg->unicomm_spi_base + UNICOMM_SPI_CLKSEL);

	/* Set CTL0 and CTL1 */
	UPDATE_REG(cfg->unicomm_spi_base + UNICOMM_SPI_CTL0, ctl0,
		   UNICOMMSPI_CTL0_FRF_MASK | UNICOMMSPI_CTL0_SPO_MASK | UNICOMMSPI_CTL0_SPH_MASK |
			   UNICOMMSPI_CTL0_DSS_MASK);

	UPDATE_REG(cfg->unicomm_spi_base + UNICOMM_SPI_CTL1, ctl1,
		   UNICOMMSPI_CTL1_PES_MASK | UNICOMMSPI_CTL1_PREN_MASK |
			   UNICOMMSPI_CTL1_PTEN_MASK | UNICOMMSPI_CTL1_MSB_MASK |
			   UNICOMMSPI_CTL1_CP_MASK | UNICOMMSPI_CTL1_LBM_MASK);

	/* Set SPI bitrate Serial Clock Divider (SCR) */
	UPDATE_REG(cfg->unicomm_spi_base + UNICOMM_SPI_CLKCTL, 99, UNICOMMSPI_CLKCTL_SCR_MASK);

	/* Enable SPI */
	UPDATE_REG(cfg->unicomm_spi_base + UNICOMM_SPI_CTL1, SPI_CTL1_ENABLE,
		   UNICOMMSPI_CTL1_ENABLE_MASK);

	/* Cache SPI config for reuse, required by spi_context owner */
	data->ctx.config = config;

	return 0;
}

static int spi_ti_unicomm_init(const struct device *dev)
{
	const struct spi_ti_unicomm_config *cfg = dev->config;
	struct spi_ti_unicomm_data *data = dev->data;
	int ret = 0;

	unicomm_reset(cfg->unicomm_inst_base);
	unicomm_enable_power(cfg->unicomm_inst_base);

	/* Set instance mode to SPI */
	sys_write32(IPMODE_SPI, cfg->unicomm_inst_base + UNICOMM_MODE);

	/* Configure clock divide ratio and select BUSCLK as clock source */
	sys_write32(cfg->clkdiv, cfg->unicomm_spi_base + UNICOMM_SPI_CLKDIV);
	sys_write32(SPI_CLKSEL_BUSCLK_ENABLE, cfg->unicomm_spi_base + UNICOMM_SPI_CLKSEL);

	/*
	 * CTL0: Motorola 4-wire (FRF=0x20), CPOL=0/CPHA=1 (SPH=0x200), 8-bit (DSS=0x7)
	 * CTL1: controller mode (CP=0x4), MSB first (MSB bit=0, acts as LSB-first enable), no
	 * parity
	 */
	UPDATE_REG(cfg->unicomm_spi_base + UNICOMM_SPI_CTL0,
		   (uint32_t)0x00000020U | (uint32_t)0x00000200U | (uint32_t)0x00000007U,
		   UNICOMMSPI_CTL0_FRF_MASK | UNICOMMSPI_CTL0_SPO_MASK | UNICOMMSPI_CTL0_SPH_MASK |
			   UNICOMMSPI_CTL0_DSS_MASK);

	UPDATE_REG(cfg->unicomm_spi_base + UNICOMM_SPI_CTL1, UNICOMMSPI_CTL1_CP_MASK,
		   UNICOMMSPI_CTL1_PES_MASK | UNICOMMSPI_CTL1_PREN_MASK |
			   UNICOMMSPI_CTL1_PTEN_MASK | UNICOMMSPI_CTL1_MSB_MASK |
			   UNICOMMSPI_CTL1_CP_MASK | UNICOMMSPI_CTL1_LBM_MASK);

	/* Set SPI bitrate Serial Clock Divider (SCR) */
	UPDATE_REG(cfg->unicomm_spi_base + UNICOMM_SPI_CLKCTL, 99, UNICOMMSPI_CLKCTL_SCR_MASK);

	/* Enable SPI */
	UPDATE_REG(cfg->unicomm_spi_base + UNICOMM_SPI_CTL1, SPI_CTL1_ENABLE,
		   UNICOMMSPI_CTL1_ENABLE_MASK);

	/* Apply pinctrl config */
	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

static int spi_ti_unicomm_transceive(const struct device *dev, const struct spi_config *config,
				     const struct spi_buf_set *tx_bufs,
				     const struct spi_buf_set *rx_bufs)
{
	const struct spi_ti_unicomm_config *cfg = dev->config;
	struct spi_ti_unicomm_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	uint32_t base = cfg->unicomm_spi_base;
	int ret = 0;

	spi_context_lock(ctx, false, NULL, NULL, config);

	ret = spi_ti_unicomm_configure(dev, config);
	if (ret != 0) {
		spi_context_release(&data->ctx, ret);
		return ret;
	}

	spi_context_buffers_setup(ctx, tx_bufs, rx_bufs, 1);

	while (spi_context_tx_buf_on(ctx) || spi_context_rx_buf_on(ctx)) {
		uint8_t tx_byte = spi_context_tx_buf_on(ctx) ? *(uint8_t *)ctx->tx_buf : 0;

		/* Wait TX FIFO not full */
		while (sys_read32(base + UNICOMM_SPI_STAT) & UNICOMMSPI_STAT_TXFF_MASK) {
		}

		sys_write32(tx_byte, base + UNICOMM_SPI_TXDATA);

		/* Wait RX FIFO not empty */
		uint32_t timeout = 100000U;

		while ((sys_read32(base + UNICOMM_SPI_STAT) & UNICOMMSPI_STAT_RXFE_MASK) &&
		       --timeout) {
		}
		if (!timeout) {
			ret = -ETIMEDOUT;
			goto done;
		}

		uint8_t rx_byte = (uint8_t)sys_read32(base + UNICOMM_SPI_RXDATA);

		if (spi_context_rx_buf_on(ctx)) {
			*(uint8_t *)ctx->rx_buf = rx_byte;
		}

		spi_context_update_tx(ctx, 1, 1);
		spi_context_update_rx(ctx, 1, 1);
	}

	/* Wait for SPI to finish */
	while (sys_read32(base + UNICOMM_SPI_STAT) & UNICOMMSPI_STAT_BUSY_MASK) {
	}

done:
	spi_context_complete(ctx, dev, ret);
	spi_context_release(ctx, ret);
	return ret;
}

static DEVICE_API(spi, spi_ti_unicomm_api) = {.transceive = spi_ti_unicomm_transceive};

#define SPI_TI_UNICOMM_INIT(index)                                                                 \
	PINCTRL_DT_INST_DEFINE(index);                                                             \
                                                                                                   \
	static const struct spi_ti_unicomm_config spi_config_##index = {                           \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(index),                                     \
		.unicomm_inst_base =                                                               \
			(uint32_t)(DT_REG_ADDR_BY_IDX(DT_PARENT(DT_DRV_INST(index)), 0)),          \
		.unicomm_spi_base = (uint32_t)(DT_INST_REG_ADDR(index)),                           \
		.clkdiv = SPI_CLKDIV_DIVIDE_BY_1,                                                  \
		.busclk_hz = DT_INST_PROP_OR(index, unicomm_clock_freq, 100000000U),               \
	};                                                                                         \
                                                                                                   \
	static struct spi_ti_unicomm_data spi_data_##index = {                                     \
		SPI_CONTEXT_INIT_LOCK(spi_data_##index, ctx),                                      \
		SPI_CONTEXT_INIT_SYNC(spi_data_##index, ctx),                                      \
	};                                                                                         \
                                                                                                   \
	SPI_DEVICE_DT_INST_DEFINE(index, spi_ti_unicomm_init, NULL, &spi_data_##index,             \
				  &spi_config_##index, POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,      \
				  &spi_ti_unicomm_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_TI_UNICOMM_INIT)
