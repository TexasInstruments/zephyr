#include <dl_sysctl.h>
#include <dl_fri.h>

static const DL_SYSCTL_SYSPLLConfig gSYSPLLConfig = {
	.sysPLLRef = DL_SYSCTL_SYSPLL_REF_HFCLK,
	.inputFreq = DL_SYSCTL_SYSPLL_INPUT_FREQ_8_16_MHZ,
	.pDiv = DL_SYSCTL_SYSPLL_PDIV_2,
	.qDiv = 31,
	.enableCLK1 = DL_SYSCTL_SYSPLL_CLK1_ENABLE,
	.enableCLK0 = DL_SYSCTL_SYSPLL_CLK0_ENABLE,
	.rDivClk1 = DL_SYSCTL_SYSPLL_RDIVCLK1_DIV2,
	.rDivClk0 = DL_SYSCTL_SYSPLL_RDIVCLK0_DIV2,
};

void SysPLL_Init(void)
{
	DL_SYSCTL_setHFCLKSourceXTAL(255, true);
	DL_SYSCTL_configSYSPLL((DL_SYSCTL_SYSPLLConfig *)&gSYSPLLConfig);

	/* Before switching to PLL output, step down to a lower frequency and gradually increase */
	DL_SYSCTL_enablePLLDivider(DL_SYSCTL_PLL_DIVIDER_DIV4);
	DL_SYSCTL_switchMCLKfromSYSOSCtoHSCLK(DL_SYSCTL_HSCLK_SOURCE_SYSPLL);
	DL_SYSCTL_enablePLLDivider(DL_SYSCTL_PLL_DIVIDER_DIV2);
	DL_Common_delayCycles(20);
	while ((DL_SYSCTL_getClockStatus() & SYSCTL_CLKSTATUS_HSCLKMUX_MASK) !=
	       DL_SYSCTL_CLK_STATUS_MCLK_SOURCE_HSCLK)
		;
	DL_SYSCTL_disablePLLDivider();
	DL_Common_delayCycles(20);
	while ((DL_SYSCTL_getClockStatus() & SYSCTL_CLKSTATUS_HSCLKMUX_MASK) !=
	       DL_SYSCTL_CLK_STATUS_MCLK_SOURCE_HSCLK)
		;

	DL_SYSCTL_setMCLKDivider(DL_SYSCTL_MCLK_DIV_2_DIV_4);
	DL_SYSCTL_setCANCLKSource(DL_SYSCTL_CANCLK_SOURCE_SYSPLL_DIV2);
}

void FLASH_init(void)
{
	/* Disable cache before changing wait states */
	DL_FRI_disableDLB();
	DL_FRI_disableCache();

	/* Set the flash wait states */
	DL_FRI_setReadWaitStates(0x3);

	/* Enable cache to improve performance of code executed from flash */
	DL_FRI_enableDLB();
	DL_FRI_enableCache();
}

void soc_early_init_hook(void)
{
	FLASH_init();
	SysPLL_Init();
}
