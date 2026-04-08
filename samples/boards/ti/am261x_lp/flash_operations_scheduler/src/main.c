/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "flsopskd_wrapper.h"
#include <zephyr/kernel.h>

#include <zephyr/cache.h>

#define FLASH_BASE 0x60000000
#define APP_FLASH_OFFSET 0x200000

#define BUFFER_SIZE 1024U*4

#define COMP_BUF_MATCH 1
#define COMP_BUF_NOMATCH 0

#define BUFFER_FILL true
#define BUFFER_CLEAR false

#define MAX_ITERATIONS 1000

/* app semaphore for write request */
K_SEM_DEFINE(app_write_req_sem, 0, 1);

/* app semaphore for write completion */
K_SEM_DEFINE(app_write_comp_sem, 0, 1);

/* App Array of 8bit data to be written to flash of size 1024 */
uint8_t app_data[BUFFER_SIZE] = {0};

/* empty buffer to read to */
uint8_t read_buffer[BUFFER_SIZE] = {0};

/* pointer to read flash from */
uint8_t *flash_ptr = (uint8_t *)(FLASH_BASE + APP_FLASH_OFFSET);

/* variable to return the flash write success / fail */
int flash_write_ret = 0;

/* util function to populate app_data */
void populate_app_data(bool fill)
{
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        if (fill == BUFFER_FILL) {
            app_data[i] = i;
        } else {
            /* BUFFER_CLEAR */
            app_data[i] = 0;
        }
    }
}

/* util function to write app_data to flash */
void write_app_data_to_flash(void)
{
    while(1){
        /* wait for the write request */
        k_sem_take(&app_write_req_sem, K_FOREVER);

        flsopskd_wrapper_erase(APP_FLASH_OFFSET, BUFFER_SIZE);

        /* Implementation for writing to flash */
        flash_write_ret = flsopskd_wrapper_write(app_data, BUFFER_SIZE, APP_FLASH_OFFSET);

        /* post the semaphore */
        k_sem_give(&app_write_comp_sem);
    }

}

/* util function to read app_data from flash */
void read_app_data_from_flash(uint8_t *buffer, size_t size)
{
    /* Implementation for reading from flash */
    /* Since DAC mode is enabled, we can read directly using the flash_ptr */

    for (size_t i = 0; i < size; i++)
    {
        buffer[i] = flash_ptr[i];
    }

    return;
}

/* compare buffers */
int compare_buffers(uint8_t *buf1, uint8_t *buf2, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        if (buf1[i] != buf2[i])
        {
            return COMP_BUF_NOMATCH; // buffers are not the same
        }
    }
    return COMP_BUF_MATCH; // buffers are the same
}

int main(void)
{

    int buff_match = COMP_BUF_NOMATCH;
    int fill = BUFFER_FILL;
    int error_count = 0;

    /* Check if the OSPI0 is in DAC mode and Flash is enabled
        read the app_data from flash and expect no abort
    */
    read_app_data_from_flash(read_buffer, BUFFER_SIZE);

    /* if no abort, then the read is successful and the OSPI0 is in DAC mode */
    printf("Flash (OSPI0 DAC mode) reads succesfully without abort, OSPI0 is in DAC mode\n");

    buff_match = compare_buffers(read_buffer, flash_ptr, BUFFER_SIZE);

    if (buff_match != COMP_BUF_MATCH)
    {
        printk("Flash read buffer non-zero\n");
    }

    /* init the flsopskd_wrapper */
    flsopskd_wrapper_init();


    for (int iteration = 0; iteration < MAX_ITERATIONS; iteration++)
    {

        if (iteration % 2 == 0) {
            fill = BUFFER_FILL;
        } else {
            fill = BUFFER_CLEAR;
        }

        /* populate app_data */
        populate_app_data(fill);

        /* write flash with app_data. initiate write request */
        k_sem_give(&app_write_req_sem);

        /* wait on the write completion */
        k_sem_take(&app_write_comp_sem, K_FOREVER);

        printk("write completion status : %d | \t", flash_write_ret);

        /* invalidate Data Cache */
        sys_cache_data_invd_range(flash_ptr, BUFFER_SIZE);

        /* read and compare buffers */
        read_app_data_from_flash(read_buffer, BUFFER_SIZE);

        buff_match = compare_buffers(read_buffer, app_data, BUFFER_SIZE);

        if (buff_match != COMP_BUF_MATCH)
        {
            printk("FAILED.    \t fill : %d. Iteration : %d\n", fill, iteration);
            error_count++;
        } else {
            printk("SUCCESS.   \t fill : %d. Iteration : %d\n", fill, iteration);
        }

        k_sleep(K_MSEC(100));
    }

    printk("Test Complete. Number of errors : %d\n", error_count);

	return 0;
}

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

K_THREAD_DEFINE(write_id, STACKSIZE, write_app_data_to_flash, NULL, NULL, NULL,
		PRIORITY, 0, 0);
