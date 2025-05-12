/*
 * Copyright (c) 2023, Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef INCLUDE_ZEPHYR_DRIVERS_TISCI_H_
#define INCLUDE_ZEPHYR_DRIVERS_TISCI_H_

#include <zephyr/device.h>

/**
 * ti_sci_cmd_clk_get_freq() - Get current frequency
 * @dev:	pointer to TI SCI dev
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @freq:	Currently frequency in Hz
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
int ti_sci_cmd_clk_get_freq(const struct device *dev, uint32_t dev_id, uint8_t clk_id, uint64_t *freq);

/**
 * ti_sci_cmd_clk_set_freq() - Set a frequency for clock
 * @dev:	pointer to TI SCI dev
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @min_freq:	The minimum allowable frequency in Hz. This is the minimum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @target_freq: The target clock frequency in Hz. A frequency will be
 *		processed as close to this target frequency as possible.
 * @max_freq:	The maximum allowable frequency in Hz. This is the maximum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
int ti_sci_cmd_clk_set_freq(const struct device *dev, uint32_t dev_id, uint8_t clk_id, uint64_t min_freq, uint64_t target_freq, uint64_t max_freq);

/**
 * ti_sci_cmd_clk_is_on() - Is the clock ON
 * @dev:	pointer to TI SCI dev
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @req_state: state indicating if the clock is managed by us and enabled
 * @curr_state: state indicating if the clock is ready for operation
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */		
int ti_sci_cmd_clk_is_on(const struct device *dev, uint32_t dev_id, uint8_t clk_id, bool *req_state, bool *curr_state);

/**
 * ti_sci_cmd_clk_is_off() - Is the clock OFF
 * @dev:	pointer to TI SCI dev
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @req_state: state indicating if the clock is managed by us and disabled
 * @curr_state: state indicating if the clock is NOT ready for operation
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
int ti_sci_cmd_clk_is_off(const struct device *dev, uint32_t dev_id, uint8_t clk_id, bool *req_state, bool *curr_state);

/**
 * ti_sci_cmd_clk_is_auto() - Is the clock being auto managed
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @req_state: state indicating if the clock is auto managed
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
 int ti_sci_cmd_clk_is_auto(const struct device *dev, uint32_t dev_id, uint8_t clk_id, bool *req_state);

/**
 * ti_sci_cmd_get_clock_state() - Get clock state helper
 * @dev:	pointer to TI SCI dev
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @programmed_state:	State requested for clock to move to
 * @current_state:	State that the clock is currently in
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
 int ti_sci_cmd_get_clock_state(const struct device *dev, uint32_t dev_id, uint8_t clk_id, uint8_t *programmed_state, uint8_t *current_state);

/**
 * ti_sci_cmd_get_revision() - command to get the revision of the SCI entity
 * @dev:	pointer to TI SCI dev
 *
 * Updates the SCI information in the internal data structure.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
int ti_sci_cmd_get_revision(const struct device *dev);
#endif