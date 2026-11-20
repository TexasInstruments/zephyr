/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TI_CC35XX_GPTIMER_PM_H_
#define TI_CC35XX_GPTIMER_PM_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/pm/cc35xx_pm.h>
#include <zephyr/spinlock.h>

/* All fields are protected by lock, including across PWM and counter devices. */
struct cc35xx_gptimer_state {
	struct k_spinlock lock;
	uint32_t pwm_mask;
	uint32_t pwm_period;
	uint32_t alarm_mask;
	bool counter_running;
	bool policy_locked;
};

struct cc35xx_gptimer_state *cc35xx_gptimer_state(uint32_t base);
enum cc35xx_pm_resource cc35xx_gptimer_resource(uint32_t base);
/* Called with the shared lock held after changing an activity field. */
void cc35xx_gptimer_sync_policy(struct cc35xx_gptimer_state *state);
bool cc35xx_gptimer_busy(const struct cc35xx_gptimer_state *state);

#endif /* TI_CC35XX_GPTIMER_PM_H_ */
