# Copyright (c) 2023 Enphase Energy
# Copyright (c) 2025 Siemens Mobility GmbH
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_sources(common/ctrl_partitions.c)

if(CONFIG_SOC_AM6234_A53 OR CONFIG_SOC_AM6232_A53 OR CONFIG_SOC_AM6254_A53)
  zephyr_sources_ifdef(CONFIG_ARM_MMU a53/mmu_regions.c)

  set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm64/scripts/linker.ld CACHE INTERNAL "")
elseif(CONFIG_SOC_SERIES_AM6X_M4)
  zephyr_sources(m4/soc.c)

  zephyr_include_directories(m4)

  if(CONFIG_OPENAMP_RSC_TABLE)
    zephyr_linker_section(NAME .resource_table GROUP ROM_REGION NOINPUT)
    zephyr_linker_section_configure(SECTION .resource_table KEEP INPUT ".resource_table*")
  endif()

  set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/m4/linker.ld CACHE INTERNAL "")
elseif(CONFIG_SOC_SERIES_AM6X_R5)
  zephyr_sources(r5/soc.c)
  zephyr_sources_ifdef(CONFIG_ARM_MPU r5/arm_mpu_regions.c)

  zephyr_include_directories(r5)

  if(CONFIG_OPENAMP_RSC_TABLE)
    zephyr_linker_section(NAME .resource_table GROUP ROM_REGION NOINPUT)
    zephyr_linker_section_configure(SECTION .resource_table KEEP INPUT ".resource_table*")
  endif()

  set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/r5/linker.ld CACHE INTERNAL "")

  # Generate binary that the ROM bootloader on TI K3 SoCs can boot (using the combined bootflow)
  if (CONFIG_BUILD_OUTPUT_TI_K3_ROM_LOADABLE)
    set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
      COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/scripts/support/ti-k3-rom-image-gen.py
              "--user-firmware-path" ${PROJECT_BINARY_DIR}/${KERNEL_BIN_NAME}
              "--sysfw-blob-path" ${CONFIG_TI_K3_SYSFW_BLOB_PATH}
              "--sysfw-cert-path" ${CONFIG_TI_K3_SYSFW_CERT_PATH}
              "--boardconfig-binary-path" ${CONFIG_TI_K3_BOARDCONFIG_PATH}
              "--private-key-path" ${CONFIG_TI_K3_PRIVATE_KEY_PATH}
              "--user-firmware-load-address" ${CONFIG_SRAM_BASE_ADDRESS}
              "--sysfw-blob-load-address" ${CONFIG_TI_K3_SYSFW_LOAD_ADDR}
              "--working-directory" ${PROJECT_BINARY_DIR}
              "--output-file-name" "zephyr.k3_rom_loadable.bin"
      )
  endif()

endif()
