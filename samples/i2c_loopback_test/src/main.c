/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>

#define TARGET_ADDR 0x50

static char i2c_buf[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static const struct device *i2c_controller = DEVICE_DT_GET(DT_ALIAS(i2c_controller));
static const struct device *i2c_target = DEVICE_DT_GET(DT_ALIAS(i2c_target));

static char target_data[] = "01234567898765432123456789";
static int target_data_ptr = 0;

#define TARGET_RX_BUFLEN 40
static char target_rx_data[TARGET_RX_BUFLEN];
static int target_rx_data_len = 0;

static bool write_requested_send_nack = true;
static bool read_requested_send_nack = true;

int my_stop(struct i2c_target_config *config) {
	if (write_requested_send_nack) {
		write_requested_send_nack = false; // only do it once
		target_rx_data_len = 0;
	}

	if (read_requested_send_nack) {
		target_data_ptr = 0;
		read_requested_send_nack = false;
	}
	printk("\nmy_stop reached\n");
}

int my_write_requested(struct i2c_target_config *config) {
	if (write_requested_send_nack) {
		return -1; /* "An error return shall cause the controller to NACK the next byte received" */
	}
	return 0;
}

int my_write_received(struct i2c_target_config *config, uint8_t data) {
	if (target_rx_data_len == TARGET_RX_BUFLEN) target_rx_data_len = 0;

	target_rx_data[target_rx_data_len++] = data;

	return 0;
}

int my_read_requested(struct i2c_target_config *config, uint8_t *data) {
	if (read_requested_send_nack) {
		return -1;
	}

	if (target_data_ptr < sizeof(target_data)) {
		*data = target_data[target_data_ptr++];
	} else {
		*data = 0;
	}

	return 0;
}

int my_read_processed(struct i2c_target_config *config, uint8_t *data) {
	if (target_data_ptr < sizeof(target_data)) {
		*data = target_data[target_data_ptr++];
	} else {
		*data = 0;
	}

	return 0;
}

static const struct i2c_target_callbacks target_callbacks = {
	.write_requested = my_write_requested,
	.write_received = my_write_received,
	.read_requested = my_read_requested,
	.read_processed = my_read_processed,
	.stop = my_stop,
};

static struct i2c_target_config target_cfg = {
	.address = TARGET_ADDR,
	.callbacks = &target_callbacks,
	//.flags = I2C_TARGET_FLAGS_ADDR_10_BITS,
};

int main(void)
{
	//dl_target_init();

	struct i2c_msg tx = {
		.buf = i2c_buf,
		.len = sizeof(i2c_buf),
		.flags = I2C_MSG_WRITE | I2C_MSG_STOP,
		//.flags = I2C_MSG_READ | I2C_MSG_STOP,
	};

	if (i2c_target_register(i2c_target, &target_cfg) != 0) {
		printk("Error registering target\n");
	}

	if (i2c_configure(i2c_controller, I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_STANDARD)) != 0) {
		printk("Error configuring i2c speed\n");
	}

	int ret = i2c_transfer(i2c_controller, &tx, 1, TARGET_ADDR);

	if (tx.flags & I2C_MSG_READ) {
		printk("received bytes from target: ");
		for (int i=0; i<tx.len; i++) {
			printk("%c, ", tx.buf[i]);
		}
		printk("\n");
	} else {
		printk("received bytes into target: ");
		for (int i=0; i<target_rx_data_len; i++) {
			printk("%c, ", target_rx_data[i]);
		}
		printk("\n");
	}

	printk("tx i2c_transfer returned %d\n", ret);

	/*
	 * Second transfer
	 */
	if (i2c_configure(i2c_controller, I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_STANDARD)) != 0) {
		printk("Error reseting i2c controller\n");
	}
	ret = i2c_transfer(i2c_controller, &tx, 1, TARGET_ADDR);

	if (tx.flags & I2C_MSG_READ) {
		printk("received bytes from target: ");
		for (int i=0; i<tx.len; i++) {
			printk("%c, ", tx.buf[i]);
		}
		printk("\n");
	} else {
		printk("received bytes into target: ");
		for (int i=0; i<target_rx_data_len; i++) {
			printk("%c, ", target_rx_data[i]);
		}
		printk("\n");
	}

	printk("tx i2c_transfer returned %d\n", ret);

	return 0;
}
