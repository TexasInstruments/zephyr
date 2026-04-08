/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "flsopskd_wrapper.h"
#include <zephyr/kernel.h>

/* SDK drivers */
#include <flsopskd.h>

K_MUTEX_DEFINE(flsopskd_write_mutex);

#ifdef CONFIG_FLSOPSKD_INT_MODE

/* Semaphore to pend on */
K_SEM_DEFINE(flsopskd_int_status_sem, 0, 1);

#define FLSOPSKD_IRQ_PRIORITY   CONFIG_FLSOPSKD_INT_PRIO
#define FLSOPSKD_IRQ_FLAGS      IRQ_TYPE_EDGE
#define FLSOPSKD_IRQ_NUM        CONFIG_FLSOPSKD_INT_NUM

/* ISR for posting the semaphore */
void flsopskd_isr(void *args)
{   
    CSL_fss_fota_genregsRegs *pReg = (CSL_fss_fota_genregsRegs *)(CSL_FSS_FOTA_GENREGS_REGS_BASE);

    /* get status */
    volatile uint32_t status = CSL_REG32_RD(&pReg->STS_IRQ.STATUS);
    
    /* Clearing the Interrupt Status */
    CSL_REG32_WR(&pReg->STS_IRQ.EOI, 1);
    CSL_REG32_WR(&pReg->STS_IRQ.STATUS, 1);
    
    if(status != 0) {
        /* Post the semaphore to indicate flash operation completion */
        k_sem_give(&flsopskd_int_status_sem);
    }
    
}

/* Redefinition to a SDK's non-busypoll */
int32_t FLSOPSKD_busyPoll(FLSOPSKD_Handle *pHandle)
{   
    ARG_UNUSED(pHandle);
    
    int ret = k_sem_take(&flsopskd_int_status_sem, K_FOREVER);
    
    if (ret == 0){
        return 0;
    }
    
    /* timeout occurred */
    return -ret;
}
#else 

/* Redefinition to a SDK's non-busypoll */
int32_t FLSOPSKD_busyPoll(FLSOPSKD_Handle *pHandle)
{   
    ARG_UNUSED(pHandle);
    
    CSL_fss_fota_genregsRegs *pReg = (const CSL_fss_fota_genregsRegs *)(CSL_FSS_FOTA_GENREGS_REGS_BASE);

    while(1){
        if (CSL_REG32_RD(&pReg->STS_IRQ.STATUS_RAW) !=0) {
            CSL_REG32_WR(&pReg->STS_IRQ.STATUS, 1);
            break;
        } else {
            /* do nothing */
        }
    }
    
    return 0;
}

#endif /* !(CONFIG_FLSOPSKD_INT_MODE) */


/* Wrapper constants for flash operations - Macronix */
#define EXT_FLASH_ERASE_OPCODE      (0x21U)
#define EXT_FLASH_ERASE_EXTOPCODE   (0xDEU)
#define EXT_FLASH_ERASE_SIZE        (4096U)
#define EXT_FLASH_PAGE_SIZE         (256U)

FLSOPSKD_Handle gFlopsdkHandle;
FLSOPSKD_Params params;

void flsopskd_wrapper_init(void)
{

    FLSOPSKD_Params_init(&params);
    params.eraseOpCode = EXT_FLASH_ERASE_OPCODE;
    params.eraseExOpCode = EXT_FLASH_ERASE_EXTOPCODE;
    params.pageSizeInBytes = EXT_FLASH_PAGE_SIZE;
    params.eraseSizeInBytes = EXT_FLASH_ERASE_SIZE;
    FLSOPSKD_init(&gFlopsdkHandle, &params);

#ifdef CONFIG_FLSOPSKD_INT_MODE

    CSL_fss_fota_genregsRegs *pReg = (CSL_fss_fota_genregsRegs *)(CSL_FSS_FOTA_GENREGS_REGS_BASE);

    CSL_REG32_WR(&pReg->STS_IRQ.STATUS, 1);
    CSL_REG32_WR(&pReg->STS_IRQ.EOI, 1);
    CSL_REG32_WR(&pReg->STS_IRQ.ENABLE_SET, 1);

    /* Register the ISR */
    IRQ_CONNECT(FLSOPSKD_IRQ_NUM, FLSOPSKD_IRQ_PRIORITY, flsopskd_isr, NULL, FLSOPSKD_IRQ_FLAGS);
    irq_enable(FLSOPSKD_IRQ_NUM);

#endif /* !(CONFIG_FLSOPSKD_INT_MODE) */
}

void flsopskd_wrapper_deinit(void)
{
    FLSOPSKD_deinit(&gFlopsdkHandle);

#ifdef CONFIG_FLSOPSKD_INT_MODE

    /* IRQ_DISCONNECT */
    irq_disable(FLSOPSKD_IRQ_NUM);

#endif /* !(CONFIG_FLSOPSKD_INT_MODE) */
}

/* wrapper takes a 8bit array and flash offset address to write to */
int flsopskd_wrapper_write( uint8_t *data, uint32_t length, uint32_t offset)
{   
    volatile uint32_t start_ticks;
    volatile uint32_t stop_ticks;

    /* Basic checks on the args */
    if (data == NULL || length == 0)
    {
        printk("FLSOPSKD Wrapper Write: Invalid data or length\n");
        return -ENOTSUP;
    }

    /* lock mutex */
    k_mutex_lock(&flsopskd_write_mutex, K_FOREVER);

    start_ticks = k_cycle_get_32();

    /* start flash write operations */
    int32_t ret = FLSOPSKD_write(&gFlopsdkHandle, offset, (uint8_t*)data, length);

    stop_ticks = k_cycle_get_32();

    k_mutex_unlock(&flsopskd_write_mutex);

    if (ret != 0)
    {
        printk("FLSOPSKD Wrapper Write: Write operation failed\n");
        return -EIO;
    }

    printk("FLSOPSKD Wrapper Write: Write Operation Success | Ticks : %d\n", stop_ticks - start_ticks);

    return 0;
}

int flsopskd_wrapper_erase(uint32_t offset, uint32_t length)
{   
    int num_pages_to_erase;

    int32_t ret = -ENOTSUP;
    
    /* 
     *  Take offset and length. calculate the number of erase blocks 
     *  and then erase those many blocks 
     */

    /* Basic checks on the args */
    if (length == 0)
    {
        printk("FLSOPSKD Wrapper Erase: Invalid length\n");
        return -ENOTSUP;
    }

    /* check if offset is aligned to minimum block size of flash */
    if (offset % EXT_FLASH_ERASE_SIZE != 0)
    {
        printk("FLSOPSKD Wrapper Erase: Offset is not aligned to erase block size\n");
        return -ENOTSUP;
    }

    /* calc number of pages needed to be erased */
    num_pages_to_erase = (length + EXT_FLASH_ERASE_SIZE - 1) / EXT_FLASH_ERASE_SIZE;

    printk("FLSOPSKD Wrapper Erase: Starting erase from offset 0x%08x to offset 0x%08x\n", offset, offset + num_pages_to_erase * EXT_FLASH_ERASE_SIZE);

    /* for loop on all the erase blocks if the len > erase block size */
    
    /* start flash erase operations */
    for (int page = 0; page < num_pages_to_erase; page++)
    {
        uint32_t page_offset = offset + (page * EXT_FLASH_ERASE_SIZE);

        /* lock mutex */
        k_mutex_lock(&flsopskd_write_mutex, K_FOREVER);

        /* erase flash page */
        ret = FLSOPSKD_erase(&gFlopsdkHandle, offset);

        /* unlock mutex */
        k_mutex_unlock(&flsopskd_write_mutex);

        /* check erase return value and proceed for further erases if successful */
        if (ret != 0)
        {
            printk("FLSOPSKD Wrapper Erase: Erase operation failed at page offset 0x%08x\n", page_offset);
            return -EIO;
        } else {
            printk("FLSOPSKD Wrapper Erase: Successfully erased page at offset 0x%08x\n", page_offset);
        }
    }
    
    if (ret != 0)
    {
        printk("FLSOPSKD Wrapper Erase: Erase operation failed\n");
        return -EIO;
    }

    return 0;
}

int flsopskd_wrapper_write_status(void)
{
    /* 
     * Try to acquire the write mutex and if the mutex is available, 
     * then the write is complete. 
     * Other wise, the write is still in progress 
     */

    if (k_mutex_lock(&flsopskd_write_mutex, K_NO_WAIT) == 0)
    {
        k_mutex_unlock(&flsopskd_write_mutex);
        return 0; /* Write is complete */
    }

    return -EIO; /* Write is still in progress */
}