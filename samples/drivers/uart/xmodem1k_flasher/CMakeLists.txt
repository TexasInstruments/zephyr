# Copyright (c) 2025 Siemens Mobility GmbH
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

# The AM2434 can receive binary files via the XModem1k protocol for execution.
# In this case the file needs to be sent without any default header that the
# code in this sample could parse
macro(app_set_runner_args)
  # TODO: This should ideally be board agnostic and only check for the am2434 SoC!
  if(NORMALIZED_BOARD_TARGET STREQUAL "am243x_launchpad_am2434_r5f0_0_boot")
    board_runner_args(xmodem1k_sender "--no-header")
  endif()
endmacro()

find_package(Zephyr)
project(xmodem1k_flasher)

message(WARNING "This sample is not compliant to the XMODEM1k protocol in terms \
of retransmissions and timeouts. Please take a look at the comment inside \
samples/drivers/uart/xmodem1k_flasher/main.c")

target_sources(app PRIVATE src/main.c)
