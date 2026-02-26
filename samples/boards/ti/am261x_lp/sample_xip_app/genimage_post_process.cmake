# Copyright (c) 2026 Texas Instruments Incorporated
# SPDX-License-Identifier: Apache-2.0

# Path to the genimage.py script
if(DEFINED ENV{MCU_PLUS_SDK_PATH})
  set(TI_MCU_SDK_ROOT $ENV{MCU_PLUS_SDK_PATH})
else()
  message(FATAL_ERROR "Environment variable MCU_PLUS_SDK_PATH is not set. Please set it to the root of your MCU+ SDK installation.")
endif()

set(GENIMAGE_SCRIPT "${TI_MCU_SDK_ROOT}/tools/boot/multicore-elf/genimage.py")

# Paths to the built ELF files
set(PRIMARY_ELF_PATH "${CMAKE_BINARY_DIR}/${DEFAULT_IMAGE}/zephyr/zephyr.elf")
set(REMOTE_ELF_PATH "${CMAKE_BINARY_DIR}/${REMOTE_APP}/zephyr/zephyr.elf")
set(OUTPUT_MCELF "${CMAKE_BINARY_DIR}/zephyr_final.mcelf")

# Add custom target for generating the multi-core ELF image
add_custom_target(generate_mcelf ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Generating multi-core ELF image..."
    COMMAND python3 ${GENIMAGE_SCRIPT}
        --core-img=0:${PRIMARY_ELF_PATH}
        --core-img=1:${REMOTE_ELF_PATH}
        --output=${OUTPUT_MCELF}
        --merge-segments=true
        --tolerance-limit=0
        --ignore-context=false
        --xip=0x60100000:0x60800000
        --xlat=none
        --max-segment-size=8192
        --otfaConfigFile=None
    COMMENT "Generating multi-core ELF with genimage.py"
    VERBATIM
)

# Make sure the generate_mcelf target runs after all applications are built
add_dependencies(generate_mcelf ${DEFAULT_IMAGE} ${REMOTE_APP})

# Create a symlink to the generated mcelf in the build directory root for easy access
add_custom_command(
    TARGET generate_mcelf
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Multi-core ELF image created at: ${OUTPUT_MCELF}"
    VERBATIM
)