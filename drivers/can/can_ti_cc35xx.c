/*
 * Copyright (c) 2026 Conclusive Engineering Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "can_ti_cc35xx.h"

#include <zephyr/drivers/can.h>
#include "can_mcan.h"
#include <zephyr/drivers/clock_control.h>
#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/pm/cc35xx_pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>

#define DT_DRV_COMPAT ti_cc35xx_can

LOG_MODULE_REGISTER(can_ti_cc35xx, CONFIG_CAN_LOG_LEVEL);

#define CLK_NODE DT_NODELABEL(canclk)
#define CLK_FREQ DT_PROP(CLK_NODE, clock_frequency)

/* Registers lost while the core clock is gated, restored in this order on resume */
static const uint16_t context_regs[] = {
	CAN_MCAN_CCCR,  CAN_MCAN_NBTP,  CAN_MCAN_DBTP,  CAN_MCAN_TEST,   CAN_MCAN_TSCC,
	CAN_MCAN_TOCC,  CAN_MCAN_TDCR,  CAN_MCAN_GFC,   CAN_MCAN_SIDFC,  CAN_MCAN_XIDFC,
	CAN_MCAN_XIDAM, CAN_MCAN_RXF0C, CAN_MCAN_RXF1C, CAN_MCAN_RXBC,   CAN_MCAN_RXESC,
	CAN_MCAN_TXBC,  CAN_MCAN_TXESC, CAN_MCAN_TXEFC, CAN_MCAN_TXBTIE, CAN_MCAN_TXBCIE,
	CAN_MCAN_ILS,   CAN_MCAN_IE,    CAN_MCAN_ILE,
};

struct can_ti_cc35xx_data {
	uint32_t regs[ARRAY_SIZE(context_regs)];
	/* Standard and extended filter elements, saved from message RAM */
	uint32_t *filters;
	bool pm_locked;
};

static int can_ti_cc35xx_read_reg(const struct device *dev, uint16_t reg, uint32_t *val)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;

	return can_mcan_sys_read_reg(config->mcan, reg, val);
}

static int can_ti_cc35xx_write_reg(const struct device *dev, uint16_t reg, uint32_t val)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;

	return can_mcan_sys_write_reg(config->mcan, reg, val);
}

static int can_ti_cc35xx_read_mram(const struct device *dev, uint16_t offset, void *dst, size_t len)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;

	return can_mcan_sys_read_mram(config->mram, offset, dst, len);
}

static int can_ti_cc35xx_write_mram(const struct device *dev, uint16_t offset, const void *src,
				    size_t len)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;

	return can_mcan_sys_write_mram(config->mram, offset, src, len);
}

static int can_ti_cc35xx_clear_mram(const struct device *dev, uint16_t offset, size_t len)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;

	return can_mcan_sys_clear_mram(config->mram, offset, len);
}

static int can_ti_cc35xx_get_core_clock(const struct device *dev, uint32_t *rate)
{
	*rate = CLK_FREQ;
	return 0;
}

static void can_ti_cc35xx_isr_line0(const struct device *dev)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;

	can_mcan_line_0_isr(dev);

	/* Clear interrupt flag in subsystem */
	sys_write32(CAN_TI_CC35XX_ICLR0_INTL0, config->mcan + CAN_TI_CC35XX_ICLR0);

	/* End of interrupt */
	sys_write32(CAN_TI_CC35XX_SSEOI_INTL0, config->mcan + CAN_TI_CC35XX_SSEOI);
}

static void can_ti_cc35xx_isr_line1(const struct device *dev)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;

	can_mcan_line_1_isr(dev);

	/* Clear interrupt flag in subsystem */
	sys_write32(CAN_TI_CC35XX_ICLR1_INTL1, config->mcan + CAN_TI_CC35XX_ICLR1);
	/* Signal end of interrupt */
	sys_write32(CAN_TI_CC35XX_SSEOI_INTL1, config->mcan + CAN_TI_CC35XX_SSEOI);
}

static inline void enable_interrupt_lines(const struct device *dev)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;

	sys_write32(CAN_TI_CC35XX_IMASK0_INTL0, config->mcan + CAN_TI_CC35XX_IMASK0);
	sys_write32(CAN_TI_CC35XX_IMASK1_INTL1, config->mcan + CAN_TI_CC35XX_IMASK1);
}

static int enable_core_clock(const struct device *dev)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;
	uint32_t start;
	const uint32_t timeout_ms = CAN_TI_CC35XX_CLKCFG_TIMEOUT_MS;
	mm_reg_t clkcfg_reg;

	sys_write32(0, config->mcan + CAN_TI_CC35XX_CLKCFG);

	k_busy_wait(1);

	sys_write32(CAN_TI_CC35XX_CLKCFG_CLKEN | CAN_TI_CC35XX_CLKCFG_RAMEN |
			    CAN_TI_CC35XX_CLKCFG_CLKSEL_HOST_DIV2_CLK,
		    config->mcan + CAN_TI_CC35XX_CLKCFG);

	start = k_uptime_get_32();
	clkcfg_reg = config->mcan + CAN_TI_CC35XX_CLKCFG;

	while (!(sys_read32(clkcfg_reg) & CAN_TI_CC35XX_CLKCFG_CLKEN)) {
		if (k_uptime_get_32() - start > timeout_ms) {
			return -ETIMEDOUT;
		}
		k_busy_wait(10);
	}

	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int can_ti_cc35xx_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;
	struct can_mcan_data *mcan_data = dev->data;
	struct can_ti_cc35xx_data *data = mcan_data->custom;
	size_t filters_size = mcan_config->mram_offsets[CAN_MCAN_MRAM_CFG_RX_FIFO0];
	uint32_t cccr;
	size_t i;
	int err;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		if (pm_device_is_busy(dev) || mcan_data->common.started) {
			return -EBUSY;
		}

		for (i = 0; i < ARRAY_SIZE(context_regs); i++) {
			can_ti_cc35xx_read_reg(dev, context_regs[i], &data->regs[i]);
		}
		can_ti_cc35xx_read_mram(dev, 0, data->filters, filters_size);
		can_ti_cc35xx_write_reg(dev, CAN_MCAN_ILE, 0);

		return cc35xx_pm_resource_put(CC35XX_PM_RESOURCE_DCAN);
	case PM_DEVICE_ACTION_RESUME:
		err = cc35xx_pm_resource_get(CC35XX_PM_RESOURCE_DCAN);
		if (err != 0) {
			return err;
		}

		err = enable_core_clock(dev);
		if (err != 0) {
			goto put;
		}

		if (!WAIT_FOR((sys_read32(config->mcan + CAN_TI_CC35XX_SSSTA) &
			       CAN_TI_CC35XX_SSSTA_MEMINITSTA) != 0U,
			      CAN_TI_CC35XX_CLKCFG_TIMEOUT_MS * USEC_PER_MSEC, k_busy_wait(1))) {
			err = -ETIMEDOUT;
			goto put;
		}

		/* Enter init mode without the sleeping can_mcan helpers */
		cccr = sys_read32(config->mcan + CAN_MCAN_CCCR);
		cccr = (cccr & ~CAN_MCAN_CCCR_CSR) | CAN_MCAN_CCCR_INIT;
		sys_write32(cccr, config->mcan + CAN_MCAN_CCCR);
		if (!WAIT_FOR((sys_read32(config->mcan + CAN_MCAN_CCCR) &
			       (CAN_MCAN_CCCR_INIT | CAN_MCAN_CCCR_CSA)) == CAN_MCAN_CCCR_INIT,
			      CAN_TI_CC35XX_CLKCFG_TIMEOUT_MS * USEC_PER_MSEC, k_busy_wait(1))) {
			err = -ETIMEDOUT;
			goto put;
		}
		sys_write32(cccr | CAN_MCAN_CCCR_CCE, config->mcan + CAN_MCAN_CCCR);

		can_ti_cc35xx_write_mram(dev, 0, data->filters, filters_size);
		for (i = 0; i < ARRAY_SIZE(context_regs); i++) {
			can_ti_cc35xx_write_reg(dev, context_regs[i], data->regs[i]);
		}
		enable_interrupt_lines(dev);

		return 0;
	default:
		return -ENOTSUP;
	}

put:
	(void)cc35xx_pm_resource_put(CC35XX_PM_RESOURCE_DCAN);

	return err;
}
#endif /* CONFIG_PM_DEVICE */

/*
 * The controller is busy while started or while RX filters are added (see can_mcan.c).
 * Keep the system out of standby for as long as it is busy.
 */
static void can_ti_cc35xx_sync_pm_lock(const struct device *dev)
{
	struct can_mcan_data *mcan_data = dev->data;
	struct can_ti_cc35xx_data *data = mcan_data->custom;
	bool busy = pm_device_is_busy(dev) || mcan_data->common.started;

	if (busy && !data->pm_locked) {
		pm_policy_state_lock_get(PM_STATE_RUNTIME_IDLE, PM_ALL_SUBSTATES);
		pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		data->pm_locked = true;
	} else if (!busy && data->pm_locked) {
		data->pm_locked = false;
		pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		pm_policy_state_lock_put(PM_STATE_RUNTIME_IDLE, PM_ALL_SUBSTATES);
	}
}

static int can_ti_cc35xx_start(const struct device *dev)
{
	int err = can_mcan_start(dev);

	can_ti_cc35xx_sync_pm_lock(dev);

	return err;
}

static int can_ti_cc35xx_stop(const struct device *dev)
{
	int err = can_mcan_stop(dev);

	can_ti_cc35xx_sync_pm_lock(dev);

	return err;
}

static int can_ti_cc35xx_add_rx_filter(const struct device *dev, can_rx_callback_t callback,
				       void *user_data, const struct can_filter *filter)
{
	int filter_id = can_mcan_add_rx_filter(dev, callback, user_data, filter);

	can_ti_cc35xx_sync_pm_lock(dev);

	return filter_id;
}

static void can_ti_cc35xx_remove_rx_filter(const struct device *dev, int filter_id)
{
	can_mcan_remove_rx_filter(dev, filter_id);
	can_ti_cc35xx_sync_pm_lock(dev);
}

static int can_ti_cc35xx_init(const struct device *dev)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;
	int err;

	err = cc35xx_pm_resource_get(CC35XX_PM_RESOURCE_DCAN);
	if (err != 0) {
		LOG_ERR("Failed to enable clock (%d)", err);
		return err;
	}

	err = enable_core_clock(dev);
	if (err != 0) {
		LOG_ERR("Failed to enable core clock (%d)", err);
		return err;
	}

	err = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (err != 0) {
		LOG_ERR("Failed to apply pinctrl state (%d)", err);
		return err;
	}

	err = can_mcan_configure_mram(dev, config->mrba, config->mram);
	if (err != 0) {
		LOG_ERR("Failed to configure mcan message ram (%d)", err);
		return err;
	}

	err = can_mcan_init(dev);
	if (err != 0) {
		LOG_ERR("Failed to initialize can_mcan core (%d)", err);
		return err;
	}

	config->irq_configure();
	enable_interrupt_lines(dev);

	return 0;
}

static DEVICE_API(can, can_ti_cc35xx_api) = {
	.get_capabilities = can_mcan_get_capabilities,
	.start = can_ti_cc35xx_start,
	.stop = can_ti_cc35xx_stop,
	.set_mode = can_mcan_set_mode,
	.set_timing = can_mcan_set_timing,
	.send = can_mcan_send,
	.add_rx_filter = can_ti_cc35xx_add_rx_filter,
	.remove_rx_filter = can_ti_cc35xx_remove_rx_filter,
#ifdef CONFIG_CAN_MANUAL_RECOVERY_MODE
	.recover = can_mcan_recover,
#endif /* CONFIG_CAN_MANUAL_RECOVERY_MODE */
	.get_state = can_mcan_get_state,
	.get_core_clock = can_ti_cc35xx_get_core_clock,
	.get_max_filters = can_mcan_get_max_filters,
	.set_state_change_callback = can_mcan_set_state_change_callback,
	.timing_min = CAN_MCAN_TIMING_MIN_INITIALIZER,
	.timing_max = CAN_MCAN_TIMING_MAX_INITIALIZER,
};

static const struct can_mcan_ops can_ti_cc35xx_ops = {
	.read_reg = can_ti_cc35xx_read_reg,
	.write_reg = can_ti_cc35xx_write_reg,
	.read_mram = can_ti_cc35xx_read_mram,
	.write_mram = can_ti_cc35xx_write_mram,
	.clear_mram = can_ti_cc35xx_clear_mram,
};

#define CAN_TI_CC35XX_IRQ_CFG(inst)                                                                \
	static void can_ti_cc35xx_irq_config_##inst(void)                                          \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, int0, irq),                                  \
			    DT_INST_IRQ_BY_NAME(inst, int0, priority), can_ti_cc35xx_isr_line0,    \
			    DEVICE_DT_GET(DT_INST(inst, DT_DRV_COMPAT)), 0);                       \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, int0, irq));                                  \
                                                                                                   \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, int1, irq),                                  \
			    DT_INST_IRQ_BY_NAME(inst, int1, priority), can_ti_cc35xx_isr_line1,    \
			    DEVICE_DT_GET(DT_INST(inst, DT_DRV_COMPAT)), 0);                       \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, int1, irq));                                  \
	}

#define CAN_TI_CC35XX_INIT(inst)                                                                   \
	CAN_MCAN_DT_INST_BUILD_ASSERT_MRAM_CFG(inst);                                              \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	CAN_MCAN_DT_INST_CALLBACKS_DEFINE(inst, can_ti_cc35xx_cbs_##inst);                         \
	CAN_TI_CC35XX_IRQ_CFG(inst);                                                               \
                                                                                                   \
	static const struct can_ti_cc35xx_config can_ti_cc35xx_config_##inst = {                   \
		.mcan = CAN_MCAN_DT_INST_MCAN_ADDR(inst),                                          \
		.mram = CAN_MCAN_DT_INST_MRAM_ADDR(inst),                                          \
		.mrba = CAN_MCAN_DT_INST_MRBA(inst),                                               \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.irq_configure = can_ti_cc35xx_irq_config_##inst,                                  \
	};                                                                                         \
                                                                                                   \
	static const struct can_mcan_config can_mcan_config_##inst =                               \
		CAN_MCAN_DT_CONFIG_INST_GET(inst, &can_ti_cc35xx_config_##inst,                    \
					    &can_ti_cc35xx_ops, &can_ti_cc35xx_cbs_##inst);        \
                                                                                                   \
	static uint32_t                                                                            \
		can_ti_cc35xx_filters_##inst[CAN_MCAN_DT_MRAM_RX_FIFO0_OFFSET(DT_DRV_INST(inst)) / \
					     sizeof(uint32_t)];                                    \
	static struct can_ti_cc35xx_data can_ti_cc35xx_data_##inst = {                             \
		.filters = can_ti_cc35xx_filters_##inst,                                           \
	};                                                                                         \
                                                                                                   \
	CAN_MCAN_DATA_DEFINE(can_mcan_data_##inst, &can_ti_cc35xx_data_##inst);                    \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, can_ti_cc35xx_pm_action);                                   \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, can_ti_cc35xx_init, PM_DEVICE_DT_INST_GET(inst),               \
			      &can_mcan_data_##inst, &can_mcan_config_##inst, POST_KERNEL,         \
			      CONFIG_CAN_INIT_PRIORITY, &can_ti_cc35xx_api);

DT_INST_FOREACH_STATUS_OKAY(CAN_TI_CC35XX_INIT)
