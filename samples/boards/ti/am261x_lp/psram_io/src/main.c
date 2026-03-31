/*
 * Copyright (c) 2025 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/cache.h>
#include <zephyr/devicetree.h>
#include <string.h>

#define TEST_ARRAY_LEN 10

/* PSRAM memory region accessed from device tree alias
 * The 'psram' alias can be overridden in board overlays for device variants
 */
#define PSRAM_BASE_ADDR DT_REG_ADDR(DT_ALIAS(psram))
#define PSRAM_SIZE      DT_REG_SIZE(DT_ALIAS(psram))

/* Offset for direct pointer access test to avoid linker section overlap */
#define PSRAM_POINTER_OFFSET 0x1000

/* Source data in SRAM - initialized with test pattern */
static uint32_t sram_src_data[TEST_ARRAY_LEN] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0x87654321,
						 0xABCDEF00, 0x00FEDCBA, 0x55AA55AA, 0xAA55AA55,
						 0x11223344, 0x99887766};

/* Readback buffer in SRAM - uninitialized */
static uint32_t sram_readback_data[TEST_ARRAY_LEN];

/* PSRAM destination - placed in PSRAM memory region via code relocation */
static uint32_t psram_dst_data[TEST_ARRAY_LEN];

static void print_array(const char *name, const uint32_t *array, size_t len)
{
	printk("%s:\n", name);
	for (size_t i = 0; i < len; i++) {
		printk("  [%zu] = 0x%08X\n", i, array[i]);
	}
}

static bool compare_arrays(const uint32_t *arr1, const uint32_t *arr2, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (arr1[i] != arr2[i]) {
			printk("ERROR: Mismatch at index %zu: 0x%08X != 0x%08X\n", i, arr1[i],
			       arr2[i]);
			return false;
		}
	}
	return true;
}

int main(void)
{
	bool test_passed = true;

	printk("\n");
	printk("===========================================\n");
	printk("  AM261x PSRAM I/O Sample Application\n");
	printk("===========================================\n\n");

	/* Display memory locations */
	printk("Memory Layout:\n");
	printk("  SRAM Source:      %p\n", (void *)sram_src_data);
	printk("  PSRAM Destination: %p\n", (void *)psram_dst_data);
	printk("  SRAM Readback:    %p\n", (void *)sram_readback_data);
	printk("  PSRAM Base Addr:  0x%08lX (Size: 0x%lX bytes)\n\n",
	       (unsigned long)PSRAM_BASE_ADDR, (unsigned long)PSRAM_SIZE);

	/* Verify PSRAM address is in expected range */
	if ((uintptr_t)psram_dst_data < PSRAM_BASE_ADDR ||
	    (uintptr_t)psram_dst_data >= (PSRAM_BASE_ADDR + PSRAM_SIZE)) {
		printk("ERROR: PSRAM array not in expected memory region!\n");
		printk("  Expected: 0x%08X - 0x%08X\n", PSRAM_BASE_ADDR,
		       PSRAM_BASE_ADDR + PSRAM_SIZE);
		printk("  Got:      %p\n", (void *)psram_dst_data);
		return -1;
	}

	printk("Source Data (SRAM):\n");
	print_array("sram_src_data", sram_src_data, TEST_ARRAY_LEN);
	printk("\n");

	/* Test 1: Write from SRAM to PSRAM using linker-placed array */
	printk("------------------------------------------\n");
	printk("Test 1: Write SRAM -> PSRAM (linker section)\n");
	printk("------------------------------------------\n");

	/* Initialize PSRAM with zeros first */
	memset(psram_dst_data, 0, sizeof(psram_dst_data));

	/* Flush cache to ensure zeros are written to PSRAM */
#ifdef CONFIG_CACHE_MANAGEMENT
	sys_cache_data_flush_range((void *)psram_dst_data, sizeof(psram_dst_data));
#endif

	/* Copy data from SRAM to PSRAM */
	memcpy(psram_dst_data, sram_src_data, sizeof(sram_src_data));

	/* Flush cache to ensure data is written to PSRAM */
#ifdef CONFIG_CACHE_MANAGEMENT
	sys_cache_data_flush_range((void *)psram_dst_data, sizeof(psram_dst_data));
#endif

	printk("Data written to PSRAM at %p\n", (void *)psram_dst_data);

	/* Test 2: Read back from PSRAM to SRAM */
	printk("\n------------------------------------------\n");
	printk("Test 2: Read PSRAM -> SRAM (readback)\n");
	printk("------------------------------------------\n");

	/* Clear readback buffer */
	memset(sram_readback_data, 0, sizeof(sram_readback_data));

	/* Invalidate cache to ensure we read from PSRAM */
#ifdef CONFIG_CACHE_MANAGEMENT
	sys_cache_data_invd_range((void *)psram_dst_data, sizeof(psram_dst_data));
#endif

	/* Read from PSRAM to SRAM */
	memcpy(sram_readback_data, psram_dst_data, sizeof(psram_dst_data));

	printk("Data read back from PSRAM\n\n");

	/* Verify data integrity */
	printk("Readback Data (SRAM):\n");
	print_array("sram_readback_data", sram_readback_data, TEST_ARRAY_LEN);
	printk("\n");

	printk("------------------------------------------\n");
	printk("Verification: Compare Source vs Readback\n");
	printk("------------------------------------------\n");

	if (compare_arrays(sram_src_data, sram_readback_data, TEST_ARRAY_LEN)) {
		printk("PASS: All data matches!\n");
	} else {
		printk("FAIL: Data mismatch detected!\n");
		test_passed = false;
	}

	/* Test 3: Direct pointer access to PSRAM */
	printk("\n------------------------------------------\n");
	printk("Test 3: Direct Pointer Access to PSRAM\n");
	printk("------------------------------------------\n");

	printk("Writing via direct pointer to 0x%08X\n", PSRAM_BASE_ADDR);

	/* Write using direct pointer (offset to avoid overwriting linker section) */
	volatile uint32_t *psram_test =
		(volatile uint32_t *)(PSRAM_BASE_ADDR + PSRAM_POINTER_OFFSET);

	for (size_t i = 0; i < TEST_ARRAY_LEN; i++) {
		psram_test[i] = sram_src_data[i] ^ 0xFFFFFFFF; /* Write inverted data */
	}

	/* Flush writes */
#ifdef CONFIG_CACHE_MANAGEMENT
	sys_cache_data_flush_range((void *)psram_test, TEST_ARRAY_LEN * sizeof(uint32_t));
#endif

	printk("Reading back via direct pointer...\n");

	/* Invalidate before read */
#ifdef CONFIG_CACHE_MANAGEMENT
	sys_cache_data_invd_range((void *)psram_test, TEST_ARRAY_LEN * sizeof(uint32_t));
#endif

	/* Read back and verify */
	bool direct_test_passed = true;
	for (size_t i = 0; i < TEST_ARRAY_LEN; i++) {

		uint32_t expected = sram_src_data[i];

		uint32_t actual = psram_test[i];

		if (actual & expected) {
			printk("ERROR: Direct access mismatch at [%zu]: "
			       "expected 0x%08X, got 0x%08X\n",
			       i, expected, actual);
			direct_test_passed = false;
		}
	}

	if (direct_test_passed) {
		printk("PASS: Direct pointer access test passed!\n");
	} else {
		printk("FAIL: Direct pointer access test failed!\n");
		test_passed = false;
	}

	/* Final Results */
	printk("\n");
	printk("===========================================\n");
	if (test_passed) {
		printk("  ALL TESTS PASSED!\n");
	} else {
		printk("  SOME TESTS FAILED!\n");
	}
	printk("===========================================\n");

	return test_passed ? 0 : -1;
}
