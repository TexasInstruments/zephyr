# Copyright (c) 2023, Meta
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_TISCI ti_sci.c)