/*
 * Copyright (c) 2023, Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public APIs for the Device Multiplexer driver
 */

#ifndef INCLUDE_ZEPHYR_DRIVERS_TISCI_PROTOCOL_H_
#define INCLUDE_ZEPHYR_DRIVERS_TISCI_PROTOCOL_H_

#include <zephyr/drivers/mbox.h>
#include <stdint.h>

#define TI_SCI_MSG_ENABLE_WDT            0x0000
#define TI_SCI_MSG_WAKE_RESET            0x0001
#define TI_SCI_MSG_VERSION               0x0002
#define TI_SCI_MSG_WAKE_REASON           0x0003
#define TI_SCI_MSG_GOODBYE               0x0004
#define TI_SCI_MSG_SYS_RESET             0x0005
#define TI_SCI_MSG_BOARD_CONFIG          0x000b
#define TI_SCI_MSG_BOARD_CONFIG_RM       0x000c
#define TI_SCI_MSG_BOARD_CONFIG_SECURITY 0x000d
#define TI_SCI_MSG_BOARD_CONFIG_PM       0x000e
#define TISCI_MSG_QUERY_MSMC             0x0020

/* Device requests */
#define TI_SCI_MSG_SET_DEVICE_STATE  0x0200
#define TI_SCI_MSG_GET_DEVICE_STATE  0x0201
#define TI_SCI_MSG_SET_DEVICE_RESETS 0x0202

/* Clock requests */
#define TI_SCI_MSG_SET_CLOCK_STATE       0x0100
#define TI_SCI_MSG_GET_CLOCK_STATE       0x0101
#define TI_SCI_MSG_SET_CLOCK_PARENT      0x0102
#define TI_SCI_MSG_GET_CLOCK_PARENT      0x0103
#define TI_SCI_MSG_GET_NUM_CLOCK_PARENTS 0x0104
#define TI_SCI_MSG_SET_CLOCK_FREQ        0x010c
#define TI_SCI_MSG_QUERY_CLOCK_FREQ      0x010d
#define TI_SCI_MSG_GET_CLOCK_FREQ        0x010e

/* Processor Control Messages */
#define TISCI_MSG_PROC_REQUEST          0xc000
#define TISCI_MSG_PROC_RELEASE          0xc001
#define TISCI_MSG_PROC_HANDOVER         0xc005
#define TISCI_MSG_SET_PROC_BOOT_CONFIG  0xc100
#define TISCI_MSG_SET_PROC_BOOT_CTRL    0xc101
#define TISCI_MSG_PROC_AUTH_BOOT_IMAGE  0xc120
#define TISCI_MSG_GET_PROC_BOOT_STATUS  0xc400
#define TISCI_MSG_WAIT_PROC_BOOT_STATUS 0xc401

/* Resource Management Requests */
#define TI_SCI_MSG_GET_RESOURCE_RANGE 0x1500

/* NAVSS resource management */
/* Ringacc requests */
#define TI_SCI_MSG_RM_RING_CFG 0x1110

/* PSI-L requests */
#define TI_SCI_MSG_RM_PSIL_PAIR   0x1280
#define TI_SCI_MSG_RM_PSIL_UNPAIR 0x1281

#define TI_SCI_MSG_RM_UDMAP_TX_ALLOC     0x1200
#define TI_SCI_MSG_RM_UDMAP_TX_FREE      0x1201
#define TI_SCI_MSG_RM_UDMAP_RX_ALLOC     0x1210
#define TI_SCI_MSG_RM_UDMAP_RX_FREE      0x1211
#define TI_SCI_MSG_RM_UDMAP_FLOW_CFG     0x1220
#define TI_SCI_MSG_RM_UDMAP_OPT_FLOW_CFG 0x1221

#define TISCI_MSG_RM_UDMAP_TX_CH_CFG            0x1205
#define TISCI_MSG_RM_UDMAP_RX_CH_CFG            0x1215
#define TISCI_MSG_RM_UDMAP_FLOW_CFG             0x1230
#define TISCI_MSG_RM_UDMAP_FLOW_SIZE_THRESH_CFG 0x1231

#define TISCI_MSG_FWL_SET          0x9000
#define TISCI_MSG_FWL_GET          0x9001
#define TISCI_MSG_FWL_CHANGE_OWNER 0x9002

/**
 * struct ti_sci_msg_hdr - Generic Message Header for All messages and responses
 * @type:	Type of messages: One of TI_SCI_MSG* values
 * @host:	Host of the message
 * @seq:	Message identifier indicating a transfer sequence
 * @flags:	Flag for the message
 */
struct ti_sci_msg_hdr {
	uint16_t type;
	uint8_t host;
	uint8_t seq;
#define TI_SCI_MSG_FLAG(val)               (1 << (val))
#define TI_SCI_FLAG_REQ_GENERIC_NORESPONSE 0x0
#define TI_SCI_FLAG_REQ_ACK_ON_RECEIVED    TI_SCI_MSG_FLAG(0)
#define TI_SCI_FLAG_REQ_ACK_ON_PROCESSED   TI_SCI_MSG_FLAG(1)
#define TI_SCI_FLAG_RESP_GENERIC_NACK      0x0
#define TI_SCI_FLAG_RESP_GENERIC_ACK       TI_SCI_MSG_FLAG(1)
	/* Additional Flags */
	uint32_t flags;
} __attribute__((__packed__));

/**
 * struct ti_sci_secure_msg_hdr - Header that prefixes all TISCI messages sent
 *				  via secure transport.
 * @checksum:	crc16 checksum for the entire message
 * @reserved:	Reserved for future use.
 */
struct ti_sci_secure_msg_hdr {
	uint16_t checksum;
	uint16_t reserved;
} __attribute__((__packed__));

/**
 * struct ti_sci_msg_resp_version - Response for a message
 * @hdr:		Generic header
 * @firmware_description: String describing the firmware
 * @firmware_revision:	Firmware revision
 * @abi_major:		Major version of the ABI that firmware supports
 * @abi_minor:		Minor version of the ABI that firmware supports
 *
 * In general, ABI version changes follow the rule that minor version increments
 * are backward compatible. Major revision changes in ABI may not be
 * backward compatible.
 *
 * Response to a generic message with message type TI_SCI_MSG_VERSION
 */
struct ti_sci_msg_resp_version {
	struct ti_sci_msg_hdr hdr;
	char firmware_description[32];
	uint16_t firmware_revision;
	uint8_t abi_major;
	uint8_t abi_minor;
} __packed;

/**
 * struct ti_sci_msg_req_reboot - Reboot the SoC
 * @hdr:	Generic Header
 * @domain:	Domain to be reset, 0 for full SoC reboot.
 *
 * Request type is TI_SCI_MSG_SYS_RESET, responded with a generic
 * ACK/NACK message.
 */
struct ti_sci_msg_req_reboot {
	struct ti_sci_msg_hdr hdr;
	uint8_t domain;
} __packed;

/**
 * struct ti_sci_msg_board_config - Board configuration message
 * @hdr:		Generic Header
 * @boardcfgp_low:	Lower 32 bit of the pointer pointing to the board
 *			configuration data
 * @boardcfgp_high:	Upper 32 bit of the pointer pointing to the board
 *			configuration data
 * @boardcfg_size:	Size of board configuration data object
 * Request type is TI_SCI_MSG_BOARD_CONFIG, responded with a generic
 * ACK/NACK message.
 */
struct ti_sci_msg_board_config {
	struct ti_sci_msg_hdr hdr;
	uint32_t boardcfgp_low;
	uint32_t boardcfgp_high;
	uint16_t boardcfg_size;
} __packed;

/**
 * struct ti_sci_msg_resp_query_msmc - Query msmc message response structure
 * @hdr:		Generic Header
 * @msmc_start_low:	Lower 32 bit of msmc start
 * @msmc_start_high:	Upper 32 bit of msmc start
 * @msmc_end_low:	Lower 32 bit of msmc end
 * @msmc_end_high:	Upper 32 bit of msmc end
 *
 * Response to a generic message with message type TISCI_MSG_QUERY_MSMC
 */
struct ti_sci_msg_resp_query_msmc {
	struct ti_sci_msg_hdr hdr;
	uint32_t msmc_start_low;
	uint32_t msmc_start_high;
	uint32_t msmc_end_low;
	uint32_t msmc_end_high;
} __packed;

/**
 * struct ti_sci_msg_req_set_device_state - Set the desired state of the device
 * @hdr:		Generic header
 * @id:	Indicates which device to modify
 * @reserved: Reserved space in message, must be 0 for backward compatibility
 * @state: The desired state of the device.
 *
 * Certain flags can also be set to alter the device state:
 *  MSG_FLAG_DEVICE_WAKE_ENABLED - Configure the device to be a wake source.
 * The meaning of this flag will vary slightly from device to device and from
 * SoC to SoC but it generally allows the device to wake the SoC out of deep
 * suspend states.
 *  MSG_FLAG_DEVICE_RESET_ISO - Enable reset isolation for this device.
 *  MSG_FLAG_DEVICE_EXCLUSIVE - Claim this device exclusively. When passed
 * with STATE_RETENTION or STATE_ON, it will claim the device exclusively.
 * If another host already has this device set to STATE_RETENTION or STATE_ON,
 * the message will fail. Once successful, other hosts attempting to set
 * STATE_RETENTION or STATE_ON will fail.
 *
 * Request type is TI_SCI_MSG_SET_DEVICE_STATE, responded with a generic
 * ACK/NACK message.
 */
struct ti_sci_msg_req_set_device_state {
	/* Additional hdr->flags options */
#define MSG_FLAG_DEVICE_WAKE_ENABLED TI_SCI_MSG_FLAG(8)
#define MSG_FLAG_DEVICE_RESET_ISO    TI_SCI_MSG_FLAG(9)
#define MSG_FLAG_DEVICE_EXCLUSIVE    TI_SCI_MSG_FLAG(10)
	struct ti_sci_msg_hdr hdr;
	uint32_t id;
	uint32_t reserved;

#define MSG_DEVICE_SW_STATE_AUTO_OFF  0
#define MSG_DEVICE_SW_STATE_RETENTION 1
#define MSG_DEVICE_SW_STATE_ON        2
	uint8_t state;
} __packed;

/**
 * struct ti_sci_msg_req_get_device_state - Request to get device.
 * @hdr:		Generic header
 * @id:		Device Identifier
 *
 * Request type is TI_SCI_MSG_GET_DEVICE_STATE, responded device state
 * information
 */
struct ti_sci_msg_req_get_device_state {
	struct ti_sci_msg_hdr hdr;
	uint32_t id;
} __packed;

/**
 * struct ti_sci_msg_resp_get_device_state - Response to get device request.
 * @hdr:		Generic header
 * @context_loss_count: Indicates how many times the device has lost context. A
 *	driver can use this monotonic counter to determine if the device has
 *	lost context since the last time this message was exchanged.
 * @resets: Programmed state of the reset lines.
 * @programmed_state:	The state as programmed by set_device.
 *			- Uses the MSG_DEVICE_SW_* macros
 * @current_state:	The actual state of the hardware.
 *
 * Response to request TI_SCI_MSG_GET_DEVICE_STATE.
 */
struct ti_sci_msg_resp_get_device_state {
	struct ti_sci_msg_hdr hdr;
	uint32_t context_loss_count;
	uint32_t resets;
	uint8_t programmed_state;
#define MSG_DEVICE_HW_STATE_OFF   0
#define MSG_DEVICE_HW_STATE_ON    1
#define MSG_DEVICE_HW_STATE_TRANS 2
	uint8_t current_state;
} __packed;

/**
 * struct ti_sci_msg_req_set_device_resets - Set the desired resets
 *				configuration of the device
 * @hdr:		Generic header
 * @id:	Indicates which device to modify
 * @resets: A bit field of resets for the device. The meaning, behavior,
 *	and usage of the reset flags are device specific. 0 for a bit
 *	indicates releasing the reset represented by that bit while 1
 *	indicates keeping it held.
 *
 * Request type is TI_SCI_MSG_SET_DEVICE_RESETS, responded with a generic
 * ACK/NACK message.
 */
struct ti_sci_msg_req_set_device_resets {
	struct ti_sci_msg_hdr hdr;
	uint32_t id;
	uint32_t resets;
} __packed;

/**
 * struct ti_sci_msg_req_set_clock_state - Request to setup a Clock state
 * @hdr:	Generic Header, Certain flags can be set specific to the clocks:
 *		MSG_FLAG_CLOCK_ALLOW_SSC: Allow this clock to be modified
 *		via spread spectrum clocking.
 *		MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE: Allow this clock's
 *		frequency to be changed while it is running so long as it
 *		is within the min/max limits.
 *		MSG_FLAG_CLOCK_INPUT_TERM: Enable input termination, this
 *		is only applicable to clock inputs on the SoC pseudo-device.
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @request_state: Request the state for the clock to be set to.
 *		MSG_CLOCK_SW_STATE_UNREQ: The IP does not require this clock,
 *		it can be disabled, regardless of the state of the device
 *		MSG_CLOCK_SW_STATE_AUTO: Allow the System Controller to
 *		automatically manage the state of this clock. If the device
 *		is enabled, then the clock is enabled. If the device is set
 *		to off or retention, then the clock is internally set as not
 *		being required by the device.(default)
 *		MSG_CLOCK_SW_STATE_REQ:  Configure the clock to be enabled,
 *		regardless of the state of the device.
 *
 * Normally, all required clocks are managed by TISCI entity, this is used
 * only for specific control *IF* required. Auto managed state is
 * MSG_CLOCK_SW_STATE_AUTO, in other states, TISCI entity assume remote
 * will explicitly control.
 *
 * Request type is TI_SCI_MSG_SET_CLOCK_STATE, response is a generic
 * ACK or NACK message.
 */
struct ti_sci_msg_req_set_clock_state {
	/* Additional hdr->flags options */
#define MSG_FLAG_CLOCK_ALLOW_SSC         TI_SCI_MSG_FLAG(8)
#define MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE TI_SCI_MSG_FLAG(9)
#define MSG_FLAG_CLOCK_INPUT_TERM        TI_SCI_MSG_FLAG(10)
	struct ti_sci_msg_hdr hdr;
	uint32_t dev_id;
	uint8_t clk_id;
#define MSG_CLOCK_SW_STATE_UNREQ 0
#define MSG_CLOCK_SW_STATE_AUTO  1
#define MSG_CLOCK_SW_STATE_REQ   2
	uint8_t request_state;
} __packed;

/**
 * struct ti_sci_msg_req_get_clock_state - Request for clock state
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to get state of.
 *
 * Request type is TI_SCI_MSG_GET_CLOCK_STATE, response is state
 * of the clock
 */
struct ti_sci_msg_req_get_clock_state {
	struct ti_sci_msg_hdr hdr;
	uint32_t dev_id;
	uint8_t clk_id;
} __packed;

/**
 * struct ti_sci_msg_resp_get_clock_state - Response to get clock state
 * @hdr:	Generic Header
 * @programmed_state: Any programmed state of the clock. This is one of
 *		MSG_CLOCK_SW_STATE* values.
 * @current_state: Current state of the clock. This is one of:
 *		MSG_CLOCK_HW_STATE_NOT_READY: Clock is not ready
 *		MSG_CLOCK_HW_STATE_READY: Clock is ready
 *
 * Response to TI_SCI_MSG_GET_CLOCK_STATE.
 */
struct ti_sci_msg_resp_get_clock_state {
	struct ti_sci_msg_hdr hdr;
	uint8_t programmed_state;
#define MSG_CLOCK_HW_STATE_NOT_READY 0
#define MSG_CLOCK_HW_STATE_READY     1
	uint8_t current_state;
} __packed;

/**
 * struct ti_sci_msg_req_set_clock_parent - Set the clock parent
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @parent_id:	The new clock parent is selectable by an index via this
 *		parameter.
 *
 * Request type is TI_SCI_MSG_SET_CLOCK_PARENT, response is generic
 * ACK / NACK message.
 */
struct ti_sci_msg_req_set_clock_parent {
	struct ti_sci_msg_hdr hdr;
	uint32_t dev_id;
	uint8_t clk_id;
	uint8_t parent_id;
} __packed;

/**
 * struct ti_sci_msg_req_get_clock_parent - Get the clock parent
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to get the parent for.
 *
 * Request type is TI_SCI_MSG_GET_CLOCK_PARENT, response is parent information
 */
struct ti_sci_msg_req_get_clock_parent {
	struct ti_sci_msg_hdr hdr;
	uint32_t dev_id;
	uint8_t clk_id;
} __packed;

/**
 * struct ti_sci_msg_resp_get_clock_parent - Response with clock parent
 * @hdr:	Generic Header
 * @parent_id:	The current clock parent
 *
 * Response to TI_SCI_MSG_GET_CLOCK_PARENT.
 */
struct ti_sci_msg_resp_get_clock_parent {
	struct ti_sci_msg_hdr hdr;
	uint8_t parent_id;
} __packed;

/**
 * struct ti_sci_msg_req_get_clock_num_parents - Request to get clock parents
 * @hdr:	Generic header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *
 * This request provides information about how many clock parent options
 * are available for a given clock to a device. This is typically used
 * for input clocks.
 *
 * Request type is TI_SCI_MSG_GET_NUM_CLOCK_PARENTS, response is appropriate
 * message, or NACK in case of inability to satisfy request.
 */
struct ti_sci_msg_req_get_clock_num_parents {
	struct ti_sci_msg_hdr hdr;
	uint32_t dev_id;
	uint8_t clk_id;
} __packed;

/**
 * struct ti_sci_msg_resp_get_clock_num_parents - Response for get clk parents
 * @hdr:		Generic header
 * @num_parents:	Number of clock parents
 *
 * Response to TI_SCI_MSG_GET_NUM_CLOCK_PARENTS
 */
struct ti_sci_msg_resp_get_clock_num_parents {
	struct ti_sci_msg_hdr hdr;
	uint8_t num_parents;
} __packed;

/**
 * struct ti_sci_msg_req_query_clock_freq - Request to query a frequency
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @min_freq_hz: The minimum allowable frequency in Hz. This is the minimum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @target_freq_hz: The target clock frequency. A frequency will be found
 *		as close to this target frequency as possible.
 * @max_freq_hz: The maximum allowable frequency in Hz. This is the maximum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @clk_id:	Clock identifier for the device for this request.
 *
 * NOTE: Normally clock frequency management is automatically done by TISCI
 * entity. In case of specific requests, TISCI evaluates capability to achieve
 * requested frequency within provided range and responds with
 * result message.
 *
 * Request type is TI_SCI_MSG_QUERY_CLOCK_FREQ, response is appropriate message,
 * or NACK in case of inability to satisfy request.
 */
struct ti_sci_msg_req_query_clock_freq {
	struct ti_sci_msg_hdr hdr;
	uint32_t dev_id;
	uint64_t min_freq_hz;
	uint64_t target_freq_hz;
	uint64_t max_freq_hz;
	uint8_t clk_id;
} __packed;

/**
 * struct ti_sci_msg_resp_query_clock_freq - Response to a clock frequency query
 * @hdr:	Generic Header
 * @freq_hz:	Frequency that is the best match in Hz.
 *
 * Response to request type TI_SCI_MSG_QUERY_CLOCK_FREQ. NOTE: if the request
 * cannot be satisfied, the message will be of type NACK.
 */
struct ti_sci_msg_resp_query_clock_freq {
	struct ti_sci_msg_hdr hdr;
	uint64_t freq_hz;
} __packed;

/**
 * struct ti_sci_msg_req_set_clock_freq - Request to setup a clock frequency
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @min_freq_hz: The minimum allowable frequency in Hz. This is the minimum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @target_freq_hz: The target clock frequency. The clock will be programmed
 *		at a rate as close to this target frequency as possible.
 * @max_freq_hz: The maximum allowable frequency in Hz. This is the maximum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @clk_id:	Clock identifier for the device for this request.
 *
 * NOTE: Normally clock frequency management is automatically done by TISCI
 * entity. In case of specific requests, TISCI evaluates capability to achieve
 * requested range and responds with success/failure message.
 *
 * This sets the desired frequency for a clock within an allowable
 * range. This message will fail on an enabled clock unless
 * MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE is set for the clock. Additionally,
 * if other clocks have their frequency modified due to this message,
 * they also must have the MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE or be disabled.
 *
 * Calling set frequency on a clock input to the SoC pseudo-device will
 * inform the PMMC of that clock's frequency. Setting a frequency of
 * zero will indicate the clock is disabled.
 *
 * Calling set frequency on clock outputs from the SoC pseudo-device will
 * function similarly to setting the clock frequency on a device.
 *
 * Request type is TI_SCI_MSG_SET_CLOCK_FREQ, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_set_clock_freq {
	struct ti_sci_msg_hdr hdr;
	uint32_t dev_id;
	uint64_t min_freq_hz;
	uint64_t target_freq_hz;
	uint64_t max_freq_hz;
	uint8_t clk_id;
} __packed;

/**
 * struct ti_sci_msg_req_get_clock_freq - Request to get the clock frequency
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *
 * NOTE: Normally clock frequency management is automatically done by TISCI
 * entity. In some cases, clock frequencies are configured by host.
 *
 * Request type is TI_SCI_MSG_GET_CLOCK_FREQ, responded with clock frequency
 * that the clock is currently at.
 */
struct ti_sci_msg_req_get_clock_freq {
	struct ti_sci_msg_hdr hdr;
	uint32_t dev_id;
	uint8_t clk_id;
} __packed;

/**
 * struct ti_sci_msg_resp_get_clock_freq - Response of clock frequency request
 * @hdr:	Generic Header
 * @freq_hz:	Frequency that the clock is currently on, in Hz.
 *
 * Response to request type TI_SCI_MSG_GET_CLOCK_FREQ.
 */
struct ti_sci_msg_resp_get_clock_freq {
	struct ti_sci_msg_hdr hdr;
	uint64_t freq_hz;
} __packed;

#define TI_SCI_IRQ_SECONDARY_HOST_INVALID 0xff

/**
 * struct ti_sci_msg_req_get_resource_range - Request to get a host's assigned
 *					      range of resources.
 * @hdr:		Generic Header
 * @type:		Unique resource assignment type
 * @subtype:		Resource assignment subtype within the resource type.
 * @secondary_host:	Host processing entity to which the resources are
 *			allocated. This is required only when the destination
 *			host id id different from ti sci interface host id,
 *			else TI_SCI_IRQ_SECONDARY_HOST_INVALID can be passed.
 *
 * Request type is TI_SCI_MSG_GET_RESOURCE_RANGE. Responded with requested
 * resource range which is of type TI_SCI_MSG_GET_RESOURCE_RANGE.
 */
struct ti_sci_msg_req_get_resource_range {
	struct ti_sci_msg_hdr hdr;
#define MSG_RM_RESOURCE_TYPE_MASK    GENMASK(9, 0)
#define MSG_RM_RESOURCE_SUBTYPE_MASK GENMASK(5, 0)
	uint16_t type;
	uint8_t subtype;
	uint8_t secondary_host;
} __packed;

/**
 * struct ti_sci_msg_resp_get_resource_range - Response to resource get range.
 * @hdr:		Generic Header
 * @range_start:	Start index of the resource range.
 * @range_num:		Number of resources in the range.
 *
 * Response to request TI_SCI_MSG_GET_RESOURCE_RANGE.
 */
struct ti_sci_msg_resp_get_resource_range {
	struct ti_sci_msg_hdr hdr;
	uint16_t range_start;
	uint16_t range_num;
} __packed;

#define GENMASK_ULL(h, l)     (((~0ULL) << (l)) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))
#define TISCI_ADDR_LOW_MASK   GENMASK_ULL(31, 0)
#define TISCI_ADDR_HIGH_MASK  GENMASK_ULL(63, 32)
#define TISCI_ADDR_HIGH_SHIFT 32

/**
 * struct ti_sci_msg_req_proc_request - Request a processor
 *
 * @hdr:		Generic Header
 * @processor_id:	ID of processor
 *
 * Request type is TISCI_MSG_PROC_REQUEST, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_proc_request {
	struct ti_sci_msg_hdr hdr;
	uint8_t processor_id;
} __packed;

/**
 * struct ti_sci_msg_req_proc_release - Release a processor
 *
 * @hdr:		Generic Header
 * @processor_id:	ID of processor
 *
 * Request type is TISCI_MSG_PROC_RELEASE, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_proc_release {
	struct ti_sci_msg_hdr hdr;
	uint8_t processor_id;
} __packed;

/**
 * struct ti_sci_msg_req_proc_handover - Handover a processor to a host
 *
 * @hdr:		Generic Header
 * @processor_id:	ID of processor
 * @host_id:		New Host we want to give control to
 *
 * Request type is TISCI_MSG_PROC_HANDOVER, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_proc_handover {
	struct ti_sci_msg_hdr hdr;
	uint8_t processor_id;
	uint8_t host_id;
} __packed;

/* A53 Config Flags */
#define PROC_BOOT_CFG_FLAG_ARMV8_DBG_EN      0x00000001
#define PROC_BOOT_CFG_FLAG_ARMV8_DBG_NIDEN   0x00000002
#define PROC_BOOT_CFG_FLAG_ARMV8_DBG_SPIDEN  0x00000004
#define PROC_BOOT_CFG_FLAG_ARMV8_DBG_SPNIDEN 0x00000008
#define PROC_BOOT_CFG_FLAG_ARMV8_AARCH32     0x00000100

/* R5 Config Flags */
#define PROC_BOOT_CFG_FLAG_R5_DBG_EN      0x00000001
#define PROC_BOOT_CFG_FLAG_R5_DBG_NIDEN   0x00000002
#define PROC_BOOT_CFG_FLAG_R5_LOCKSTEP    0x00000100
#define PROC_BOOT_CFG_FLAG_R5_TEINIT      0x00000200
#define PROC_BOOT_CFG_FLAG_R5_NMFI_EN     0x00000400
#define PROC_BOOT_CFG_FLAG_R5_TCM_RSTBASE 0x00000800
#define PROC_BOOT_CFG_FLAG_R5_BTCM_EN     0x00001000
#define PROC_BOOT_CFG_FLAG_R5_ATCM_EN     0x00002000

/**
 * struct ti_sci_msg_req_set_proc_boot_config - Set Processor boot configuration
 * @hdr:		Generic Header
 * @processor_id:	ID of processor
 * @bootvector_low:	Lower 32bit (Little Endian) of boot vector
 * @bootvector_high:	Higher 32bit (Little Endian) of boot vector
 * @config_flags_set:	Optional Processor specific Config Flags to set.
 *			Setting a bit here implies required bit sets to 1.
 * @config_flags_clear:	Optional Processor specific Config Flags to clear.
 *			Setting a bit here implies required bit gets cleared.
 *
 * Request type is TISCI_MSG_SET_PROC_BOOT_CONFIG, response is a generic
 * ACK/NACK message.
 */
struct ti_sci_msg_req_set_proc_boot_config {
	struct ti_sci_msg_hdr hdr;
	uint8_t processor_id;
	uint32_t bootvector_low;
	uint32_t bootvector_high;
	uint32_t config_flags_set;
	uint32_t config_flags_clear;
} __packed;

/* R5 Control Flags */
#define PROC_BOOT_CTRL_FLAG_R5_CORE_HALT 0x00000001

/**
 * struct ti_sci_msg_req_set_proc_boot_ctrl - Set Processor boot control flags
 * @hdr:		Generic Header
 * @processor_id:	ID of processor
 * @control_flags_set:	Optional Processor specific Control Flags to set.
 *			Setting a bit here implies required bit sets to 1.
 * @control_flags_clear:Optional Processor specific Control Flags to clear.
 *			Setting a bit here implies required bit gets cleared.
 *
 * Request type is TISCI_MSG_SET_PROC_BOOT_CTRL, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_set_proc_boot_ctrl {
	struct ti_sci_msg_hdr hdr;
	uint8_t processor_id;
	uint32_t control_flags_set;
	uint32_t control_flags_clear;
} __packed;

/**
 * struct ti_sci_msg_req_proc_auth_start_image - Authenticate and start image
 * @hdr:		Generic Header
 * @cert_addr_low:	Lower 32bit (Little Endian) of certificate
 * @cert_addr_high:	Higher 32bit (Little Endian) of certificate
 *
 * Request type is TISCI_MSG_PROC_AUTH_BOOT_IMAGE, response is a generic
 * ACK/NACK message.
 */
struct ti_sci_msg_req_proc_auth_boot_image {
	struct ti_sci_msg_hdr hdr;
	uint32_t cert_addr_low;
	uint32_t cert_addr_high;
} __packed;

struct ti_sci_msg_resp_proc_auth_boot_image {
	struct ti_sci_msg_hdr hdr;
	uint32_t image_addr_low;
	uint32_t image_addr_high;
	uint32_t image_size;
} __packed;

/**
 * struct ti_sci_msg_req_get_proc_boot_status - Get processor boot status
 * @hdr:		Generic Header
 * @processor_id:	ID of processor
 *
 * Request type is TISCI_MSG_GET_PROC_BOOT_STATUS, response is appropriate
 * message, or NACK in case of inability to satisfy request.
 */
struct ti_sci_msg_req_get_proc_boot_status {
	struct ti_sci_msg_hdr hdr;
	uint8_t processor_id;
} __packed;

/* ARMv8 Status Flags */
#define PROC_BOOT_STATUS_FLAG_ARMV8_WFE 0x00000001
#define PROC_BOOT_STATUS_FLAG_ARMV8_WFI 0x00000002

/* R5 Status Flags */
#define PROC_BOOT_STATUS_FLAG_R5_WFE                0x00000001
#define PROC_BOOT_STATUS_FLAG_R5_WFI                0x00000002
#define PROC_BOOT_STATUS_FLAG_R5_CLK_GATED          0x00000004
#define PROC_BOOT_STATUS_FLAG_R5_LOCKSTEP_PERMITTED 0x00000100

/**
 * struct ti_sci_msg_resp_get_proc_boot_status - Processor boot status response
 * @hdr:		Generic Header
 * @processor_id:	ID of processor
 * @bootvector_low:	Lower 32bit (Little Endian) of boot vector
 * @bootvector_high:	Higher 32bit (Little Endian) of boot vector
 * @config_flags:	Optional Processor specific Config Flags set.
 * @control_flags:	Optional Processor specific Control Flags.
 * @status_flags:	Optional Processor specific Status Flags set.
 *
 * Response to TISCI_MSG_GET_PROC_BOOT_STATUS.
 */
struct ti_sci_msg_resp_get_proc_boot_status {
	struct ti_sci_msg_hdr hdr;
	uint8_t processor_id;
	uint32_t bootvector_low;
	uint32_t bootvector_high;
	uint32_t config_flags;
	uint32_t control_flags;
	uint32_t status_flags;
} __packed;

/**
 * struct ti_sci_msg_req_wait_proc_boot_status - Wait for a processor
 *						 boot status
 * @hdr:			Generic Header
 * @processor_id:		ID of processor
 * @num_wait_iterations:	Total number of iterations we will check before
 *				we will timeout and give up
 * @num_match_iterations:	How many iterations should we have continued
 *				status to account for status bits glitching.
 *				This is to make sure that match occurs for
 *				consecutive checks. This implies that the
 *				worst case should consider that the stable
 *				time should at the worst be num_wait_iterations
 *				num_match_iterations to prevent timeout.
 * @delay_per_iteration_us:	Specifies how long to wait (in micro seconds)
 *				between each status checks. This is the minimum
 *				duration, and overhead of register reads and
 *				checks are on top of this and can vary based on
 *				varied conditions.
 * @delay_before_iterations_us:	Specifies how long to wait (in micro seconds)
 *				before the very first check in the first
 *				iteration of status check loop. This is the
 *				minimum duration, and overhead of register
 *				reads and checks are.
 * @status_flags_1_set_all_wait:If non-zero, Specifies that all bits of the
 *				status matching this field requested MUST be 1.
 * @status_flags_1_set_any_wait:If non-zero, Specifies that at least one of the
 *				bits matching this field requested MUST be 1.
 * @status_flags_1_clr_all_wait:If non-zero, Specifies that all bits of the
 *				status matching this field requested MUST be 0.
 * @status_flags_1_clr_any_wait:If non-zero, Specifies that at least one of the
 *				bits matching this field requested MUST be 0.
 *
 * Request type is TISCI_MSG_WAIT_PROC_BOOT_STATUS, response is appropriate
 * message, or NACK in case of inability to satisfy request.
 */
struct ti_sci_msg_req_wait_proc_boot_status {
	struct ti_sci_msg_hdr hdr;
	uint8_t processor_id;
	uint8_t num_wait_iterations;
	uint8_t num_match_iterations;
	uint8_t delay_per_iteration_us;
	uint8_t delay_before_iterations_us;
	uint32_t status_flags_1_set_all_wait;
	uint32_t status_flags_1_set_any_wait;
	uint32_t status_flags_1_clr_all_wait;
	uint32_t status_flags_1_clr_any_wait;
} __packed;

/**
 * struct ti_sci_msg_rm_ring_cfg_req - Configure a Navigator Subsystem ring
 *
 * Configures the non-real-time registers of a Navigator Subsystem ring.
 * @hdr:	Generic Header
 * @valid_params: Bitfield defining validity of ring configuration parameters.
 *	The ring configuration fields are not valid, and will not be used for
 *	ring configuration, if their corresponding valid bit is zero.
 *	Valid bit usage:
 *	0 - Valid bit for @tisci_msg_rm_ring_cfg_req addr_lo
 *	1 - Valid bit for @tisci_msg_rm_ring_cfg_req addr_hi
 *	2 - Valid bit for @tisci_msg_rm_ring_cfg_req count
 *	3 - Valid bit for @tisci_msg_rm_ring_cfg_req mode
 *	4 - Valid bit for @tisci_msg_rm_ring_cfg_req size
 *	5 - Valid bit for @tisci_msg_rm_ring_cfg_req order_id
 * @nav_id: Device ID of Navigator Subsystem from which the ring is allocated
 * @index: ring index to be configured.
 * @addr_lo: 32 LSBs of ring base address to be programmed into the ring's
 *	RING_BA_LO register
 * @addr_hi: 16 MSBs of ring base address to be programmed into the ring's
 *	RING_BA_HI register.
 * @count: Number of ring elements. Must be even if mode is CREDENTIALS or QM
 *	modes.
 * @mode: Specifies the mode the ring is to be configured.
 * @size: Specifies encoded ring element size. To calculate the encoded size use
 *	the formula (log2(size_bytes) - 2), where size_bytes cannot be
 *	greater than 256.
 * @order_id: Specifies the ring's bus order ID.
 */
struct ti_sci_msg_rm_ring_cfg_req {
	struct ti_sci_msg_hdr hdr;
	uint32_t valid_params;
	uint16_t nav_id;
	uint16_t index;
	uint32_t addr_lo;
	uint32_t addr_hi;
	uint32_t count;
	uint8_t mode;
	uint8_t size;
	uint8_t order_id;
} __packed;

/**
 * struct ti_sci_msg_rm_ring_cfg_resp - Response to configuring a ring.
 *
 * @hdr:	Generic Header
 */
struct ti_sci_msg_rm_ring_cfg_resp {
	struct ti_sci_msg_hdr hdr;
} __packed;

/**
 * struct ti_sci_msg_rm_ring_get_cfg_req - Get RA ring's configuration
 *
 * Gets the configuration of the non-real-time register fields of a ring.  The
 * host, or a supervisor of the host, who owns the ring must be the requesting
 * host.  The values of the non-real-time registers are returned in
 * @ti_sci_msg_rm_ring_get_cfg_resp.
 *
 * @hdr: Generic Header
 * @nav_id: Device ID of Navigator Subsystem from which the ring is allocated
 * @index: ring index.
 */
struct ti_sci_msg_rm_ring_get_cfg_req {
	struct ti_sci_msg_hdr hdr;
	uint16_t nav_id;
	uint16_t index;
} __packed;

/**
 * struct ti_sci_msg_rm_ring_get_cfg_resp -  Ring get configuration response
 *
 * Response received by host processor after RM has handled
 * @ti_sci_msg_rm_ring_get_cfg_req. The response contains the ring's
 * non-real-time register values.
 *
 * @hdr: Generic Header
 * @addr_lo: Ring 32 LSBs of base address
 * @addr_hi: Ring 16 MSBs of base address.
 * @count: Ring number of elements.
 * @mode: Ring mode.
 * @size: encoded Ring element size
 * @order_id: ing order ID.
 */
struct ti_sci_msg_rm_ring_get_cfg_resp {
	struct ti_sci_msg_hdr hdr;
	uint32_t addr_lo;
	uint32_t addr_hi;
	uint32_t count;
	uint8_t mode;
	uint8_t size;
	uint8_t order_id;
} __packed;

/**
 * struct ti_sci_msg_psil_pair - Pairs a PSI-L source thread to a destination
 *				 thread
 * @hdr:	Generic Header
 * @nav_id:	SoC Navigator Subsystem device ID whose PSI-L config proxy is
 *		used to pair the source and destination threads.
 * @src_thread:	PSI-L source thread ID within the PSI-L System thread map.
 *
 * UDMAP transmit channels mapped to source threads will have their
 * TCHAN_THRD_ID register programmed with the destination thread if the pairing
 * is successful.

 * @dst_thread: PSI-L destination thread ID within the PSI-L System thread map.
 * PSI-L destination threads start at index 0x8000.  The request is NACK'd if
 * the destination thread is not greater than or equal to 0x8000.
 *
 * UDMAP receive channels mapped to destination threads will have their
 * RCHAN_THRD_ID register programmed with the source thread if the pairing
 * is successful.
 *
 * Request type is TI_SCI_MSG_RM_PSIL_PAIR, response is a generic ACK or NACK
 * message.
 */
struct ti_sci_msg_psil_pair {
	struct ti_sci_msg_hdr hdr;
	uint32_t nav_id;
	uint32_t src_thread;
	uint32_t dst_thread;
} __packed;

/**
 * struct ti_sci_msg_psil_unpair - Unpairs a PSI-L source thread from a
 *				   destination thread
 * @hdr:	Generic Header
 * @nav_id:	SoC Navigator Subsystem device ID whose PSI-L config proxy is
 *		used to unpair the source and destination threads.
 * @src_thread:	PSI-L source thread ID within the PSI-L System thread map.
 *
 * UDMAP transmit channels mapped to source threads will have their
 * TCHAN_THRD_ID register cleared if the unpairing is successful.
 *
 * @dst_thread: PSI-L destination thread ID within the PSI-L System thread map.
 * PSI-L destination threads start at index 0x8000.  The request is NACK'd if
 * the destination thread is not greater than or equal to 0x8000.
 *
 * UDMAP receive channels mapped to destination threads will have their
 * RCHAN_THRD_ID register cleared if the unpairing is successful.
 *
 * Request type is TI_SCI_MSG_RM_PSIL_UNPAIR, response is a generic ACK or NACK
 * message.
 */
struct ti_sci_msg_psil_unpair {
	struct ti_sci_msg_hdr hdr;
	uint32_t nav_id;
	uint32_t src_thread;
	uint32_t dst_thread;
} __packed;

/**
 * Configures a Navigator Subsystem UDMAP transmit channel
 *
 * Configures the non-real-time registers of a Navigator Subsystem UDMAP
 * transmit channel.  The channel index must be assigned to the host defined
 * in the TISCI header via the RM board configuration resource assignment
 * range list.
 *
 * @hdr: Generic Header
 *
 * @valid_params: Bitfield defining validity of tx channel configuration
 * parameters. The tx channel configuration fields are not valid, and will not
 * be used for ch configuration, if their corresponding valid bit is zero.
 * Valid bit usage:
 *    0 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_pause_on_err
 *    1 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_atype
 *    2 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_chan_type
 *    3 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_fetch_size
 *    4 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::txcq_qnum
 *    5 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_priority
 *    6 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_qos
 *    7 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_orderid
 *    8 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_sched_priority
 *    9 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_filt_einfo
 *   10 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_filt_pswords
 *   11 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_supr_tdpkt
 *   12 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_credit_count
 *   13 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::fdepth
 *   14 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_burst_size
 *   15 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_tdtype
 *   16 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::extended_ch_type
 *
 * @nav_id: SoC device ID of Navigator Subsystem where tx channel is located
 *
 * @index: UDMAP transmit channel index.
 *
 * @tx_pause_on_err: UDMAP transmit channel pause on error configuration to
 * be programmed into the tx_pause_on_err field of the channel's TCHAN_TCFG
 * register.
 *
 * @tx_filt_einfo: UDMAP transmit channel extended packet information passing
 * configuration to be programmed into the tx_filt_einfo field of the
 * channel's TCHAN_TCFG register.
 *
 * @tx_filt_pswords: UDMAP transmit channel protocol specific word passing
 * configuration to be programmed into the tx_filt_pswords field of the
 * channel's TCHAN_TCFG register.
 *
 * @tx_atype: UDMAP transmit channel non Ring Accelerator access pointer
 * interpretation configuration to be programmed into the tx_atype field of
 * the channel's TCHAN_TCFG register.
 *
 * @tx_chan_type: UDMAP transmit channel functional channel type and work
 * passing mechanism configuration to be programmed into the tx_chan_type
 * field of the channel's TCHAN_TCFG register.
 *
 * @tx_supr_tdpkt: UDMAP transmit channel teardown packet generation suppression
 * configuration to be programmed into the tx_supr_tdpkt field of the channel's
 * TCHAN_TCFG register.
 *
 * @tx_fetch_size: UDMAP transmit channel number of 32-bit descriptor words to
 * fetch configuration to be programmed into the tx_fetch_size field of the
 * channel's TCHAN_TCFG register.  The user must make sure to set the maximum
 * word count that can pass through the channel for any allowed descriptor type.
 *
 * @tx_credit_count: UDMAP transmit channel transfer request credit count
 * configuration to be programmed into the count field of the TCHAN_TCREDIT
 * register.  Specifies how many credits for complete TRs are available.
 *
 * @txcq_qnum: UDMAP transmit channel completion queue configuration to be
 * programmed into the txcq_qnum field of the TCHAN_TCQ register. The specified
 * completion queue must be assigned to the host, or a subordinate of the host,
 * requesting configuration of the transmit channel.
 *
 * @tx_priority: UDMAP transmit channel transmit priority value to be programmed
 * into the priority field of the channel's TCHAN_TPRI_CTRL register.
 *
 * @tx_qos: UDMAP transmit channel transmit qos value to be programmed into the
 * qos field of the channel's TCHAN_TPRI_CTRL register.
 *
 * @tx_orderid: UDMAP transmit channel bus order id value to be programmed into
 * the orderid field of the channel's TCHAN_TPRI_CTRL register.
 *
 * @fdepth: UDMAP transmit channel FIFO depth configuration to be programmed
 * into the fdepth field of the TCHAN_TFIFO_DEPTH register. Sets the number of
 * Tx FIFO bytes which are allowed to be stored for the channel. Check the UDMAP
 * section of the TRM for restrictions regarding this parameter.
 *
 * @tx_sched_priority: UDMAP transmit channel tx scheduling priority
 * configuration to be programmed into the priority field of the channel's
 * TCHAN_TST_SCHED register.
 *
 * @tx_burst_size: UDMAP transmit channel burst size configuration to be
 * programmed into the tx_burst_size field of the TCHAN_TCFG register.
 *
 * @tx_tdtype: UDMAP transmit channel teardown type configuration to be
 * programmed into the tdtype field of the TCHAN_TCFG register:
 * 0 - Return immediately
 * 1 - Wait for completion message from remote peer
 *
 * @extended_ch_type: Valid for BCDMA.
 * 0 - the channel is split tx channel (tchan)
 * 1 - the channel is block copy channel (bchan)
 */
struct ti_sci_msg_rm_udmap_tx_ch_cfg_req {
	struct ti_sci_msg_hdr hdr;
	uint32_t valid_params;
	uint16_t nav_id;
	uint16_t index;
	uint8_t tx_pause_on_err;
	uint8_t tx_filt_einfo;
	uint8_t tx_filt_pswords;
	uint8_t tx_atype;
	uint8_t tx_chan_type;
	uint8_t tx_supr_tdpkt;
	uint16_t tx_fetch_size;
	uint8_t tx_credit_count;
	uint16_t txcq_qnum;
	uint8_t tx_priority;
	uint8_t tx_qos;
	uint8_t tx_orderid;
	uint16_t fdepth;
	uint8_t tx_sched_priority;
	uint8_t tx_burst_size;
	uint8_t tx_tdtype;
	uint8_t extended_ch_type;
} __packed;

/**
 *  Response to configuring a UDMAP transmit channel.
 *
 * @hdr: Standard TISCI header
 */
struct ti_sci_msg_rm_udmap_tx_ch_cfg_resp {
	struct ti_sci_msg_hdr hdr;
} __packed;

/**
 * Configures a Navigator Subsystem UDMAP receive channel
 *
 * Configures the non-real-time registers of a Navigator Subsystem UDMAP
 * receive channel.  The channel index must be assigned to the host defined
 * in the TISCI header via the RM board configuration resource assignment
 * range list.
 *
 * @hdr: Generic Header
 *
 * @valid_params: Bitfield defining validity of rx channel configuration
 * parameters.
 * The rx channel configuration fields are not valid, and will not be used for
 * ch configuration, if their corresponding valid bit is zero.
 * Valid bit usage:
 *    0 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_pause_on_err
 *    1 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_atype
 *    2 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_chan_type
 *    3 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_fetch_size
 *    4 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rxcq_qnum
 *    5 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_priority
 *    6 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_qos
 *    7 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_orderid
 *    8 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_sched_priority
 *    9 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::flowid_start
 *   10 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::flowid_cnt
 *   11 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_ignore_short
 *   12 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_ignore_long
 *
 * @nav_id: SoC device ID of Navigator Subsystem where rx channel is located
 *
 * @index: UDMAP receive channel index.
 *
 * @rx_fetch_size: UDMAP receive channel number of 32-bit descriptor words to
 * fetch configuration to be programmed into the rx_fetch_size field of the
 * channel's RCHAN_RCFG register.
 *
 * @rxcq_qnum: UDMAP receive channel completion queue configuration to be
 * programmed into the rxcq_qnum field of the RCHAN_RCQ register.
 * The specified completion queue must be assigned to the host, or a subordinate
 * of the host, requesting configuration of the receive channel.
 *
 * @rx_priority: UDMAP receive channel receive priority value to be programmed
 * into the priority field of the channel's RCHAN_RPRI_CTRL register.
 *
 * @rx_qos: UDMAP receive channel receive qos value to be programmed into the
 * qos field of the channel's RCHAN_RPRI_CTRL register.
 *
 * @rx_orderid: UDMAP receive channel bus order id value to be programmed into
 * the orderid field of the channel's RCHAN_RPRI_CTRL register.
 *
 * @rx_sched_priority: UDMAP receive channel rx scheduling priority
 * configuration to be programmed into the priority field of the channel's
 * RCHAN_RST_SCHED register.
 *
 * @flowid_start: UDMAP receive channel additional flows starting index
 * configuration to program into the flow_start field of the RCHAN_RFLOW_RNG
 * register. Specifies the starting index for flow IDs the receive channel is to
 * make use of beyond the default flow. flowid_start and @ref flowid_cnt must be
 * set as valid and configured together. The starting flow ID set by
 * @ref flowid_cnt must be a flow index within the Navigator Subsystem's subset
 * of flows beyond the default flows statically mapped to receive channels.
 * The additional flows must be assigned to the host, or a subordinate of the
 * host, requesting configuration of the receive channel.
 *
 * @flowid_cnt: UDMAP receive channel additional flows count configuration to
 * program into the flowid_cnt field of the RCHAN_RFLOW_RNG register.
 * This field specifies how many flow IDs are in the additional contiguous range
 * of legal flow IDs for the channel.  @ref flowid_start and flowid_cnt must be
 * set as valid and configured together. Disabling the valid_params field bit
 * for flowid_cnt indicates no flow IDs other than the default are to be
 * allocated and used by the receive channel. @ref flowid_start plus flowid_cnt
 * cannot be greater than the number of receive flows in the receive channel's
 * Navigator Subsystem.  The additional flows must be assigned to the host, or a
 * subordinate of the host, requesting configuration of the receive channel.
 *
 * @rx_pause_on_err: UDMAP receive channel pause on error configuration to be
 * programmed into the rx_pause_on_err field of the channel's RCHAN_RCFG
 * register.
 *
 * @rx_atype: UDMAP receive channel non Ring Accelerator access pointer
 * interpretation configuration to be programmed into the rx_atype field of the
 * channel's RCHAN_RCFG register.
 *
 * @rx_chan_type: UDMAP receive channel functional channel type and work passing
 * mechanism configuration to be programmed into the rx_chan_type field of the
 * channel's RCHAN_RCFG register.
 *
 * @rx_ignore_short: UDMAP receive channel short packet treatment configuration
 * to be programmed into the rx_ignore_short field of the RCHAN_RCFG register.
 *
 * @rx_ignore_long: UDMAP receive channel long packet treatment configuration to
 * be programmed into the rx_ignore_long field of the RCHAN_RCFG register.
 */
struct ti_sci_msg_rm_udmap_rx_ch_cfg_req {
	struct ti_sci_msg_hdr hdr;
	uint32_t valid_params;
	uint16_t nav_id;
	uint16_t index;
	uint16_t rx_fetch_size;
	uint16_t rxcq_qnum;
	uint8_t rx_priority;
	uint8_t rx_qos;
	uint8_t rx_orderid;
	uint8_t rx_sched_priority;
	uint16_t flowid_start;
	uint16_t flowid_cnt;
	uint8_t rx_pause_on_err;
	uint8_t rx_atype;
	uint8_t rx_chan_type;
	uint8_t rx_ignore_short;
	uint8_t rx_ignore_long;
} __packed;

/**
 * Response to configuring a UDMAP receive channel.
 *
 * @hdr: Standard TISCI header
 */
struct ti_sci_msg_rm_udmap_rx_ch_cfg_resp {
	struct ti_sci_msg_hdr hdr;
} __packed;

/**
 * Configures a Navigator Subsystem UDMAP receive flow
 *
 * Configures a Navigator Subsystem UDMAP receive flow's registers.
 * Configuration does not include the flow registers which handle size-based
 * free descriptor queue routing.
 *
 * The flow index must be assigned to the host defined in the TISCI header via
 * the RM board configuration resource assignment range list.
 *
 * @hdr: Standard TISCI header
 *
 * @valid_params
 * Bitfield defining validity of rx flow configuration parameters.  The
 * rx flow configuration fields are not valid, and will not be used for flow
 * configuration, if their corresponding valid bit is zero.  Valid bit usage:
 *     0 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_einfo_present
 *     1 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_psinfo_present
 *     2 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_error_handling
 *     3 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_desc_type
 *     4 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_sop_offset
 *     5 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_qnum
 *     6 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_src_tag_hi
 *     7 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_src_tag_lo
 *     8 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_tag_hi
 *     9 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_tag_lo
 *    10 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_src_tag_hi_sel
 *    11 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_src_tag_lo_sel
 *    12 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_tag_hi_sel
 *    13 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_tag_lo_sel
 *    14 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_fdq0_sz0_qnum
 *    15 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_fdq1_sz0_qnum
 *    16 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_fdq2_sz0_qnum
 *    17 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_fdq3_sz0_qnum
 *    18 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_ps_location
 *
 * @nav_id: SoC device ID of Navigator Subsystem from which the receive flow is
 * allocated
 *
 * @flow_index: UDMAP receive flow index for non-optional configuration.
 *
 * @rx_einfo_present:
 * UDMAP receive flow extended packet info present configuration to be
 * programmed into the rx_einfo_present field of the flow's RFLOW_RFA register.
 *
 * @rx_psinfo_present:
 * UDMAP receive flow PS words present configuration to be programmed into the
 * rx_psinfo_present field of the flow's RFLOW_RFA register.
 *
 * @rx_error_handling:
 * UDMAP receive flow error handling configuration to be programmed into the
 * rx_error_handling field of the flow's RFLOW_RFA register.
 *
 * @rx_desc_type:
 * UDMAP receive flow descriptor type configuration to be programmed into the
 * rx_desc_type field field of the flow's RFLOW_RFA register.
 *
 * @rx_sop_offset:
 * UDMAP receive flow start of packet offset configuration to be programmed
 * into the rx_sop_offset field of the RFLOW_RFA register.  See the UDMAP
 * section of the TRM for more information on this setting.  Valid values for
 * this field are 0-255 bytes.
 *
 * @rx_dest_qnum:
 * UDMAP receive flow destination queue configuration to be programmed into the
 * rx_dest_qnum field of the flow's RFLOW_RFA register.  The specified
 * destination queue must be valid within the Navigator Subsystem and must be
 * owned by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_src_tag_hi:
 * UDMAP receive flow source tag high byte constant configuration to be
 * programmed into the rx_src_tag_hi field of the flow's RFLOW_RFB register.
 * See the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_src_tag_lo:
 * UDMAP receive flow source tag low byte constant configuration to be
 * programmed into the rx_src_tag_lo field of the flow's RFLOW_RFB register.
 * See the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_dest_tag_hi:
 * UDMAP receive flow destination tag high byte constant configuration to be
 * programmed into the rx_dest_tag_hi field of the flow's RFLOW_RFB register.
 * See the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_dest_tag_lo:
 * UDMAP receive flow destination tag low byte constant configuration to be
 * programmed into the rx_dest_tag_lo field of the flow's RFLOW_RFB register.
 * See the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_src_tag_hi_sel:
 * UDMAP receive flow source tag high byte selector configuration to be
 * programmed into the rx_src_tag_hi_sel field of the RFLOW_RFC register.  See
 * the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_src_tag_lo_sel:
 * UDMAP receive flow source tag low byte selector configuration to be
 * programmed into the rx_src_tag_lo_sel field of the RFLOW_RFC register.  See
 * the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_dest_tag_hi_sel:
 * UDMAP receive flow destination tag high byte selector configuration to be
 * programmed into the rx_dest_tag_hi_sel field of the RFLOW_RFC register.  See
 * the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_dest_tag_lo_sel:
 * UDMAP receive flow destination tag low byte selector configuration to be
 * programmed into the rx_dest_tag_lo_sel field of the RFLOW_RFC register.  See
 * the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_fdq0_sz0_qnum:
 * UDMAP receive flow free descriptor queue 0 configuration to be programmed
 * into the rx_fdq0_sz0_qnum field of the flow's RFLOW_RFD register.  See the
 * UDMAP section of the TRM for more information on this setting. The specified
 * free queue must be valid within the Navigator Subsystem and must be owned
 * by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_fdq1_qnum:
 * UDMAP receive flow free descriptor queue 1 configuration to be programmed
 * into the rx_fdq1_qnum field of the flow's RFLOW_RFD register.  See the
 * UDMAP section of the TRM for more information on this setting.  The specified
 * free queue must be valid within the Navigator Subsystem and must be owned
 * by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_fdq2_qnum:
 * UDMAP receive flow free descriptor queue 2 configuration to be programmed
 * into the rx_fdq2_qnum field of the flow's RFLOW_RFE register.  See the
 * UDMAP section of the TRM for more information on this setting.  The specified
 * free queue must be valid within the Navigator Subsystem and must be owned
 * by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_fdq3_qnum:
 * UDMAP receive flow free descriptor queue 3 configuration to be programmed
 * into the rx_fdq3_qnum field of the flow's RFLOW_RFE register.  See the
 * UDMAP section of the TRM for more information on this setting.  The specified
 * free queue must be valid within the Navigator Subsystem and must be owned
 * by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_ps_location:
 * UDMAP receive flow PS words location configuration to be programmed into the
 * rx_ps_location field of the flow's RFLOW_RFA register.
 */
struct ti_sci_msg_rm_udmap_flow_cfg_req {
	struct ti_sci_msg_hdr hdr;
	uint32_t valid_params;
	uint16_t nav_id;
	uint16_t flow_index;
	uint8_t rx_einfo_present;
	uint8_t rx_psinfo_present;
	uint8_t rx_error_handling;
	uint8_t rx_desc_type;
	uint16_t rx_sop_offset;
	uint16_t rx_dest_qnum;
	uint8_t rx_src_tag_hi;
	uint8_t rx_src_tag_lo;
	uint8_t rx_dest_tag_hi;
	uint8_t rx_dest_tag_lo;
	uint8_t rx_src_tag_hi_sel;
	uint8_t rx_src_tag_lo_sel;
	uint8_t rx_dest_tag_hi_sel;
	uint8_t rx_dest_tag_lo_sel;
	uint16_t rx_fdq0_sz0_qnum;
	uint16_t rx_fdq1_qnum;
	uint16_t rx_fdq2_qnum;
	uint16_t rx_fdq3_qnum;
	uint8_t rx_ps_location;
} __packed;

/**
 *  Response to configuring a Navigator Subsystem UDMAP receive flow
 *
 * @hdr: Standard TISCI header
 */
struct ti_sci_msg_rm_udmap_flow_cfg_resp {
	struct ti_sci_msg_hdr hdr;
} __packed;

#define FWL_MAX_PRIVID_SLOTS 3U

/**
 * struct ti_sci_msg_fwl_set_firewall_region_req - Request for configuring the firewall permissions.
 *
 * @hdr:		Generic Header
 *
 * @fwl_id:		Firewall ID in question
 * @region:		Region or channel number to set config info
 *			This field is unused in case of a simple firewall  and must be initialized
 *			to zero.  In case of a region based firewall, this field indicates the
 *			region in question. (index starting from 0) In case of a channel based
 *			firewall, this field indicates the channel in question (index starting
 *			from 0)
 * @n_permission_regs:	Number of permission registers to set
 * @control:		Contents of the firewall CONTROL register to set
 * @permissions:	Contents of the firewall PERMISSION register to set
 * @start_address:	Contents of the firewall START_ADDRESS register to set
 * @end_address:	Contents of the firewall END_ADDRESS register to set
 */

struct ti_sci_msg_fwl_set_firewall_region_req {
	struct ti_sci_msg_hdr hdr;
	uint16_t fwl_id;
	uint16_t region;
	uint32_t n_permission_regs;
	uint32_t control;
	uint32_t permissions[FWL_MAX_PRIVID_SLOTS];
	uint64_t start_address;
	uint64_t end_address;
} __packed;

/**
 * struct ti_sci_msg_fwl_get_firewall_region_req - Request for retrieving the firewall permissions
 *
 * @hdr:		Generic Header
 *
 * @fwl_id:		Firewall ID in question
 * @region:		Region or channel number to get config info
 *			This field is unused in case of a simple firewall and must be initialized
 *			to zero.  In case of a region based firewall, this field indicates the
 *			region in question (index starting from 0). In case of a channel based
 *			firewall, this field indicates the channel in question (index starting
 *			from 0).
 * @n_permission_regs:	Number of permission registers to retrieve
 */
struct ti_sci_msg_fwl_get_firewall_region_req {
	struct ti_sci_msg_hdr hdr;
	uint16_t fwl_id;
	uint16_t region;
	uint32_t n_permission_regs;
} __packed;

/**
 * struct ti_sci_msg_fwl_get_firewall_region_resp - Response for retrieving the firewall permissions
 *
 * @hdr:		Generic Header
 *
 * @fwl_id:		Firewall ID in question
 * @region:		Region or channel number to set config info This field is
 *			unused in case of a simple firewall  and must be initialized to zero.  In
 *			case of a region based firewall, this field indicates the region in
 *			question. (index starting from 0) In case of a channel based firewall, this
 *			field indicates the channel in question (index starting from 0)
 * @n_permission_regs:	Number of permission registers retrieved
 * @control:		Contents of the firewall CONTROL register
 * @permissions:	Contents of the firewall PERMISSION registers
 * @start_address:	Contents of the firewall START_ADDRESS register This is not applicable for
 *channelized firewalls.
 * @end_address:	Contents of the firewall END_ADDRESS register This is not applicable for
 *channelized firewalls.
 */
struct ti_sci_msg_fwl_get_firewall_region_resp {
	struct ti_sci_msg_hdr hdr;
	uint16_t fwl_id;
	uint16_t region;
	uint32_t n_permission_regs;
	uint32_t control;
	uint32_t permissions[FWL_MAX_PRIVID_SLOTS];
	uint64_t start_address;
	uint64_t end_address;
} __packed;

/**
 * struct ti_sci_msg_fwl_change_owner_info_req - Request for a firewall owner change
 *
 * @hdr:		Generic Header
 *
 * @fwl_id:		Firewall ID in question
 * @region:		Region or channel number if applicable
 * @owner_index:	New owner index to transfer ownership to
 */
struct ti_sci_msg_fwl_change_owner_info_req {
	struct ti_sci_msg_hdr hdr;
	uint16_t fwl_id;
	uint16_t region;
	uint8_t owner_index;
} __packed;

/**
 * struct ti_sci_msg_fwl_change_owner_info_resp - Response for a firewall owner change
 *
 * @hdr:		Generic Header
 *
 * @fwl_id:		Firewall ID specified in request
 * @region:		Region or channel number specified in request
 * @owner_index:	Owner index specified in request
 * @owner_privid:	New owner priv-ID returned by DMSC.
 * @owner_permission_bits:	New owner permission bits returned by DMSC.
 */

struct ti_sci_msg_fwl_change_owner_info_resp {
	struct ti_sci_msg_hdr hdr;
	uint16_t fwl_id;
	uint16_t region;
	uint8_t owner_index;
	uint8_t owner_privid;
	uint16_t owner_permission_bits;
} __packed;

/**
 * struct ti_sci_version_info - version information structure
 * @abi_major:	Major ABI version. Change here implies risk of backward
 *		compatibility break.
 * @abi_minor:	Minor ABI version. Change here implies new feature addition,
 *		or compatible change in ABI.
 * @firmware_revision:	Firmware revision (not usually used).
 * @firmware_description: Firmware description (not usually used).
 */
struct ti_sci_version_info {
	uint8_t abi_major;
	uint8_t abi_minor;
	uint16_t firmware_revision;
	char firmware_description[32];
};


struct ti_sci_desc {
	uint8_t default_host_id;
	int max_rx_timeout_ms;
	int max_msgs;
	int max_msg_size;
};

#endif /* INCLUDE_ZEPHYR_DRIVERS_MISC_TISCI_H_ */