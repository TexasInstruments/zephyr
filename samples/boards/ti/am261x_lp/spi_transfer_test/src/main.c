/*
 * Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <zephyr/cache.h>

LOG_MODULE_REGISTER(spi_loopback, LOG_LEVEL_INF);

#define SPI_DEV_NODE DT_ALIAS(spi_dut)
#if !DT_NODE_HAS_STATUS_OKAY(SPI_DEV_NODE)
#error "Unsupported board: spi_dut devicetree alias is not defined"
#endif
#define BUFFER_SIZE 32

/* Test patterns for loopback verification */
static uint8_t test_patterns[][8]
	__aligned(CONFIG_DCACHE_LINE_SIZE) = {{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
					      {0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8},
					      {0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A},
					      {0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF}};

static uint8_t __aligned(CONFIG_DCACHE_LINE_SIZE) tx_buf[4][BUFFER_SIZE];
static uint8_t __aligned(CONFIG_DCACHE_LINE_SIZE) rx_buf[4][BUFFER_SIZE];

/**
 * @brief Perform a single SPI loopback test with a specific pattern
 *
 * @param spi_dev Pointer to the SPI device
 * @param pattern Pointer to test pattern array
 * @param pattern_len Length of the test pattern
 * @param cb Callback function to be called upon transfer completion
 * @param sem Pointer to semaphore for synchronization with callback
 * @param thread_num Identifier of the calling thread (for logging)
 * @return 0 on success (pattern verified), negative on error, positive on pattern mismatch
 */
static int spi_loopback_test_pattern(const struct device *spi_dev, const uint8_t *pattern,
				     size_t pattern_len,
				     void (*cb)(const struct device *, int, void *),
				     struct k_sem *sem, int thread_num)
{
	k_sem_reset(sem);

	uint8_t *tx_buffer = tx_buf[thread_num];
	uint8_t *rx_buffer = rx_buf[thread_num];
	int ret;

	/* Basic SPI configuration for loopback test - no CS needed */
	struct spi_config spi_cfg = {
		.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_LOOP,
		.frequency = 25000000, /* 25 MHz */
	};

	/* Fill transmit buffer with the pattern (possibly repeating) */
	for (int i = 0; i < BUFFER_SIZE; i++) {
		tx_buffer[i] = pattern[i % pattern_len];
	}
	sys_cache_data_flush_range((void *)tx_buffer, sizeof(tx_buffer));

	/* Clear receive buffer */
	memset(rx_buffer, 0, BUFFER_SIZE);
	sys_cache_data_flush_range((void *)rx_buffer, sizeof(rx_buffer));

	/* Set up SPI buffers */
	struct spi_buf tx_buf = {
		.buf = tx_buffer,
		.len = BUFFER_SIZE,
	};

	struct spi_buf rx_buf = {
		.buf = rx_buffer,
		.len = BUFFER_SIZE,
	};

	struct spi_buf_set tx_bufs = {
		.buffers = &tx_buf,
		.count = 1,
	};

	struct spi_buf_set rx_bufs = {
		.buffers = &rx_buf,
		.count = 1,
	};

	/* Perform transceive operation */
	LOG_INF("Thread %d: Testing pattern with first byte: 0x%02X", thread_num, pattern[0]);

	ret = spi_transceive_cb(spi_dev, &spi_cfg, &tx_bufs, &rx_bufs, cb, (void *)sem);
	k_sem_take(sem, K_FOREVER);

	sys_cache_data_invd_range((void *)tx_buffer, sizeof(tx_buffer));
	sys_cache_data_invd_range((void *)rx_buffer, sizeof(rx_buffer));

	if (ret) {
		LOG_ERR("Thread %d: SPI transceive failed: %d", thread_num, ret);
		return ret;
	}

	/* Verify received data matches transmitted data */
	bool pattern_match = true;

	for (int i = 0; i < BUFFER_SIZE; i++) {
		if (rx_buffer[i] != tx_buffer[i]) {
			pattern_match = false;
			LOG_ERR("Thread %d: Data mismatch at index %d: TX=0x%02X, RX=0x%02X",
				thread_num, i, tx_buffer[i], rx_buffer[i]);
			/* Continue checking to show all mismatches */
		}
	}

	if (pattern_match) {
		LOG_INF("Thread %d:Pattern test PASSED - Loopback successful", thread_num);

		LOG_INF("Thread %d: Transmitted/received data (first 8 bytes):", thread_num);
		for (int i = 0; i < 8 && i < BUFFER_SIZE; i++) {
			printk("0x%02X ", rx_buffer[i]);
		}
		printk("\n");
		return 0;
	}
	LOG_ERR("Thread %d: Pattern test FAILED - Loopback not functioning correctly", thread_num);
	return 1;
}

/* Threads based test */
#define THREAD_STACK_SIZE 1024

struct k_thread task1, task2, task3, task4;
K_THREAD_STACK_DEFINE(task1_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(task2_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(task3_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(task4_stack, THREAD_STACK_SIZE);

K_SEM_DEFINE(sem1, 0, 1);
K_SEM_DEFINE(sem2, 0, 1);
K_SEM_DEFINE(sem3, 0, 1);
K_SEM_DEFINE(sem4, 0, 1);

/* Thread completion tracking */
/* -1 = not run, 0 = fail, 1 = pass */
static int thread_test_results[4] = {-1, -1, -1, -1};

void call_back_task1(const struct device *spi_dev, int status, void *user_data)
{
	if (status >= 0) {
		printk("Test Callback activated. SPI transfer completed successfully!\n");
	} else {
		printk("Test Callback activated. SPI transfer failed with status %d\n", status);
	}
	k_sem_give(&sem1);
}
void call_back_task2(const struct device *spi_dev, int status, void *user_data)
{
	if (status >= 0) {
		printk("Test Callback activated. SPI transfer completed successfully!\n");
	} else {
		printk("Test Callback activated. SPI transfer failed with status %d\n", status);
	}
	k_sem_give(&sem2);
}
void call_back_task3(const struct device *spi_dev, int status, void *user_data)
{
	if (status >= 0) {
		printk("Test Callback activated. SPI transfer completed successfully!\n");
	} else {
		printk("Test Callback activated. SPI transfer failed with status %d\n", status);
	}
	k_sem_give(&sem3);
}
void call_back_task4(const struct device *spi_dev, int status, void *user_data)
{
	if (status >= 0) {
		printk("Test Callback activated. SPI transfer completed successfully!\n");
	} else {
		printk("Test Callback activated. SPI transfer failed with status %d\n", status);
	}
	k_sem_give(&sem4);
}

static void main_function(void *sem, void *cb, void *thread_id_ptr)
{
	const struct device *spi_dev;
	int test_result = 0;
	int thread_id = (int)thread_id_ptr; /* Each thread has unique ID 0-3 */

	/* Get SPI device */
	spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);
	if (!device_is_ready(spi_dev)) {
		LOG_ERR("Thread %d: SPI device not ready", thread_id);
		return;
	}

	LOG_INF("SPI Loopback Test Started (Thread %d)", thread_id);

	/* Each thread tests ONE specific pattern using its own dedicated buffer */
	printk("\n");
	LOG_INF("Running test pattern %d", thread_id + 1);

	/* Run multiple iterations to stress test */
	int iterations = 4;
	int passed = 0;

	for (int iter = 0; iter < iterations; iter++) {
		test_result = spi_loopback_test_pattern(spi_dev, test_patterns[thread_id],
							ARRAY_SIZE(test_patterns[thread_id]), cb,
							(struct k_sem *)sem, thread_id);

		if (test_result == 0) {
			passed++;
		}

		/* Add delay between iterations */
		k_sleep(K_MSEC(500));
	}

	/* Print overall results */
	printk("\n");
	LOG_INF("SPI Loopback Test Summary (Thread %d):", thread_id);
	LOG_INF("%d of %d iterations passed for pattern %d", passed, iterations, thread_id + 1);

	if (passed == iterations) {
		LOG_INF("OVERALL RESULT: PASS - Loopback functioning correctly");
		thread_test_results[thread_id] = 1; /* Pass */
	} else {
		LOG_INF("OVERALL RESULT: FAIL - Loopback issues detected");
		thread_test_results[thread_id] = 0; /* Fail */
	}

	LOG_INF("Thread %d: Test complete - Exiting", thread_id);
}

int main(void)
{
	k_thread_create(&task1, task1_stack, THREAD_STACK_SIZE, main_function, &sem1,
			&call_back_task1, (void *)0, /* Thread 0 tests pattern 0 */
			-1, K_USER, K_MSEC(3));

	k_thread_create(&task2, task2_stack, THREAD_STACK_SIZE, main_function, &sem2,
			&call_back_task2, (void *)1, /* Thread 1 tests pattern 1 */
			-1, K_USER, K_MSEC(2));

	k_thread_create(&task3, task3_stack, THREAD_STACK_SIZE, main_function, &sem3,
			&call_back_task3, (void *)2, /* Thread 2 tests pattern 2 */
			-1, K_USER, K_MSEC(1));

	k_thread_create(&task4, task4_stack, THREAD_STACK_SIZE, main_function, &sem4,
			&call_back_task4, (void *)3, /* Thread 3 tests pattern 3 */
			-1, K_USER, K_MSEC(4));

	/* Wait for all threads to complete and exit */
	k_thread_join(&task1, K_FOREVER);
	k_thread_join(&task2, K_FOREVER);
	k_thread_join(&task3, K_FOREVER);
	k_thread_join(&task4, K_FOREVER);

	/* Collect results and print final summary */
	int total_passed = 0;

	for (int i = 0; i < 4; i++) {
		if (thread_test_results[i] == 1) {
			total_passed++;
		}
	}

	printk("\n");
	printk("========================================\n");
	printk("FINAL TEST SUMMARY\n");
	printk("========================================\n");
	printk("Threads passed: %d / 4\n", total_passed);

	if (total_passed == 4) {
		printk("RESULT: ALL TESTS PASSED\n");
	} else {
		printk("RESULT: SOME TESTS FAILED\n");
	}
	printk("========================================\n");

	return 0;
}
