/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gptimer_pm.h"

#include <zephyr/pm/policy.h>
#include <inc/hw_memmap.h>

static struct cc35xx_gptimer_state timers[2];

struct cc35xx_gptimer_state *cc35xx_gptimer_state(uint32_t base)
{
	__ASSERT(base == GPTIMER0_BASE || base == GPTIMER1_BASE, "Invalid GPTimer base");
	return &timers[base == GPTIMER0_BASE ? 0 : 1];
}

enum cc35xx_pm_resource cc35xx_gptimer_resource(uint32_t base)
{
	return base == GPTIMER0_BASE ? CC35XX_PM_RESOURCE_GPTIMER0 : CC35XX_PM_RESOURCE_GPTIMER1;
}

bool cc35xx_gptimer_busy(const struct cc35xx_gptimer_state *state)
{
	return state->counter_running || state->alarm_mask != 0U || state->pwm_mask != 0U;
}

void cc35xx_gptimer_sync_policy(struct cc35xx_gptimer_state *state)
{
	bool busy = cc35xx_gptimer_busy(state);

	if (busy && !state->policy_locked) {
		pm_policy_state_lock_get(PM_STATE_RUNTIME_IDLE, PM_ALL_SUBSTATES);
		pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		state->policy_locked = true;
	} else if (!busy && state->policy_locked) {
		state->policy_locked = false;
		pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		pm_policy_state_lock_put(PM_STATE_RUNTIME_IDLE, PM_ALL_SUBSTATES);
	}
}
