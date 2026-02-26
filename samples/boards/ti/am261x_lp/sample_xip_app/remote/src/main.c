/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <stdio.h>

int main(void)
{
	printf("Remote Core Hello World Started!\n");

	while (1) {
		printf("Hello World from Remote Core! (%s)\n", CONFIG_BOARD_TARGET);
		k_msleep(2000); /* Print every 2 seconds */
	}

	return 0;
}
