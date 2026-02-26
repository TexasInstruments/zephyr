#
# Copyright (c) 2026 Texas Instruments Incorporated
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sample_xip_app_remote)

# Include helloworld source code
target_sources(app PRIVATE src/main.c)
