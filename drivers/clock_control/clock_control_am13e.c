/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/am13e_clock_control.h>
#include <zephyr/dt-bindings/clock/am13e_clock.h>

#define DT_DRV_COMPAT ti_am13e_clk

/*
 * SYSCTL register base address and offsets
 */
BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(ckm), ti_sysctl_base),
	     "ti,sysctl-base property is missing from ckm devicetree node!");

#define AM13E_SYSCTL_BASE DT_PROP(DT_NODELABEL(ckm), ti_sysctl_base)

#define AM13E_SYSCTL_MCLKCFG      0x1104U
#define AM13E_SYSCTL_HSCLKEN      0x1108U
#define AM13E_SYSCTL_HSCLKCFG     0x110CU
#define AM13E_SYSCTL_HFCLKCLKCFG  0x1110U
#define AM13E_SYSCTL_SYSPLLCFG0   0x1120U
#define AM13E_SYSCTL_SYSPLLCFG1   0x1124U
#define AM13E_SYSCTL_SYSPLLPARAM0 0x1128U
#define AM13E_SYSCTL_SYSPLLPARAM1 0x112CU
#define AM13E_SYSCTL_GENCLKEN     0x113CU
#define AM13E_SYSCTL_GENCLKCFG    0x1140U
#define AM13E_SYSCTL_CLKSTATUS    0x1204U
#define AM13E_SYSCTL_XTALCR       0x1474U

/* MCLKCFG */
#define AM13E_MCLKCFG_USEHSCLK_EN     0x00010000U
#define AM13E_MCLKCFG_MCLKDIVCFG_MASK 0x07000000U

/* HSCLKEN */
#define AM13E_HSCLKEN_SYSPLLEN_MASK 0x00000100U
#define AM13E_HSCLKEN_SYSPLLEN_EN   0x00000100U

/* HSCLKCFG */
#define AM13E_HSCLKCFG_HSCLKSEL_MASK   0x00000001U
#define AM13E_HSCLKCFG_HSCLKSEL_SYSPLL 0x00000000U

/* HFCLKCLKCFG */
#define AM13E_HFCLKCLKCFG_XTALTIME_MASK 0x000000FFU
#define AM13E_HFCLKCLKCFG_FLTCHK_EN     0x10000000U

/* SYSPLLCFG0 */
#define AM13E_SYSPLLCFG0_SYSPLLREF_MASK  0x00000001U
#define AM13E_SYSPLLCFG0_SYSPLLREF_HFCLK 0x00000001U
#define AM13E_SYSPLLCFG0_ENABLECLK0_MASK 0x00000010U
#define AM13E_SYSPLLCFG0_ENABLECLK0_EN   0x00000010U
#define AM13E_SYSPLLCFG0_ENABLECLK1_MASK 0x00000020U
#define AM13E_SYSPLLCFG0_ENABLECLK1_EN   0x00000020U
#define AM13E_SYSPLLCFG0_RDIVCLK0_OFS    8U
#define AM13E_SYSPLLCFG0_RDIVCLK0_MASK   0x00000F00U
#define AM13E_SYSPLLCFG0_RDIVCLK1_OFS    12U
#define AM13E_SYSPLLCFG0_RDIVCLK1_MASK   0x0000F000U

/* SYSPLLCFG1 */
#define AM13E_SYSPLLCFG1_PDIV_MASK 0x00000003U
#define AM13E_SYSPLLCFG1_QDIV_OFS  8U
#define AM13E_SYSPLLCFG1_QDIV_MASK 0x00007F00U

/* GENCLKEN */
#define AM13E_GENCLKEN_EXTDIVMCLK_MASK  0x00000700U
#define AM13E_GENCLKEN_EXTDIVMCLK_DIV4  0x00000100U
#define AM13E_GENCLKEN_EXTDIVMCLK_DIV2  0x00000000U
#define AM13E_GENCLKEN_MCLKEXTDIVEN_EN  0x00000800U
#define AM13E_GENCLKEN_EXTDIVCAN_MASK   0x00007000U
#define AM13E_GENCLKEN_CANEXTDIVEN_MASK 0x00008000U
#define AM13E_GENCLKEN_CANEXTDIVEN_EN   0x00008000U

/* GENCLKCFG */
#define AM13E_GENCLKCFG_CANCLKSRC_MASK      0x00000100U
#define AM13E_GENCLKCFG_CANCLKSRC_HFCLK     0x00000000U
#define AM13E_GENCLKCFG_CANCLKSRC_SYSPLLOUT 0x00000100U

/* CLKSTATUS */
#define AM13E_CLKSTATUS_HFCLKGOOD_MASK  0x00000100U
#define AM13E_CLKSTATUS_SYSPLLGOOD_MASK 0x00000200U
#define AM13E_CLKSTATUS_HSCLKMUX_MASK   0x00000010U
#define AM13E_CLKSTATUS_SYSPLLOFF_MASK  0x00004000U
#define AM13E_CLKSTATUS_HSCLKGOOD_MASK  0x00200000U

/* XTALCR */
#define AM13E_XTALCR_OSCOFF_MASK 0x00000001U

/* Factory region: PLL startup parameter base addresses (per input freq range) */
#define AM13E_FACTORY_PLLPARAM0_4_8MHZ   0x60111020UL
#define AM13E_FACTORY_PLLPARAM0_8_16MHZ  0x60111028UL
#define AM13E_FACTORY_PLLPARAM0_16_32MHZ 0x60111030UL
#define AM13E_FACTORY_PLLPARAM0_32_48MHZ 0x60111038UL

/*
 * MMIO helpers
 */
#define SYSCTL_RD(off)        (*(volatile uint32_t *)(AM13E_SYSCTL_BASE + (off)))
#define SYSCTL_WR(off, val)   (*(volatile uint32_t *)(AM13E_SYSCTL_BASE + (off)) = (uint32_t)(val))
#define SYSCTL_SET(off, mask) SYSCTL_WR(off, SYSCTL_RD(off) | (uint32_t)(mask))
#define SYSCTL_CLR(off, mask) SYSCTL_WR(off, SYSCTL_RD(off) & ~(uint32_t)(mask))
#define SYSCTL_RMW(off, mask, val)                                                                 \
	SYSCTL_WR(off, (SYSCTL_RD(off) & ~(uint32_t)(mask)) | ((uint32_t)(val) & (uint32_t)(mask)))

/*
 * Compile-time presence checks
 */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(syspll), okay)
#define AM13E_PLL_ENABLED 1
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(hfxt), okay)
#define AM13E_HFXT_ENABLED 1
#endif

/*
 * Build-time validation
 */
#ifdef AM13E_PLL_ENABLED
#if !DT_NODE_HAS_PROP(DT_NODELABEL(syspll), ti_clk0_div) &&                                        \
	!DT_NODE_HAS_PROP(DT_NODELABEL(syspll), ti_clk1_div)
#error "syspll: at least one of ti,clk0-div or ti,clk1-div must be set"
#endif
#endif

/*
 * Compile-time clock tree values derived from device tree
 */
#define AM13E_MCLK_HZ  DT_PROP(DT_NODELABEL(mclk), clock_frequency)
#define AM13E_MCLK2_HZ DT_PROP(DT_NODELABEL(mclk2), clock_frequency)
#define AM13E_MCLK4_HZ DT_PROP(DT_NODELABEL(mclk4), clock_frequency)

/*
 * MCLKDIVCFG raw field value (written directly to MCLKCFG[26:24]).
 * Encoding: RATIO_1_1_1=0x0, RATIO_1_1_2=0x1, RATIO_1_1_4=0x3,
 *           RATIO_1_2_2=0x5, RATIO_1_2_4=0x7 (all shifted to bit 24).
 */
#define AM13E_MCLK_DIV_VAL                                                                         \
	((AM13E_MCLK2_HZ == AM13E_MCLK_HZ && AM13E_MCLK4_HZ == AM13E_MCLK_HZ)       ? 0x00000000UL \
	 : (AM13E_MCLK2_HZ == AM13E_MCLK_HZ && AM13E_MCLK4_HZ == AM13E_MCLK_HZ / 2) ? 0x01000000UL \
	 : (AM13E_MCLK2_HZ == AM13E_MCLK_HZ && AM13E_MCLK4_HZ == AM13E_MCLK_HZ / 4) ? 0x03000000UL \
	 : (AM13E_MCLK2_HZ == AM13E_MCLK_HZ / 2 && AM13E_MCLK4_HZ == AM13E_MCLK_HZ / 2)            \
		 ? 0x05000000UL                                                                    \
		 : 0x07000000UL)

BUILD_ASSERT((AM13E_MCLK2_HZ == AM13E_MCLK_HZ && AM13E_MCLK4_HZ == AM13E_MCLK_HZ) ||
		     (AM13E_MCLK2_HZ == AM13E_MCLK_HZ && AM13E_MCLK4_HZ == AM13E_MCLK_HZ / 2) ||
		     (AM13E_MCLK2_HZ == AM13E_MCLK_HZ && AM13E_MCLK4_HZ == AM13E_MCLK_HZ / 4) ||
		     (AM13E_MCLK2_HZ == AM13E_MCLK_HZ / 2 && AM13E_MCLK4_HZ == AM13E_MCLK_HZ / 2) ||
		     (AM13E_MCLK2_HZ == AM13E_MCLK_HZ / 2 && AM13E_MCLK4_HZ == AM13E_MCLK_HZ / 4),
	     "mclk2,mclk4: unsupported MCLK divider ratio combination");

#ifdef AM13E_PLL_ENABLED

/* Frequency of the PLL reference clock (parent of syspll) */
#define AM13E_PLL_REF_HZ DT_PROP(DT_CLOCKS_CTLR(DT_NODELABEL(syspll)), clock_frequency)

/* PLL input frequency after pre-divider: F_in = REF / p-div */
#define AM13E_PLL_INPUT_HZ (AM13E_PLL_REF_HZ / DT_PROP(DT_NODELABEL(syspll), ti_p_div))

BUILD_ASSERT(DT_PROP(DT_NODELABEL(syspll), ti_p_div) == 1 ||
		     DT_PROP(DT_NODELABEL(syspll), ti_p_div) == 2 ||
		     DT_PROP(DT_NODELABEL(syspll), ti_p_div) == 4 ||
		     DT_PROP(DT_NODELABEL(syspll), ti_p_div) == 8,
	     "syspll: ti,p-div must be 1, 2, 4, or 8");

BUILD_ASSERT(AM13E_PLL_INPUT_HZ >= 4000000 && AM13E_PLL_INPUT_HZ <= 48000000,
	     "syspll: PLL input frequency (REF/p-div) must be in range 4..48 MHz");

#if DT_NODE_HAS_PROP(DT_NODELABEL(syspll), ti_clk0_div)
BUILD_ASSERT(DT_PROP(DT_NODELABEL(syspll), ti_clk0_div) >= 2 &&
		     DT_PROP(DT_NODELABEL(syspll), ti_clk0_div) <= 32 &&
		     (DT_PROP(DT_NODELABEL(syspll), ti_clk0_div) % 2) == 0,
	     "syspll: ti,clk0-div must be an even value in range 2..32");
#endif

#if DT_NODE_HAS_PROP(DT_NODELABEL(syspll), ti_clk1_div)
BUILD_ASSERT(DT_PROP(DT_NODELABEL(syspll), ti_clk1_div) >= 2 &&
		     DT_PROP(DT_NODELABEL(syspll), ti_clk1_div) <= 32 &&
		     (DT_PROP(DT_NODELABEL(syspll), ti_clk1_div) % 2) == 0,
	     "syspll: ti,clk1-div must be an even value in range 2..32");
#endif

/* TODO
 * Factory region address of PLLPARAM0 for the applicable input-frequency band.
 * PLLPARAM1 is at +4 bytes from the same base.
 */
#define AM13E_FACTORY_PARAM0_ADDR                                                                  \
	((AM13E_PLL_INPUT_HZ >= 4000000 && AM13E_PLL_INPUT_HZ < 8000000)                           \
		 ? AM13E_FACTORY_PLLPARAM0_4_8MHZ                                                  \
	 : (AM13E_PLL_INPUT_HZ >= 8000000 && AM13E_PLL_INPUT_HZ < 16000000)                        \
		 ? AM13E_FACTORY_PLLPARAM0_8_16MHZ                                                 \
	 : (AM13E_PLL_INPUT_HZ >= 16000000 && AM13E_PLL_INPUT_HZ < 32000000)                       \
		 ? AM13E_FACTORY_PLLPARAM0_16_32MHZ                                                \
		 : AM13E_FACTORY_PLLPARAM0_32_48MHZ)

/*
 * Map pDiv integer (1, 2, 4, 8) to SYSPLLCFG1[PDIV] 2-bit encoded value.
 * Hardware: 0=÷1, 1=÷2, 2=÷4, 3=÷8.
 */
#define AM13E_PDIV_BITS(p) ((p) == 1U ? 0U : (p) == 2U ? 1U : (p) == 4U ? 2U : 3U)

/*
 * Map output-divider (2, 4, 6, ..., 32) to RDIVCLK field value.
 * Hardware: field=0 → ÷2, field=1 → ÷4, etc.  (field = d/2 − 1)
 */
#define AM13E_RDIV(d) (((d) / 2U) - 1U)

/* SYSPLL reference bit: 1 if parent is hfclk, 0 for sysosc */
#if DT_SAME_NODE(DT_CLOCKS_CTLR(DT_NODELABEL(syspll)), DT_NODELABEL(hfclk))
#define AM13E_PLL_REF_BIT AM13E_SYSPLLCFG0_SYSPLLREF_HFCLK
#else
#define AM13E_PLL_REF_BIT 0U
#endif

#endif /* AM13E_PLL_ENABLED */

/*
 * Conservative settling delay after an MCLK source switch or GENCLKEN
 * divider change. No minimum is specified in the TRM.
 */
#define AM13E_MCLK_SETTLE_CYCLES 20U

/*
 * Short cycle-count delay (replaces DL_Common_delayCycles)
 */
__maybe_unused static void am13e_delay(uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		__asm__ volatile("nop");
	}
}

/*
 * Clock control API callbacks
 */

static int clock_am13e_on(const struct device *dev, clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(sys);
	return -ENOTSUP;
}

static int clock_am13e_off(const struct device *dev, clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(sys);
	return -ENOTSUP;
}

static int clock_am13e_get_rate(const struct device *dev, clock_control_subsys_t sys,
				uint32_t *rate)
{
	const struct am13e_sys_clock *clk = (const struct am13e_sys_clock *)sys;

	switch (clk->clk) {
	case AM13E_CLOCK_MCLK:
		*rate = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
		break;

	case AM13E_CLOCK_MCLK2:
		*rate = AM13E_MCLK2_HZ;
		break;

	case AM13E_CLOCK_MCLK4:
		*rate = AM13E_MCLK4_HZ;
		break;

	case AM13E_CLOCK_HFCLK:
		*rate = DT_PROP(DT_NODELABEL(hfclk), clock_frequency);
		break;

	case AM13E_CLOCK_SYSPLL0:
#ifdef AM13E_PLL_ENABLED
		*rate = DT_PROP(DT_NODELABEL(syspll), clock_frequency);
#else
		return -ENOTSUP;
#endif
		break;

	case AM13E_CLOCK_SYSPLL1:
#if defined(AM13E_PLL_ENABLED) && DT_NODE_HAS_PROP(DT_NODELABEL(syspll), ti_clk1_div)
		*rate = (uint32_t)(((uint64_t)AM13E_PLL_REF_HZ *
				    (DT_PROP(DT_NODELABEL(syspll), ti_q_div) + 1)) /
				   DT_PROP(DT_NODELABEL(syspll), ti_p_div) /
				   DT_PROP(DT_NODELABEL(syspll), ti_clk1_div));
#else
		return -ENOTSUP;
#endif
		break;

	case AM13E_CLOCK_HSCLK:
		*rate = DT_PROP(DT_NODELABEL(hsclk), clock_frequency);
		break;

	case AM13E_CLOCK_CANCLK:
		return -ENOTSUP;

	case AM13E_CLOCK_SYSOSC:
		*rate = DT_PROP(DT_NODELABEL(sysosc), clock_frequency);
		break;

	default:
		return -ENOTSUP;
	}

	return 0;
}

/*
 * Hardware initialization
 *
 * NOTE: Flash wait states must already be configured before this runs.
 *       soc_early_init_hook() calls FLASH_init() prior to any device init.
 */

static int clock_am13e_init(const struct device *dev)
{
	ARG_UNUSED(dev);

#ifdef AM13E_HFXT_ENABLED
	/* Reset XTAL to a known state before reconfiguring */
	SYSCTL_SET(AM13E_SYSCTL_XTALCR, AM13E_XTALCR_OSCOFF_MASK);

	/* Program XTAL startup time (register unit = 64 us) */
	uint32_t xtaltime = DT_PROP_OR(DT_NODELABEL(hfxt), ti_xtal_startup_delay, 0U);
	SYSCTL_RMW(AM13E_SYSCTL_HFCLKCLKCFG, AM13E_HFCLKCLKCFG_XTALTIME_MASK, xtaltime);

	/* Power up XTAL (clear OSCOFF) */
	SYSCTL_CLR(AM13E_SYSCTL_XTALCR, AM13E_XTALCR_OSCOFF_MASK);

	/* Enable startup monitor and wait for HFCLK to be valid */
	SYSCTL_SET(AM13E_SYSCTL_HFCLKCLKCFG, AM13E_HFCLKCLKCFG_FLTCHK_EN);
	while (!(SYSCTL_RD(AM13E_SYSCTL_CLKSTATUS) & AM13E_CLKSTATUS_HFCLKGOOD_MASK)) {
	} /* TODO: busy-wait loops need ETIMEDOUT logic */
#endif /* AM13E_HFXT_ENABLED */

#ifdef AM13E_PLL_ENABLED
	/* Disable SYSPLL (retained across lower resets; ensure clean state) */
	SYSCTL_CLR(AM13E_SYSCTL_HSCLKEN, AM13E_HSCLKEN_SYSPLLEN_MASK);
	while (!(SYSCTL_RD(AM13E_SYSCTL_CLKSTATUS) & AM13E_CLKSTATUS_SYSPLLOFF_MASK)) {
	}

	/* SYSPLLCFG0: reference source, output enables, output dividers */
	{
		uint32_t cfg0 = AM13E_PLL_REF_BIT;
		uint32_t mask = AM13E_SYSPLLCFG0_SYSPLLREF_MASK | AM13E_SYSPLLCFG0_ENABLECLK0_MASK |
				AM13E_SYSPLLCFG0_ENABLECLK1_MASK | AM13E_SYSPLLCFG0_RDIVCLK0_MASK |
				AM13E_SYSPLLCFG0_RDIVCLK1_MASK;

#if DT_NODE_HAS_PROP(DT_NODELABEL(syspll), ti_clk0_div)
		cfg0 |= AM13E_SYSPLLCFG0_ENABLECLK0_EN;
		cfg0 |= AM13E_RDIV(DT_PROP(DT_NODELABEL(syspll), ti_clk0_div))
			<< AM13E_SYSPLLCFG0_RDIVCLK0_OFS;
#endif
#if DT_NODE_HAS_PROP(DT_NODELABEL(syspll), ti_clk1_div)
		cfg0 |= AM13E_SYSPLLCFG0_ENABLECLK1_EN;
		cfg0 |= AM13E_RDIV(DT_PROP(DT_NODELABEL(syspll), ti_clk1_div))
			<< AM13E_SYSPLLCFG0_RDIVCLK1_OFS;
#endif
		SYSCTL_RMW(AM13E_SYSCTL_SYSPLLCFG0, mask, cfg0);
	}

	/* SYSPLLCFG1: pre-divider (pDiv) and feedback divider (qDiv) */
	{
		uint32_t pdiv = AM13E_PDIV_BITS(DT_PROP(DT_NODELABEL(syspll), ti_p_div));
		uint32_t qdiv =
			(DT_PROP(DT_NODELABEL(syspll), ti_q_div) << AM13E_SYSPLLCFG1_QDIV_OFS) &
			AM13E_SYSPLLCFG1_QDIV_MASK;

		SYSCTL_RMW(AM13E_SYSCTL_SYSPLLCFG1,
			   AM13E_SYSPLLCFG1_PDIV_MASK | AM13E_SYSPLLCFG1_QDIV_MASK, pdiv | qdiv);
	}

	/* Load factory-calibrated PLL startup parameters */
	*(volatile uint32_t *)(AM13E_SYSCTL_BASE + AM13E_SYSCTL_SYSPLLPARAM0) =
		*(volatile uint32_t *)(AM13E_FACTORY_PARAM0_ADDR);
	*(volatile uint32_t *)(AM13E_SYSCTL_BASE + AM13E_SYSCTL_SYSPLLPARAM1) =
		*(volatile uint32_t *)(AM13E_FACTORY_PARAM0_ADDR + 4U);

	/* Enable SYSPLL and wait for lock */
	SYSCTL_SET(AM13E_SYSCTL_HSCLKEN, AM13E_HSCLKEN_SYSPLLEN_EN);
	while (!(SYSCTL_RD(AM13E_SYSCTL_CLKSTATUS) & AM13E_CLKSTATUS_SYSPLLGOOD_MASK)) {
	}

	/* Select SYSPLL as the HSCLK source and wait for HSCLK valid */
	SYSCTL_WR(AM13E_SYSCTL_HSCLKCFG, AM13E_HSCLKCFG_HSCLKSEL_SYSPLL);
	while (!(SYSCTL_RD(AM13E_SYSCTL_CLKSTATUS) & AM13E_CLKSTATUS_HSCLKGOOD_MASK)) {
	}

	/*
	 * Ramp MCLK to full PLL speed in three steps to stay within safe
	 * operating limits during the transition. The intermediate dividers
	 * are fixed by hardware requirements; the final speed is HSCLK as
	 * configured in the device tree.
	 *
	 *  Step 1 – engage /4 divider, switch MCLK source from SYSOSC to HSCLK.
	 *           MCLK = HSCLK / 4.
	 *  Step 2 – step up to /2 divider.
	 *           MCLK = HSCLK / 2.
	 *  Step 3 – remove divider.
	 *           MCLK = HSCLK.
	 */

	/* Step 1: /4 divider + switch to HSCLK */
	SYSCTL_RMW(AM13E_SYSCTL_GENCLKEN,
		   AM13E_GENCLKEN_EXTDIVMCLK_MASK | AM13E_GENCLKEN_MCLKEXTDIVEN_EN,
		   AM13E_GENCLKEN_EXTDIVMCLK_DIV4 | AM13E_GENCLKEN_MCLKEXTDIVEN_EN);

	SYSCTL_SET(AM13E_SYSCTL_MCLKCFG, AM13E_MCLKCFG_USEHSCLK_EN);
	am13e_delay(AM13E_MCLK_SETTLE_CYCLES);
	while (!(SYSCTL_RD(AM13E_SYSCTL_CLKSTATUS) & AM13E_CLKSTATUS_HSCLKMUX_MASK)) {
	}

	/* Step 2: /2 divider */
	SYSCTL_RMW(AM13E_SYSCTL_GENCLKEN,
		   AM13E_GENCLKEN_EXTDIVMCLK_MASK | AM13E_GENCLKEN_MCLKEXTDIVEN_EN,
		   AM13E_GENCLKEN_EXTDIVMCLK_DIV2 | AM13E_GENCLKEN_MCLKEXTDIVEN_EN);

	am13e_delay(AM13E_MCLK_SETTLE_CYCLES);

	/* Step 3: no divider */
	SYSCTL_CLR(AM13E_SYSCTL_GENCLKEN, AM13E_GENCLKEN_MCLKEXTDIVEN_EN);
	am13e_delay(AM13E_MCLK_SETTLE_CYCLES);
#endif /* AM13E_PLL_ENABLED */

	/* Configure MCLK2 / MCLK4 sub-dividers */
	SYSCTL_RMW(AM13E_SYSCTL_MCLKCFG, AM13E_MCLKCFG_MCLKDIVCFG_MASK, AM13E_MCLK_DIV_VAL);

	return 0;
}

/*
 * Device registration
 */

static DEVICE_API(clock_control, clock_am13e_driver_api) = {
	.on = clock_am13e_on,
	.off = clock_am13e_off,
	.get_rate = clock_am13e_get_rate,
};

DEVICE_DT_DEFINE(DT_NODELABEL(ckm), &clock_am13e_init, NULL, NULL, NULL, PRE_KERNEL_1,
		 CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &clock_am13e_driver_api);
