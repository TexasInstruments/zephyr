# Copyright (c) 2026 Texas Instruments Incorporated
# SPDX-License-Identifier: Apache-2.0

if("${REMOTE_CORE_BOARD}" STREQUAL "")
	message(
	"Target ${BOARD} not supported for this sample. "
	"There is no remote board selected in Kconfig.sysbuild")
endif()

set(REMOTE_APP remote)

ExternalZephyrProject_Add(
	APPLICATION ${REMOTE_APP}
	SOURCE_DIR	${APP_DIR}/${REMOTE_APP}
	BOARD			 ${SB_CONFIG_REMOTE_CORE_BOARD}
)

# Add dependencies so that the remote sample will be built first
# This is required because some primary cores need information from the
# remote core's build, such as the output image's LMA
add_dependencies(${DEFAULT_IMAGE} ${REMOTE_APP})
sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} ${REMOTE_APP})

# Include the post-processing steps to generate the multi-core ELF image
include(${APP_DIR}/genimage_post_process.cmake)