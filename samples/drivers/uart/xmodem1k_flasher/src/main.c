/*
 * Copyright (c) 2025 Siemens Mobility GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * WARNING / TODO:
 *
 * This is only a minimal implementation of the XMODEM1k protocol that isn't
 * compliant with retransfers and timeouts. Checksums are verified and if an
 * error is detected a "transmission cancelled" byte it continuously transferred.
 *
 * THIS CODE IS NOT READY TO BE SUBMITTED UPSTREAM YET AND IS ONLY PUBLIC AS
 * WORK-IN-PROGRESS / PROOF-OF-CONCEPT!
 */

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#define SOH 0x01
#define STX 0x02
#define EOT 0x04

#define PING 0x43
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18

#define XMODEM_POLYNOM 0x1021

#define NO_VERIFICATION_FLAG BIT(0)

struct __packed flashing_info_header {
	uint32_t base_address_be;
	uint32_t size_be;
	uint32_t flags_be;
};

static struct flashing_context {
	struct k_sem receiving_in_progress_semaphore;

	/* Flags from the header. E.g. whether there should be a readback check after
	 * every write
	 */
	uint32_t flags;

	enum {
		CHUNK_TYPE_UNKNOWN,
		CHUNK_TYPE_128,
		CHUNK_TYPE_1024,
		CHUNK_TYPE_EOT
	} current_chunk_type;

	uint32_t remaining_bytes_current_transfer;
	uint32_t current_offset_on_flash;

	struct __packed xmodem_chunk {
		uint8_t start_byte;
		uint8_t block_num;
		uint8_t inv_block_num;
		union {
			struct __packed {
				uint8_t data[1024];
				uint8_t crc_higher;
				uint8_t crc_lower;
			} block_1024;

			struct __packed {
				uint8_t data[128];
				uint8_t crc_higher;
				uint8_t crc_lower;
			} block_128;
		};
	} current_chunk;

	/* Write pointer into the currently received xmodem chunk */
	uint8_t *write_ptr;
} flashing_ctx;

/* Size of a chunk, exclucing the initial STX/SOH byte.
 * Includes the (inverse) block num, data and CRC
 */
#define REMAINING_BYTES_CHUNK_128 (2 + 128 + 2)
#define REMAINING_BYTES_CHUNK_1024 (2 + 1024 + 2)

/*
 * Loop endlessly and send that the transfer was cancelled. In this sample it is
 * also used for recoverable errors like Checksum mismatches
 */
static void fatal_error(const struct device *uart_dev)
{
	uart_rx_disable(uart_dev);
	while (1) {
		uart_poll_out(uart_dev, CAN);
		k_sleep(K_MSEC(500));
	}
}

static void uart_isr(const struct device *uart_dev, void *flash_ctx)
{
	int ret;
	struct flashing_context *ctx = flash_ctx;

	ret = uart_irq_update(uart_dev);
	if (ret < 0) {
		fatal_error(uart_dev);
	}

	ret = uart_irq_rx_ready(uart_dev);
	if (ret < 0) {
		fatal_error(uart_dev);
	} else if (ret == 0) {
		return;
	}

	if (ctx->current_chunk_type == CHUNK_TYPE_UNKNOWN) {
		/* An interrupt occured meaning new data should be available but there was no data available */
		if (uart_fifo_read(uart_dev, ctx->write_ptr++, 1) != 1) {
			fatal_error(uart_dev);
		}

		/* Identify the chunk type based on the first byte */
		if (ctx->current_chunk.start_byte == SOH) {
			ctx->current_chunk_type = CHUNK_TYPE_128;
			ctx->remaining_bytes_current_transfer = REMAINING_BYTES_CHUNK_128;
		} else if (ctx->current_chunk.start_byte == STX) {
			ctx->current_chunk_type = CHUNK_TYPE_1024;
			ctx->remaining_bytes_current_transfer = REMAINING_BYTES_CHUNK_1024;
		} else if (ctx->current_chunk.start_byte == EOT) {
			ctx->current_chunk_type = CHUNK_TYPE_EOT;

			/* We can stop receiving since we received a End of Transfer byte */
			uart_irq_rx_disable(uart_dev);
			k_sem_give(&ctx->receiving_in_progress_semaphore);

			return;
		} else {
			/* Unknown / not implemented Chunk */
			fatal_error(uart_dev);
		}
	}

	/* Read until either the chunk is complete or there is no data left */
	while (1) {
		if (ctx->remaining_bytes_current_transfer == 0) {
			break;
		}
		if (uart_fifo_read(uart_dev, ctx->write_ptr, 1) != 1) {
			return;
		}
		ctx->write_ptr++;
		ctx->remaining_bytes_current_transfer--;
	}

	if (ctx->remaining_bytes_current_transfer == 0) {
		uart_irq_rx_disable(uart_dev);

		k_sem_give(&ctx->receiving_in_progress_semaphore);
	}
}

/* Verify integrety of the current message via CRC16 checksum */
static int verify_message(struct flashing_context *ctx)
{
	uint16_t calculated_crc;
	uint16_t received_crc;
	if (ctx->current_chunk_type == CHUNK_TYPE_1024) {
		calculated_crc = crc16(XMODEM_POLYNOM, 0x0, ctx->current_chunk.block_1024.data, 1024);
		received_crc = (ctx->current_chunk.block_1024.crc_higher << 8) | ctx->current_chunk.block_1024.crc_lower;
	} else {
		calculated_crc = crc16(XMODEM_POLYNOM, 0x0, ctx->current_chunk.block_128.data, 128);
		received_crc = (ctx->current_chunk.block_128.crc_higher << 8) | ctx->current_chunk.block_128.crc_lower;
	}

	if (calculated_crc != received_crc) {
		return -EIO;
	}

	return 0;
}

/* Erase flash based on flash header and Flash parameters */
static int erase_flash(const struct device *flash_dev, uint32_t base, uint32_t size)
{
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	int ret = 0;
	struct flash_pages_info info = {0};
	uint32_t additional_offset = 0;

	while (additional_offset < size) {
		ret = flash_get_page_info_by_offs(flash_dev, base + additional_offset, &info);
		if (ret < 0) {
			return ret;
		}

		ret = flash_flatten(flash_dev, info.start_offset, info.size);
		if (ret < 0) {
			return ret;
		}

		additional_offset += info.size;
	}

	return 0;

#else
	return flash_flatten(flash_dev, offset, size);
#endif
}

/* Parse header and erase appropiatly */
static int parse_header_and_erase(const struct device *flash_dev, struct flashing_context *ctx)
{
	const struct flashing_info_header *reinterpreted = (struct flashing_info_header*) ctx->current_chunk.block_128.data;

	const uint32_t base = sys_be32_to_cpu(reinterpreted->base_address_be);
	const uint32_t len = sys_be32_to_cpu(reinterpreted->size_be);
	const uint32_t flags = sys_be32_to_cpu(reinterpreted->flags_be);

	ctx->current_offset_on_flash = base;
	ctx->flags = flags;

	return erase_flash(flash_dev, base, len);
}

static int flash_data(const struct device *flash_dev, struct flashing_context *ctx)
{
	uint32_t len;
	void *data;

	if (ctx->current_chunk_type == CHUNK_TYPE_128) {
		len = 128;
		data = ctx->current_chunk.block_128.data;
	} else {
		len = 1024;
		data = ctx->current_chunk.block_1024.data;
	}

	int ret = flash_write(flash_dev, ctx->current_offset_on_flash, data, len);

	if (ret < 0) {
		return ret;
	}

	/* Only do verification when the NO_VERIFICATION_FLAG bit *isn't* set */
	if (!(ctx->flags & NO_VERIFICATION_FLAG)) {
		static uint8_t readback[1024] = {0};

		ret = flash_read(flash_dev, ctx->current_offset_on_flash, readback, len);
		if (ret < 0) {
			return ret;
		}

		if (memcmp(data, readback, len) != 0) {
			return ret;
		}
	}

	ctx->current_offset_on_flash += len;

	return ret;
}

static void prepare_for_next_message(struct flashing_context *ctx)
{
	ctx->current_chunk_type = CHUNK_TYPE_UNKNOWN;
	memset(&ctx->current_chunk, 0, sizeof(struct xmodem_chunk));
	ctx->write_ptr = (uint8_t*) &ctx->current_chunk;
}

static void receive_next_message(const struct device *uart_dev, struct flashing_context *ctx)
{
	uart_irq_rx_enable(uart_dev);
	k_sem_take(&ctx->receiving_in_progress_semaphore, K_FOREVER);
}

static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

int main(void)
{
	int ret;
	flashing_ctx.current_chunk_type = CHUNK_TYPE_UNKNOWN;

	if (!device_is_ready(uart_dev)) {
		fatal_error(uart_dev);
	}

	ret = uart_irq_callback_user_data_set(uart_dev, uart_isr, &flashing_ctx);
	if (ret < 0) {
		fatal_error(uart_dev);
	}

	k_sem_init(&flashing_ctx.receiving_in_progress_semaphore, 0, 1);
	flashing_ctx.write_ptr = (uint8_t*) &flashing_ctx.current_chunk;

	/* send pings (ASCII 'C') until there is a reply, indicating a transfer started */
	while (1) {
		uart_irq_rx_disable(uart_dev);

		if (flashing_ctx.current_chunk_type != CHUNK_TYPE_UNKNOWN) {
			break;
		}

		uart_poll_out(uart_dev, PING);

		uart_irq_rx_enable(uart_dev);

		k_sleep(K_MSEC(500));
	}

	k_sem_take(&flashing_ctx.receiving_in_progress_semaphore, K_FOREVER);

	ret = verify_message(&flashing_ctx);
	if (ret < 0) {
		fatal_error(uart_dev);
	}

	ret = parse_header_and_erase(flash_dev, &flashing_ctx);
	if (ret < 0) {
		fatal_error(uart_dev);
	}

	k_sleep(K_MSEC(100));

	while (1) {
		prepare_for_next_message(&flashing_ctx);
		uart_poll_out(uart_dev, ACK);

		receive_next_message(uart_dev, &flashing_ctx);

		if (flashing_ctx.current_chunk_type == CHUNK_TYPE_EOT) {
			uart_poll_out(uart_dev, ACK);
			/* Go into infinite loop after the complete message was received */
			while (1);
		}

		ret = verify_message(&flashing_ctx);
		if (ret < 0) {
			fatal_error(uart_dev);
		}

		ret = flash_data(flash_dev, &flashing_ctx);
		if (ret < 0) {
			fatal_error(uart_dev);
		}
	}
}
