# Copyright (c) 2025 Siemens Mobility GmbH
#
# SPDX-License-Identifier: Apache-2.0

if(NORMALIZED_BOARD_TARGET STREQUAL "am243x_launchpad_am2434_r5f0_0_boot")
  board_runner_args(xmodem1k_sender "--flash-offset=0x0" "--file=${PROJECT_BINARY_DIR}/zephyr.k3_rom_loadable.bin")
  include(${ZEPHYR_BASE}/boards/common/xmodem1k_sender.board.cmake)
elseif(NORMALIZED_BOARD_TARGET STREQUAL "am243x_launchpad_am2434_r5f0_0")
  board_runner_args(xmodem1k_sender "--flash-offset=0x80000" "--file=${PROJECT_BINARY_DIR}/zephyr.signed.bin")
  include(${ZEPHYR_BASE}/boards/common/xmodem1k_sender.board.cmake)
endif()
