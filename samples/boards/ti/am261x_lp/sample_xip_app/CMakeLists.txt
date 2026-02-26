#
# Copyright (c) 2026 Texas Instruments Incorporated
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

set(REMOTE_ZEPHYR_DIR ${CMAKE_CURRENT_BINARY_DIR}/../remote/zephyr)

if(NOT (CONFIG_BOARD_AM261X_LP_AM2612_R5F0_0))
	message("${BOARD} is not supported for this sample")
endif()

project(sample_xip_app)

if(CONFIG_INCLUDE_REMOTE_DIR)
	target_include_directories(zephyr_interface
		INTERFACE ${REMOTE_ZEPHYR_DIR}/include/public)
endif()

# Include blinky source code
target_sources(app PRIVATE src/main.c)

include(ExternalProject)
