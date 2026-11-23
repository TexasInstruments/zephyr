# Copyright (c) 2025-2026 Conclusive Engineering Sp. z o.o.
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_DT_HAS_TI_CC33XX_SPI_ENABLED AND CONFIG_DT_HAS_TI_CC33XX_SDIO_ENABLED)
  message(FATAL_ERROR "ti,cc33xx-spi and ti,cc33xx-sdio nodes cannot both be enabled: "
                      "the SPI and SDIO adapters export the same SDK hooks. "
                      "Enable only one CC33xx transport.")
endif()

zephyr_library_sources_ifdef(CONFIG_WIFI_TI_CC3XXX ti_cc3xxx_wifi.c ti_cc3xxx_heap.c)
zephyr_library_sources_ifdef(CONFIG_DT_HAS_TI_CC33XX_SPI_ENABLED ti_cc33xx_spi.c)
zephyr_library_sources_ifdef(CONFIG_DT_HAS_TI_CC33XX_SDIO_ENABLED ti_cc33xx_sdio.c)

zephyr_library_include_directories(.)
