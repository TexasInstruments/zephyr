/*
 * Copyright (c) 2025 Conclusive Engineering Sp. z o.o.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_cc35xx_lgpt_pwm

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/sys_io.h>
#include <inc/hw_gptimer.h>
#include "gptimer_pm.h"

#define GPTIMER_O_CXCC(index) (GPTIMER_O_C0CC + ((index) * 4U))
#define GPTIMER_O_CXCFG(index) (GPTIMER_O_C0CFG + ((index) * 4U))

struct pwm_cc35xx_config {
	uint32_t base;
	const struct pinctrl_dev_config *pcfg;
	uint32_t freq;
	uint32_t prescale;
	uint8_t channel;
};

struct pwm_cc35xx_data {
	bool powered;
	bool configured;
	pwm_flags_t flags;
};

static void pwm_cc35xx_idle(const struct pwm_cc35xx_config *config, pwm_flags_t flags)
{
	uint32_t level = (flags & PWM_POLARITY_INVERTED) != 0U ?
		GPTIMER_OUTCTL_SETOUT0 : GPTIMER_OUTCTL_CLROUT0;

	sys_write32(0U, config->base + GPTIMER_O_CXCFG(config->channel));
	sys_write32(level << (config->channel * 2U), config->base + GPTIMER_O_OUTCTL);
}

static int pwm_cc35xx_set_cycles(const struct device *dev, uint32_t channel, uint32_t period,
				uint32_t pulse, pwm_flags_t flags)
{
	const struct pwm_cc35xx_config *config = dev->config;
	struct pwm_cc35xx_data *data = dev->data;
	struct cc35xx_gptimer_state *shared = cc35xx_gptimer_state(config->base);
	uint32_t own_bit = BIT(config->channel);
	uint32_t action;
	int ret = 0;
	k_spinlock_key_t key;

	if (channel != config->channel || pulse > period ||
	    (flags & ~PWM_POLARITY_INVERTED) != 0U) {
		return -EINVAL;
	}
	key = k_spin_lock(&shared->lock);
	if (!data->powered) {
		ret = -EACCES;
		goto out;
	}
	if (shared->counter_running || shared->alarm_mask != 0U) {
		ret = -EBUSY;
		goto out;
	}
	/* Each bank has a single period and prescaler, shared by all PWM outputs. */
	if (pulse != 0U && (shared->pwm_mask & ~own_bit) != 0U &&
	    shared->pwm_period != period) {
		ret = -EBUSY;
		goto out;
	}
	data->configured = true;
	data->flags = flags;
	if (pulse == 0U) {
		pwm_cc35xx_idle(config, flags);
		shared->pwm_mask &= ~own_bit;
		if (shared->pwm_mask == 0U) {
			sys_clear_bits(config->base + GPTIMER_O_CTL, GPTIMER_CTL_MODE_M);
		}
		cc35xx_gptimer_sync_policy(shared);
		goto out;
	}
	if (shared->pwm_mask == 0U) {
		sys_write32(UINT32_MAX, config->base + GPTIMER_O_IMCLR);
		sys_write32(UINT32_MAX, config->base + GPTIMER_O_ICLR);
	}
	/* Establish the hold before arming any output, including repeated updates. */
	shared->pwm_mask |= own_bit;
	cc35xx_gptimer_sync_policy(shared);
	shared->pwm_period = period;
	sys_write32(config->prescale << 8U, config->base + GPTIMER_O_PRECFG);
	sys_write32(period, config->base + GPTIMER_O_TGT);
	sys_write32(pulse, config->base + GPTIMER_O_CXCC(channel));
	action = (flags & PWM_POLARITY_INVERTED) != 0U ?
		GPTIMER_C0CFG_CCACT_CLR_ON_0_TGL_ON_CMP :
		GPTIMER_C0CFG_CCACT_SET_ON_0_TGL_ON_CMP;
	if (pulse == period) {
		action = GPTIMER_C0CFG_CCACT_DIS;
		sys_write32(((flags & PWM_POLARITY_INVERTED) != 0U ?
			     GPTIMER_OUTCTL_CLROUT0 : GPTIMER_OUTCTL_SETOUT0) << (channel * 2U),
			    config->base + GPTIMER_O_OUTCTL);
	}
	sys_write32((GPTIMER_C0CFG_OUT0_EN << channel) | action,
		    config->base + GPTIMER_O_CXCFG(channel));
	sys_write32(GPTIMER_CTL_MODE_UP_PER, config->base + GPTIMER_O_CTL);
	sys_write32(1U, config->base + GPTIMER_O_STARTCFG);
out:
	k_spin_unlock(&shared->lock, key);
	return ret;
}

static int pwm_cc35xx_get_cycles_per_sec(const struct device *dev, uint32_t channel,
				       uint64_t *cycles)
{
	const struct pwm_cc35xx_config *config = dev->config;

	if (channel != config->channel) {
		return -EINVAL;
	}
	*cycles = config->freq / (config->prescale + 1U);
	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int pwm_cc35xx_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct pwm_cc35xx_config *config = dev->config;
	struct pwm_cc35xx_data *data = dev->data;
	struct cc35xx_gptimer_state *shared = cc35xx_gptimer_state(config->base);
	k_spinlock_key_t key = k_spin_lock(&shared->lock);
	int ret = 0;

	if (action == PM_DEVICE_ACTION_SUSPEND) {
		if (cc35xx_gptimer_busy(shared)) {
			ret = -EBUSY;
		} else if (data->powered) {
			ret = cc35xx_pm_resource_put(cc35xx_gptimer_resource(config->base));
			if (ret == 0) {
				data->powered = false;
			}
		}
	} else if (action == PM_DEVICE_ACTION_RESUME) {
		if (!data->powered && cc35xx_gptimer_busy(shared)) {
			ret = -EBUSY;
		} else if (!data->powered) {
			ret = cc35xx_pm_resource_get(cc35xx_gptimer_resource(config->base));
			if (ret == 0) {
				data->powered = true;
				if (data->configured) {
					pwm_cc35xx_idle(config, data->flags);
				}
			}
		}
	} else {
		ret = -ENOTSUP;
	}
	k_spin_unlock(&shared->lock, key);
	return ret;
}
#endif

static int pwm_cc35xx_init(const struct device *dev)
{
	const struct pwm_cc35xx_config *config = dev->config;
	struct pwm_cc35xx_data *data = dev->data;
	int ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);

	if (ret != 0) {
		return ret;
	}
	ret = cc35xx_pm_resource_get(cc35xx_gptimer_resource(config->base));
	if (ret == 0) {
		data->powered = true;
	}
	return ret;
}

static DEVICE_API(pwm, pwm_cc35xx_api) = {
	.set_cycles = pwm_cc35xx_set_cycles,
	.get_cycles_per_sec = pwm_cc35xx_get_cycles_per_sec,
};

#define PWM_CC35XX_INIT(n)                                                                     \
	PINCTRL_DT_INST_DEFINE(n);                                                            \
	static const struct pwm_cc35xx_config pwm_config_##n = {                              \
		.base = DT_REG_ADDR(DT_INST_PARENT(n)),                                       \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                    \
		.freq = DT_PROP_BY_PHANDLE(DT_INST_PARENT(n), clocks, clock_frequency),        \
		.prescale = DT_PROP(DT_INST_PARENT(n), clk_prescale),                          \
		.channel = DT_NODE_CHILD_IDX(DT_DRV_INST(n)),                                  \
	};                                                                                   \
	static struct pwm_cc35xx_data pwm_data_##n;                                            \
	PM_DEVICE_DT_INST_DEFINE(n, pwm_cc35xx_pm_action);                                     \
	DEVICE_DT_INST_DEFINE(n, pwm_cc35xx_init, PM_DEVICE_DT_INST_GET(n),                     \
			      &pwm_data_##n, &pwm_config_##n, POST_KERNEL,                     \
			      CONFIG_PWM_INIT_PRIORITY, &pwm_cc35xx_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_CC35XX_INIT)
