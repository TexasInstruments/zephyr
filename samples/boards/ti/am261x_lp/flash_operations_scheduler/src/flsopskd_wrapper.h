/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file flsopskd_wrapper.h
 * @brief Wrapper functions for FLSOPSKD flash operations from MCU_PLUS_SDK
 * 
 */

#include <stdio.h>
#include <stdint.h>

/**
 * @brief API to initilize flsopskd wrapper module
 * 
 * @return * void 
 */
void flsopskd_wrapper_init(void);

/**
 * @brief API to de-initialize flsopskd wrapper module
 * 
 */
void flsopskd_wrapper_deinit(void);

/**
 * @brief API to write to flash using flsopskd wrapper module.
 * 
 * CONFIG_BUSY_POLL will determine whether the API will use 
 * busy polling or interrupt based mechanism to track the write completion.
 * 
 * Use flsopskd_wrapper_write_status API to track the write status when using busy polling mechanism.
 * 
 * @param data 
 * @param length 
 * @param offset 
 * @return * void 
 */
int flsopskd_wrapper_write( uint8_t *data, uint32_t length, uint32_t offset);

/**
 * @brief API to erase flash using flsopskd wrapper module.
 * 
 * @param offset 
 * @param length 
 * @return * void 
 */
int flsopskd_wrapper_erase(uint32_t offset, uint32_t length);

/**
 * @brief 
 * 
 * @return int 
 */
int flsopskd_wrapper_write_status(void);


