/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <inc/hw_ccfg.h>

#define CC23X1_TO_PERM_VAL(en) (IS_ENABLED(en) ? CCFG_PERMISSION_ALLOW : CCFG_PERMISSION_FORBID)

#if CONFIG_CC23X1_BLDR_VTOR_TYPE_UNDEF
#define CC23X1_BLDR_VTOR 0xffffffff
#elif CONFIG_CC23X1_BLDR_VTOR_TYPE_FORBID
#define CC23X1_BLDR_VTOR 0xfffffffc
#elif CONFIG_CC23X1_BLDR_VTOR_TYPE_USE_FCFG
#define CC23X1_BLDR_VTOR 0xfffffff0
#else
#define CC23X1_BLDR_VTOR CONFIG_CC23X1_BLDR_VTOR_FLASH
#endif

/* Default CCFG */
const ccfg_t ccfg __attribute__((section(".ti_ccfg"))) __attribute__((used)) = {

	.bootCfg.pBldrVtor = (void *)CC23X1_BLDR_VTOR,

	.bootCfg.bldrParam.serialRomBldrParamStruct = {
		.bldrEnabled = IS_ENABLED(CONFIG_CC23X1_BLDR_ENABLED),
		.serialIoCfgIndex = CONFIG_CC23X1_SERIAL_IO_CFG_INDEX,
		.pinTriggerEnabled = IS_ENABLED(CONFIG_CC23X1_PIN_TRIGGER_ENABLED),
		.pinTriggerDio = CONFIG_CC23X1_PIN_TRIGGER_DIO,
		.pinTriggerLevel = IS_ENABLED(CONFIG_CC23X1_PIN_TRIGGER_LEVEL_HI),
	},

	.bootCfg.pAppVtor = (void *)CONFIG_CC23X1_P_APP_VTOR,

	.INTERNAL_FIELD_0,

	.permissions.allowDebugPort = CC23X1_TO_PERM_VAL(CONFIG_CC23X1_ALLOW_DEBUG_PORT),
	.permissions.allowEnergyTrace = CC23X1_TO_PERM_VAL(CONFIG_CC23X1_ALLOW_ENERGY_TRACE),
	.permissions.allowFlashVerify = CC23X1_TO_PERM_VAL(CONFIG_CC23X1_ALLOW_FLASH_VERIFY),
	.permissions.allowFlashProgram = CC23X1_TO_PERM_VAL(CONFIG_CC23X1_ALLOW_FLASH_PROGRAM),
	.permissions.allowChipErase = CC23X1_TO_PERM_VAL(CONFIG_CC23X1_ALLOW_CHIP_ERASE),
	.permissions.allowToolsClientMode =
		CC23X1_TO_PERM_VAL(CONFIG_CC23X1_ALLOW_TOOLS_CLIENT_MODE),
	.permissions.allowReturnToFactory =
		CC23X1_TO_PERM_VAL(CONFIG_CC23X1_ALLOW_RETURN_TO_FACTORY),
	.permissions.allowFakeStby = CCFG_PERMISSION_ALLOW,

	.misc.saciTimeoutOverride = IS_ENABLED(CONFIG_CC23X1_SACI_TIMEOUT_OVERRIDE),
	.misc.saciTimeoutExp = CONFIG_CC23X1_SACI_TIMEOUT_EXP,
	.misc.allowMainAppErase = CC23X1_TO_PERM_VAL(CONFIG_CC23X1_ALLOW_MAIN_APP_ERASE),

	.flashProt.writeEraseProt.mainSectors0_31 = CONFIG_CC23X1_WR_ER_PROT_SECT0_31,
	.flashProt.writeEraseProt.mainSectors32_255 = CONFIG_CC23X1_WR_ER_PROT_SECT32_255,
	.flashProt.writeEraseProt.mainSectors256_511 = CONFIG_CC23X1_WR_ER_PROT_SECT256_511,

	.flashProt.writeEraseProt.ccfgSector = CONFIG_CC23X1_WR_ER_PROT_CCFG_SECT,
	.flashProt.writeEraseProt.fcfgSector = CONFIG_CC23X1_WR_ER_PROT_FCFG_SECT,
	.flashProt.writeEraseProt.engrSector = CONFIG_CC23X1_WR_ER_PROT_ENGR_SECT,
	.flashProt.writeEraseProt.scfgSector = CONFIG_CC23X1_WR_ER_PROT_SCFG_SECT,
	.flashProt.writeEraseProt.vlogSector = CONFIG_CC23X1_WR_ER_PROT_VLOG_SECT,

	.flashProt.readProt.mainSectors = CONFIG_CC23X1_READ_PROT_MAIN_SEC,
	.flashProt.readProt.ccfgSector = CONFIG_CC23X1_READ_PROT_CCFG_SEC,

	.flashProt.chipEraseRetain.mainSectors0_31 = CONFIG_CC23X1_CHIP_ER_RETAIN_SECT0_31,
	.flashProt.chipEraseRetain.mainSectors32_255 = CONFIG_CC23X1_CHIP_ER_RETAIN_SECT32_255,
	.flashProt.chipEraseRetain.mainSectors256_511 = CONFIG_CC23X1_CHIP_ER_RETAIN_SECT256_511,

	.debugCfg.authorization = CCFG_DBGAUTH_DBGOPEN,
};
