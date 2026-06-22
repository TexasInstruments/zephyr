/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>

/* SPI config */
#define SPI_NODE DT_ALIAS(test_unicomm)

static const struct device *spi_dev = DEVICE_DT_GET(SPI_NODE);
static const struct spi_config spi_cfg = {
	.frequency = 1000000,
	.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPHA,
	.slave = 0,
};

#define ALPHABET_LEN 26

int main(void)
{
	int ret;

	uint8_t tx_buffer[ALPHABET_LEN];
	uint8_t rx_buffer[ALPHABET_LEN];

	printk("Starting SPI Alphabet Transfer Sample\n");

	/* Verify that the SPI device is ready */
	if (!device_is_ready(spi_dev)) {
		printk("SPI device not ready\n");
		return -ENODEV;
	}

	/* Initialize the transmit buffer with the uppercase alphabet */
	for (int i = 0; i < ALPHABET_LEN; i++) {
		tx_buffer[i] = 'A' + i;
	}

    /* Set up the SPI buffer structures */
    const struct spi_buf tx_buf = {
        .buf = tx_buffer,
        .len = sizeof(tx_buffer)
    };
    const struct spi_buf_set tx_bufs = {
        .buffers = &tx_buf,
        .count = 1
    };

    struct spi_buf rx_buf = {
        .buf = rx_buffer,
        .len = sizeof(rx_buffer)
    };
    const struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1
    };

    while (1) {
        printk("Transmitting alphabet ('A'-'Z')... ");

        /* Perform synchronous full-duplex SPI transfer */
        ret = spi_transceive(spi_dev, &spi_cfg, &tx_bufs, &rx_bufs);
        if (ret < 0) {
            printk("SPI transceive failed with error: %d", ret);
        } else {
            printk("Transfer complete.\n");
            /* If MISO is tied to MOSI (loopback), rx_buffer will hold the alphabet */
	    printk("Received data: ");
	    for (int j=0; j<ALPHABET_LEN; j++) {
		    printk("%c,", rx_buffer[j]);
		}
	    printk("\n");
        }

        /* Wait 2 seconds before sending again */
        k_msleep(2000);
    }

	return 0;
}
