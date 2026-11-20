/*
 * Copyright (c) 2026 Conclusive Engineering Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "can_ti_cc35xx.h"
#include <inc/hw_dcan.h>
#include <cmsis_core.h>

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

/* Only stopped controllers may lose their register and message RAM context. */
static const uint16_t context_regs[] = {
	CAN_MCAN_CCCR, CAN_MCAN_NBTP, CAN_MCAN_DBTP, CAN_MCAN_TEST, CAN_MCAN_TSCC,
	CAN_MCAN_TOCC, CAN_MCAN_TDCR, CAN_MCAN_GFC, CAN_MCAN_SIDFC, CAN_MCAN_XIDFC,
	CAN_MCAN_XIDAM, CAN_MCAN_RXF0C, CAN_MCAN_RXF1C, CAN_MCAN_RXBC, CAN_MCAN_RXESC,
	CAN_MCAN_TXBC, CAN_MCAN_TXESC, CAN_MCAN_TXEFC, CAN_MCAN_TXBTIE, CAN_MCAN_TXBCIE,
	CAN_MCAN_ILS, CAN_MCAN_IE, CAN_MCAN_ILE,
};

struct can_ti_cc35xx_data {
	struct k_mutex api_lock;
	uint32_t regs[ARRAY_SIZE(context_regs)];
	uint32_t *mram_context;
	uint32_t users;
	bool powered;
};

static void can_ti_cc35xx_hold(void)
{
	pm_policy_state_lock_get(PM_STATE_RUNTIME_IDLE, PM_ALL_SUBSTATES);
	pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
}

static void can_ti_cc35xx_release(void)
{
	pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
	pm_policy_state_lock_put(PM_STATE_RUNTIME_IDLE, PM_ALL_SUBSTATES);
}

static int can_ti_cc35xx_enter(const struct device *dev, bool serialize)
{
	struct can_mcan_data *mcan = dev->data;
	struct can_ti_cc35xx_data *data = mcan->custom;
	unsigned int key = irq_lock();

	if (!data->powered) {
		irq_unlock(key);
		return -EACCES;
	}
	data->users++;
	can_ti_cc35xx_hold();
	irq_unlock(key);
	if (serialize && !k_is_in_isr()) {
		k_mutex_lock(&data->api_lock, K_FOREVER);
	}
	return 0;
}

static void can_ti_cc35xx_leave(const struct device *dev, bool serialize)
{
	struct can_mcan_data *mcan = dev->data;
	struct can_ti_cc35xx_data *data = mcan->custom;
	unsigned int key;

	if (serialize && !k_is_in_isr()) {
		k_mutex_unlock(&data->api_lock);
	}
	key = irq_lock();
	data->users--;
	can_ti_cc35xx_release();
	irq_unlock(key);
}

static int can_ti_cc35xx_start(const struct device *dev)
{
	int ret = can_ti_cc35xx_enter(dev, true);

	if (ret != 0) {
		return ret;
	}
	/* Retain a separate hold until stop, including asynchronous reception. */
	can_ti_cc35xx_hold();
	ret = can_mcan_start(dev);
	if (ret != 0) {
		can_ti_cc35xx_release();
	}
	can_ti_cc35xx_leave(dev, true);
	return ret;
}

static int can_ti_cc35xx_stop(const struct device *dev)
{
	struct can_mcan_data *mcan = dev->data;
	int ret = can_ti_cc35xx_enter(dev, true);

	if (ret != 0) {
		return ret;
	}
	ret = can_mcan_stop(dev);
	if (ret == 0) {
		can_ti_cc35xx_release();
		if (!mcan->common.started) {
			pm_device_busy_clear(dev);
		}
	}
	can_ti_cc35xx_leave(dev, true);
	return ret;
}

static int can_ti_cc35xx_set_mode(const struct device *dev, can_mode_t mode)
{
	int ret = can_ti_cc35xx_enter(dev, true);

	if (ret == 0) {
		ret = can_mcan_set_mode(dev, mode);
		can_ti_cc35xx_leave(dev, true);
	}
	return ret;
}

static int can_ti_cc35xx_set_timing(const struct device *dev, const struct can_timing *timing)
{
	int ret = can_ti_cc35xx_enter(dev, true);

	if (ret == 0) {
		ret = can_mcan_set_timing(dev, timing);
		can_ti_cc35xx_leave(dev, true);
	}
	return ret;
}

static int can_ti_cc35xx_send(const struct device *dev, const struct can_frame *frame,
			    k_timeout_t timeout, can_tx_callback_t callback, void *user_data)
{
	int ret = can_ti_cc35xx_enter(dev, false);

	if (ret == 0) {
		ret = can_mcan_send(dev, frame, timeout, callback, user_data);
		can_ti_cc35xx_leave(dev, false);
	}
	return ret;
}

static int can_ti_cc35xx_add_rx_filter(const struct device *dev, can_rx_callback_t callback,
				     void *user_data, const struct can_filter *filter)
{
	struct can_mcan_data *mcan = dev->data;
	int ret = can_ti_cc35xx_enter(dev, true);

	if (ret == 0) {
		ret = can_mcan_add_rx_filter(dev, callback, user_data, filter);
		/* Filter RAM is saved on suspend; stopped filters do not prevent PM. */
		if (!mcan->common.started) {
			pm_device_busy_clear(dev);
		}
		can_ti_cc35xx_leave(dev, true);
	}
	return ret;
}

static void can_ti_cc35xx_remove_rx_filter(const struct device *dev, int filter_id)
{
	if (can_ti_cc35xx_enter(dev, true) == 0) {
		can_mcan_remove_rx_filter(dev, filter_id);
		can_ti_cc35xx_leave(dev, true);
	}
}

static int can_ti_cc35xx_get_state(const struct device *dev, enum can_state *state,
				 struct can_bus_err_cnt *err_cnt)
{
	int ret = can_ti_cc35xx_enter(dev, false);

	if (ret == 0) {
		ret = can_mcan_get_state(dev, state, err_cnt);
		can_ti_cc35xx_leave(dev, false);
	}
	return ret;
}

#ifdef CONFIG_CAN_MANUAL_RECOVERY_MODE
static int can_ti_cc35xx_recover(const struct device *dev, k_timeout_t timeout)
{
	int ret = can_ti_cc35xx_enter(dev, true);

	if (ret == 0) {
		ret = can_mcan_recover(dev, timeout);
		can_ti_cc35xx_leave(dev, true);
	}
	return ret;
}
#endif

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
	const uint32_t timeout_ms = CAN_TI_CC35XX_CLKCFG_TIMEOUT_MS;
	mm_reg_t clkcfg_reg;

	sys_write32(0, config->mcan + CAN_TI_CC35XX_CLKCFG);

	k_busy_wait(1);

	sys_write32(CAN_TI_CC35XX_CLKCFG_CLKEN | CAN_TI_CC35XX_CLKCFG_RAMEN |
			    CAN_TI_CC35XX_CLKCFG_CLKSEL_HOST_DIV2_CLK,
		    config->mcan + CAN_TI_CC35XX_CLKCFG);

	clkcfg_reg = config->mcan + CAN_TI_CC35XX_CLKCFG;
	if (!WAIT_FOR((sys_read32(clkcfg_reg) & CAN_TI_CC35XX_CLKCFG_CLKEN) != 0U,
		      timeout_ms * 1000U, k_busy_wait(1))) {
		return -ETIMEDOUT;
	}

	if (!WAIT_FOR((sys_read32(config->mcan + DCAN_O_SSSTA) &
		       DCAN_SSSTA_MEMINITSTA) != 0U, timeout_ms * 1000U, k_busy_wait(1))) {
		return -ETIMEDOUT;
	}

	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int can_ti_cc35xx_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;
	struct can_mcan_data *mcan = dev->data;
	struct can_ti_cc35xx_data *data = mcan->custom;
	unsigned int key = irq_lock();
	int ret = 0;

	if (action == PM_DEVICE_ACTION_SUSPEND) {
		if (data->users != 0U || mcan->common.started) {
			ret = -EBUSY;
			goto out;
		}
		for (size_t i = 0U; i < ARRAY_SIZE(context_regs); i++) {
			can_ti_cc35xx_read_reg(dev, context_regs[i], &data->regs[i]);
		}
		can_ti_cc35xx_read_mram(dev, 0U, data->mram_context, mcan_config->mram_size);
		can_ti_cc35xx_write_reg(dev, CAN_MCAN_ILE, 0U);
		irq_disable(config->irq0);
		irq_disable(config->irq1);
		data->powered = false;
		ret = cc35xx_pm_resource_put(CC35XX_PM_RESOURCE_DCAN);
	} else if (action == PM_DEVICE_ACTION_RESUME) {
		ret = cc35xx_pm_resource_get(CC35XX_PM_RESOURCE_DCAN);
		if (ret != 0) {
			goto out;
		}
		ret = enable_core_clock(dev);
		if (ret == 0) {
			uint32_t cccr;

			/* Avoid sleeping MCAN helpers in the system-managed PM callback. */
			cccr = sys_read32(config->mcan + CAN_MCAN_CCCR);
			cccr = (cccr & ~CAN_MCAN_CCCR_CSR) | CAN_MCAN_CCCR_INIT;
			sys_write32(cccr, config->mcan + CAN_MCAN_CCCR);
			if (!WAIT_FOR((sys_read32(config->mcan + CAN_MCAN_CCCR) &
				       (CAN_MCAN_CCCR_INIT | CAN_MCAN_CCCR_CSA)) ==
				      CAN_MCAN_CCCR_INIT, 100000U, k_busy_wait(1))) {
				ret = -ETIMEDOUT;
			}
			sys_write32(cccr | CAN_MCAN_CCCR_CCE, config->mcan + CAN_MCAN_CCCR);
		}
		if (ret != 0) {
			(void)cc35xx_pm_resource_put(CC35XX_PM_RESOURCE_DCAN);
			goto out;
		}
		can_ti_cc35xx_write_mram(dev, 0U, data->mram_context, mcan_config->mram_size);
		/* CCCR first enables configuration writes; ILE is restored last. */
		for (size_t i = 0U; i < ARRAY_SIZE(context_regs); i++) {
			can_ti_cc35xx_write_reg(dev, context_regs[i], data->regs[i]);
		}
		enable_interrupt_lines(dev);
		NVIC_ClearPendingIRQ(config->irq0);
		NVIC_ClearPendingIRQ(config->irq1);
		data->powered = true;
		irq_enable(config->irq0);
		irq_enable(config->irq1);
	} else {
		ret = -ENOTSUP;
	}
out:
	irq_unlock(key);
	return ret;
}
#endif

static int can_ti_cc35xx_init(const struct device *dev)
{
	const struct can_mcan_config *mcan_config = dev->config;
	const struct can_ti_cc35xx_config *config = mcan_config->custom;
	struct can_mcan_data *mcan = dev->data;
	struct can_ti_cc35xx_data *data = mcan->custom;
	int err;

	k_mutex_init(&data->api_lock);
	err = cc35xx_pm_resource_get(CC35XX_PM_RESOURCE_DCAN);
	if (err != 0) {
		return err;
	}
	data->powered = true;
	err = enable_core_clock(dev);
	if (err != 0) {
		LOG_ERR("Failed to enable core clock (%d)", err);
		goto fail;
	}

	err = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (err != 0) {
		LOG_ERR("Failed to apply pinctrl state (%d)", err);
		goto fail;
	}

	err = can_mcan_configure_mram(dev, config->mrba, config->mram);
	if (err != 0) {
		LOG_ERR("Failed to configure mcan message ram (%d)", err);
		goto fail;
	}

	err = can_mcan_init(dev);
	if (err != 0) {
		LOG_ERR("Failed to initialize can_mcan core (%d)", err);
		goto fail;
	}

	config->irq_configure();
	enable_interrupt_lines(dev);

	return 0;
fail:
	data->powered = false;
	(void)cc35xx_pm_resource_put(CC35XX_PM_RESOURCE_DCAN);
	return err;
}

static DEVICE_API(can, can_ti_cc35xx_api) = {
	.get_capabilities = can_mcan_get_capabilities,
	.start = can_ti_cc35xx_start,
	.stop = can_ti_cc35xx_stop,
	.set_mode = can_ti_cc35xx_set_mode,
	.set_timing = can_ti_cc35xx_set_timing,
	.send = can_ti_cc35xx_send,
	.add_rx_filter = can_ti_cc35xx_add_rx_filter,
	.remove_rx_filter = can_ti_cc35xx_remove_rx_filter,
#ifdef CONFIG_CAN_MANUAL_RECOVERY_MODE
	.recover = can_ti_cc35xx_recover,
#endif /* CONFIG_CAN_MANUAL_RECOVERY_MODE */
	.get_state = can_ti_cc35xx_get_state,
	.set_state_change_callback = can_mcan_set_state_change_callback,
	.get_core_clock = can_ti_cc35xx_get_core_clock,
	.get_max_filters = can_mcan_get_max_filters,
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
		.irq0 = DT_INST_IRQ_BY_NAME(inst, int0, irq), \
		.irq1 = DT_INST_IRQ_BY_NAME(inst, int1, irq), \
		.irq_configure = can_ti_cc35xx_irq_config_##inst,                                  \
	};                                                                                         \
                                                                                                   \
	static const struct can_mcan_config can_mcan_config_##inst =                               \
		CAN_MCAN_DT_CONFIG_INST_GET(inst, &can_ti_cc35xx_config_##inst,                    \
					    &can_ti_cc35xx_ops, &can_ti_cc35xx_cbs_##inst);        \
                                                                                                   \
	static uint32_t can_mram_context_##inst[CAN_MCAN_DT_MRAM_ELEMENTS_SIZE(                    \
		DT_DRV_INST(inst)) / sizeof(uint32_t)];                                            \
	static struct can_ti_cc35xx_data can_ti_data_##inst = {                                    \
		.mram_context = can_mram_context_##inst,                                           \
	};                                                                                         \
	CAN_MCAN_DATA_DEFINE(can_mcan_data_##inst, &can_ti_data_##inst);                           \
	PM_DEVICE_DT_INST_DEFINE(inst, can_ti_cc35xx_pm_action);                                   \
	DEVICE_DT_INST_DEFINE(inst, can_ti_cc35xx_init, PM_DEVICE_DT_INST_GET(inst),               \
			      &can_mcan_data_##inst, &can_mcan_config_##inst, POST_KERNEL,         \
			      CONFIG_CAN_INIT_PRIORITY, &can_ti_cc35xx_api);

DT_INST_FOREACH_STATUS_OKAY(CAN_TI_CC35XX_INIT)
