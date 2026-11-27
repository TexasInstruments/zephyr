# Copyright (c) 2026 Texas Instruments Incorporated
#
# SPDX-License-Identifier: Apache-2.0

zephyr_sources(soc.c)
zephyr_sources(ccfg.c)
zephyr_sources(scfg.c)
zephyr_sources(power.c)
zephyr_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_POWEROFF poweroff.c)

if(NOT CONFIG_FLASH AND CONFIG_GPIO)
zephyr_library()
zephyr_library_sources(../lpf3_common/ext_flash_dpd.c)
endif()

zephyr_linker_sources_ifdef(CONFIG_HAS_TI_CCFG SECTIONS ccfg.ld)

zephyr_linker_sources_ifdef(CONFIG_HAS_TI_SCFG SECTIONS scfg.ld)

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")

set_property(GLOBAL APPEND PROPERTY post_build_patch_elf_commands
  # Patch .elf file with correct CRC32 fields
  COMMAND crc_tool patch-image
    --elf ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME}
    --symbol-prefix ti_utils_build_GenMap_sym_CRC_CCFG
    --output ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME}
  )
