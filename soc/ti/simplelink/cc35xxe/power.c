/*
 * Copyright (c) 2026 Conclusive Engineering Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/pm/cc35xx_pm.h>
#include <zephyr/pm/policy.h>

#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/sys_io.h>
#include <cmsis_core.h>
#include <inc/hw_types.h>
#include <inc/hw_memmap.h>
#include <inc/hw_hostmcu_aon.h>
#include <inc/hw_rtc.h>
#include <inc/hw_systim.h>
#include <inc/hw_ints.h>

/* ELP wake-up threshold and NVIC helpers, as used by TI PowerWFF3_enterSleep(). */
#define ELP_WAKEUP_THRESHOLD  0x3U
#define NVIC_IRQ_REG_NUM(num) ((num) >> 5U)
#define NVIC_IRQ_BIT_POS(num) BIT(((uint32_t)(num)) & 0x1FU)

#define ELP_TIMER_IS_ENABLED()                                                                     \
	(((sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN) & HOSTMCU_AON_ELPTMREN_VAL_M)) != \
	 HOSTMCU_AON_ELPTMREN_VAL_DIS)

#define ELP_TIMER_STOP_AND_RELOAD()                                                                \
	do {                                                                                       \
		sys_write32(sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN) |                \
				    HOSTMCU_AON_ELPTMREN_ELPTMRRST,                                \
			    HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN);                            \
		while (ELP_TIMER_IS_ENABLED()) {                                                   \
		}                                                                                  \
		sys_write32(sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN) &                \
				    ~(HOSTMCU_AON_ELPTMREN_ELPTMRRST |                             \
				      HOSTMCU_AON_ELPTMREN_ELPTMRSET),                             \
			    HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN);                            \
		sys_write32(sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN) |                \
				    HOSTMCU_AON_ELPTMREN_ELPTMRLD,                                 \
			    HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN);                            \
	} while (0)

#define ELP_TIMER_START()                                                                          \
	sys_write32(sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN) |                        \
			    HOSTMCU_AON_ELPTMREN_ELPTMRSET,                                        \
		    HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN)

/* Keep PM states locked until the wake sources are configured. */
static int cc35xx_pm_early_lock(void)
{
	pm_policy_state_all_lock_get();
	return 0;
}

SYS_INIT(cc35xx_pm_early_lock, PRE_KERNEL_1, 0);

__ramfunc static void cc35xx_pm_enter_standby_ram(void)
{
	uint32_t irq_status0;
	uint32_t irq_status1;
	uint32_t elptmren;
	bool elp_running;
	bool elp_sw_mode;

	SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

	irq_status0 = NVIC->ISER[0];
	irq_status1 = NVIC->ISER[1];
	elptmren = sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN);

	elp_running = (elptmren & HOSTMCU_AON_ELPTMREN_VAL_M) == HOSTMCU_AON_ELPTMREN_VAL_EN;
	elp_sw_mode =
		(elptmren & HOSTMCU_AON_ELPTMREN_TMRSWCTL_M) == HOSTMCU_AON_ELPTMREN_TMRSWCTL_SW;

	NVIC->ICER[1] = irq_status1;
	NVIC->ICER[0] = irq_status0;
	__DSB();
	__ISB();

	__COMPILER_BARRIER();
	NVIC->ISER[NVIC_IRQ_REG_NUM(INT_HOST_ELP_TMR_WAKEUP_REQ_IRQn)] =
		NVIC_IRQ_BIT_POS(INT_HOST_ELP_TMR_WAKEUP_REQ_IRQn);
	__COMPILER_BARRIER();
	sys_write32(sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWICSNS) &
			    ~HOSTMCU_AON_CFGWICSNS_VAL_TMRREQ_EN,
		    HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWICSNS);
	NVIC->ICPR[NVIC_IRQ_REG_NUM(INT_HOST_ELP_TMR_WAKEUP_REQ_IRQn)] =
		NVIC_IRQ_BIT_POS(INT_HOST_ELP_TMR_WAKEUP_REQ_IRQn);

	if (!elp_sw_mode) {
		sys_write32(sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN) |
				    HOSTMCU_AON_ELPTMREN_TMRSWCTL_SW,
			    HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN);
	}

	ELP_TIMER_STOP_AND_RELOAD();

	sys_write32(ELP_WAKEUP_THRESHOLD | HOSTMCU_AON_CFGTMRWU_EN_EN,
		    HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGTMRWU);

	ELP_TIMER_START();

	__DSB();
	__WFI();
	__ISB();

	sys_write32(sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGTMRWU) &
			    ~HOSTMCU_AON_CFGTMRWU_EN_EN,
		    HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGTMRWU);

	NVIC->ICER[NVIC_IRQ_REG_NUM(INT_HOST_ELP_TMR_WAKEUP_REQ_IRQn)] =
		NVIC_IRQ_BIT_POS(INT_HOST_ELP_TMR_WAKEUP_REQ_IRQn);
	__DSB();
	__ISB();

	sys_write32(HOSTMCU_AON_TMRWUREQ_CLR_M, HOSTMCU_AON_BASE + HOSTMCU_AON_O_TMRWUREQ);
	sys_write32(sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN) &
			    ~HOSTMCU_AON_ELPTMREN_ELPTMRSET,
		    HOSTMCU_AON_BASE + HOSTMCU_AON_O_ELPTMREN);

	if (!elp_sw_mode || (elp_sw_mode && !elp_running)) {
		ELP_TIMER_STOP_AND_RELOAD();
	}

	NVIC->ICPR[NVIC_IRQ_REG_NUM(INT_HOST_ELP_TMR_WAKEUP_REQ_IRQn)] =
		NVIC_IRQ_BIT_POS(INT_HOST_ELP_TMR_WAKEUP_REQ_IRQn);

	__COMPILER_BARRIER();
	NVIC->ISER[1] = irq_status1;
	NVIC->ISER[0] = irq_status0;
	__COMPILER_BARRIER();

	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}

static void configure_wake_sources(void)
{
	/* Wake on the GPIO OR event (any enabled pin edge), not on the AND event. */
	sys_set_bits(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWICSNS,
		     HOSTMCU_AON_CFGWICSNS_VAL_WUSRC1_EN);
	sys_clear_bits(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWICSNS,
		       HOSTMCU_AON_CFGWICSNS_VAL_WUSRC0_EN);
	sys_set_bits(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWUTP, HOSTMCU_AON_CFGWUTP_VAL_WUSRC1);
	/* Wake on the RTC compare event used by the system timer. */
	sys_set_bits(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWICSNS, HOSTMCU_AON_CFGWICSNS_VAL_RTC_EN);
	sys_set_bits(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWUTP, HOSTMCU_AON_CFGWUTP_VAL_RTC);
	/* Allow a debugger power-up request to wake the device. */
	sys_set_bits(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWICSNS,
		     HOSTMCU_AON_CFGWICSNS_VAL_DBGPWRUP_EN);
	sys_set_bits(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWUTP, HOSTMCU_AON_CFGWUTP_VAL_DBGPWRUP);
	/* Leave watchdog ownership with its driver when it is enabled. */
	if (!IS_ENABLED(CONFIG_WATCHDOG)) {
		sys_clear_bits(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWDT, HOSTMCU_AON_CFGWDT_EN);
	}
	irq_disable(INT_DEBUGSS_HOST_CSYSPWRUPREQ_IRQn);
	NVIC_ClearPendingIRQ(INT_DEBUGSS_HOST_CSYSPWRUPREQ_IRQn);
}

static int cc35xx_pm_init(void)
{
	configure_wake_sources();
	pm_policy_state_all_lock_put();
	return 0;
}

SYS_INIT(cc35xx_pm_init, APPLICATION, 99);

__weak void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	uint32_t systim_clock;

	ARG_UNUSED(substate_id);
	/* Zephyr 4.4 releases the idle IRQ lock in post_ops after device resume. */
	__disable_irq();
	irq_unlock(0);
	__DSB();
	__ISB();
	/* As TI PowerWFF3_sleepPolicy(), only enter standby if no wake source is pending. */
	if ((state == PM_STATE_STANDBY) &&
	    ((sys_read32(RTC_BASE + RTC_O_MIS) & RTC_MIS_EV0) == 0U) &&
	    ((sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_WUREQ) &
	      sys_read32(HOSTMCU_AON_BASE + HOSTMCU_AON_O_CFGWICSNS)) == 0U)) {
		/* Mirror the SYSTIMER dependency gating in TI Power_sleep(). */
		systim_clock = sys_read32(SYSTIM_BASE + SYSTIM_O_CLKCFG);
		sys_write32(0U, SYSTIM_BASE + SYSTIM_O_CLKCFG);
		cc35xx_pm_enter_standby_ram();
		sys_write32(systim_clock, SYSTIM_BASE + SYSTIM_O_CLKCFG);
		/* Wait for the SysTimer to resynchronise with the RTC. */
		while (sys_read32(SYSTIM_BASE + SYSTIM_O_STA) != SYSTIM_STA_VAL_RUN) {
		}
		cc35xx_pm_resources_resume();
	} else {
		__DSB();
		__WFI();
	}
}

__weak void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
	irq_unlock(0);
	__enable_irq();
	__ISB();
}
