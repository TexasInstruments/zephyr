/*
 * Copyright (c) 2025, Texas Instruments
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include "zephyr/sys/printk.h"
 #include <stdint.h>
 #include <stdio.h>
 #define DT_DRV_COMPAT ti_k2g_sci
 
 #include <zephyr/drivers/mbox.h>
 #include <zephyr/device.h>
 #include <zephyr/drivers/firmware/tisci/tisci_protocol.h>
 #include <zephyr/kernel.h>
 #include <zephyr/sys/util.h>
 
 #define LOG_LEVEL CONFIG_MBOX_LOG_LEVEL
 #include <zephyr/logging/log.h>
 LOG_MODULE_REGISTER(ti, k2g - sci);
 
 /* Semaphore for signaling response readiness */
 static struct k_sem response_ready_sem;
 
 /**
  * struct tisci_config - TISCI device configuration
  * @mbox_tx: Mailbox transmit channel
  * @mbox_rx: Mailbox receive channel
  * @host_id: Host ID for the device
  */
 struct tisci_config {
	 struct mbox_dt_spec mbox_tx;
	 struct mbox_dt_spec mbox_rx;
	 uint32_t host_id;
 };
 
 /**
  * struct rx_msg - Received message details
  * @seq: Message sequence number
  * @size: Message size in bytes
  * @buf: Buffer for message data
  */
 struct rx_msg {
	 uint8_t seq;
	 uint8_t size;
	 char buf[256];
 };
 
 /* Received message container */
 static struct rx_msg rx_message;
 
 /**
  * struct ti_sci_xfer - TISCI transfer details
  * @tx_message: Transmit message
  * @rx_message: Received message
  * @rx_len: Expected receive message length
  */
 struct ti_sci_xfer {
	 struct mbox_msg tx_message;
	 struct rx_msg rx_message;
	 uint8_t rx_len;
 };
 
 /**
  * struct tisci_data - Runtime data for TISCI device
  * @xfer: Current transfer details
  * @desc: Device descriptor
  * @version: Firmware version info
  * @host_id: Host ID for the device
  * @seq: Current transfer sequence number
  */
 struct tisci_data {
	 struct ti_sci_xfer xfer;
	 struct ti_sci_desc desc;
	 struct ti_sci_version_info version;
	 uint32_t host_id;
	 uint8_t seq;
 };
 
 /* Core/Setup Functions */
 static struct ti_sci_xfer *ti_sci_setup_one_xfer(const struct device *dev, uint16_t msg_type,
						  uint32_t msg_flags, void *buf,
						  size_t tx_message_size, size_t rx_message_size)
 {
	 struct tisci_data *data = dev->data;
	 struct ti_sci_xfer *xfer = &data->xfer;
	 struct ti_sci_msg_hdr *hdr;
 
	 if (rx_message_size > data->desc.max_msg_size ||
		 tx_message_size > data->desc.max_msg_size ||
		 (rx_message_size > 0 && rx_message_size < sizeof(*hdr)) ||
		 tx_message_size < sizeof(*hdr)) {
		 return NULL;
	 }
 
	 data->seq++;
	 xfer->tx_message.data = buf;
	 xfer->tx_message.size = tx_message_size;
	 xfer->rx_len = (uint8_t)rx_message_size;
 
	 hdr = (struct ti_sci_msg_hdr *)buf;
	 hdr->seq = data->seq;
	 hdr->type = msg_type;
	 hdr->host = data->host_id;
	 hdr->flags = msg_flags;
 
	 return xfer;
 }
 
 static void callback(const struct device *dev, mbox_channel_id_t channel_id, void *user_data,
			  struct mbox_msg *data)
 {
	 const struct ti_sci_msg_hdr *hdr = data->data;
 
	 if (data->size > 64) {
		 LOG_ERR("Too large incoming message");
	 }
 
	 memcpy(rx_message.buf, data->data, 256);
	 rx_message.size = data->size;
	 rx_message.seq = hdr->seq;
 
	 k_sem_give(&response_ready_sem);
 }
 
 static bool ti_sci_is_response_ack(void *r)
 {
	 struct ti_sci_msg_hdr *hdr = (struct ti_sci_msg_hdr *)r;
	 return hdr->flags & TI_SCI_FLAG_RESP_GENERIC_ACK ? true : false;
 }
 
 static int ti_sci_get_response(const struct device *dev, struct ti_sci_xfer *xfer)
 {
	 struct tisci_data *dev_data = dev->data;
	 struct ti_sci_msg_hdr *hdr;
	 int ret = 0;
 
	 if (k_sem_take(&response_ready_sem, K_MSEC(100)) != 0) {
		 LOG_ERR("Timeout waiting for response");
		 return -ETIMEDOUT;
	 }
 
	 hdr = (struct ti_sci_msg_hdr *)rx_message.buf;
 
	 /* Sanity check for message response */
	 if (hdr->seq != dev_data->seq) {
		 LOG_ERR("HDR seq != data seq [%d != %d]\n", hdr->seq, dev_data->seq);
		 return -EINVAL;
	 }
 
	 if (rx_message.size > dev_data->desc.max_msg_size) {
		 LOG_ERR("rx_message.size [ %d ] > max_msg_size\n", xfer->rx_message.size);
		 return -EINVAL;
	 }
 
	 if (rx_message.size < xfer->rx_len) {
		 LOG_ERR("rx_message.size [ %d ] < xfer->rx_len\n", xfer->rx_message.size);
		 return -EINVAL;
	 }
 
	 return ret;
 }
 
 static int ti_sci_do_xfer(const struct device *dev, struct ti_sci_xfer *xfer)
 {
	 const struct tisci_config *config = dev->config;
	 struct mbox_msg *msg = &xfer->tx_message;
	 int ret;
 
	 ret = mbox_send_dt(&config->mbox_tx, msg);
	 if (ret < 0) {
		 LOG_ERR("Could not send (%d)\n", ret);
		 return 0;
	 }
 
	 /* Get response if requested */
	 if (xfer->rx_len) {
		 ret = ti_sci_get_response(dev, xfer);
		 if (!ti_sci_is_response_ack(rx_message.buf)) {
			 LOG_ERR("TISCI Response in NACK\n");
			 ret = -ENODEV;
		 }
	 }
 
	 return ret;
 }
 
 static int tisci_init(const struct device *dev)
 {
	 const struct tisci_config *config = dev->config;
	 struct tisci_data *data = dev->data;
	 int ret;
 
	 k_sem_init(&response_ready_sem, 0, 1);
 
	 data->host_id = config->host_id;
	 data->seq = 0x0;
	 data->desc.default_host_id = config->host_id;
	 data->desc.max_rx_timeout_ms = 1000;
	 data->desc.max_msgs = 5;
	 data->desc.max_msg_size = 60;
 
	 ret = mbox_register_callback_dt(&config->mbox_rx, callback, NULL);
	 if (ret < 0) {
		 printk("Could not register callback (%d)\n", ret);
		 return 0;
	 }
 
	 ret = mbox_set_enabled_dt(&config->mbox_rx, true);
	 if (ret < 0) {
		 printk("Could not enable RX channel (%d)\n", ret);
		 return 0;
	 }
	 return 0;
 }
 
 /* Clock Management Functions */
 int ti_sci_cmd_get_clock_state(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
					uint8_t *programmed_state, uint8_t *current_state)
 {
	 struct ti_sci_msg_resp_get_clock_state *resp;
	 struct ti_sci_msg_req_get_clock_state req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_GET_CLOCK_STATE,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
 
	 req.dev_id = dev_id;
	 req.clk_id = clk_id;
	 ret = ti_sci_do_xfer(dev, xfer);
	 resp = (struct ti_sci_msg_resp_get_clock_state *)rx_message.buf;
	 if (programmed_state) {
		 *programmed_state = resp->programmed_state;
	 }
	 if (current_state) {
		 *current_state = resp->current_state;
	 }
	 return ret;
 }
 
 int ti_sci_cmd_clk_is_auto(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
				bool *req_state)
 {
	 uint8_t state = 0;
	 int ret;
 
	 if (!req_state) {
		 return -EINVAL;
	 }
 
	 ret = ti_sci_cmd_get_clock_state(dev, dev_id, clk_id, &state, NULL);
	 if (ret) {
		 return ret;
	 }
 
	 *req_state = (state == MSG_CLOCK_SW_STATE_AUTO);
	 return 0;
 }
 
 int ti_sci_cmd_clk_is_on(const struct device *dev, uint32_t dev_id, uint8_t clk_id, bool *req_state,
			  bool *curr_state)
 {
	 uint8_t c_state = 0, r_state = 0;
	 int ret;
 
	 if (!req_state && !curr_state) {
		 return -EINVAL;
	 }
 
	 ret = ti_sci_cmd_get_clock_state(dev, dev_id, clk_id, &r_state, &c_state);
	 if (ret) {
		 return ret;
	 }
 
	 if (req_state) {
		 *req_state = (r_state == MSG_CLOCK_SW_STATE_REQ);
	 }
	 if (curr_state) {
		 *curr_state = (c_state == MSG_CLOCK_HW_STATE_READY);
	 }
	 return 0;
 }
 
 int ti_sci_cmd_clk_is_off(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
			   bool *req_state, bool *curr_state)
 {
	 uint8_t c_state = 0, r_state = 0;
	 int ret;
 
	 if (!req_state && !curr_state) {
		 return -EINVAL;
	 }
 
	 ret = ti_sci_cmd_get_clock_state(dev, dev_id, clk_id, &r_state, &c_state);
	 if (ret) {
		 return ret;
	 }
 
	 if (req_state) {
		 *req_state = (r_state == MSG_CLOCK_SW_STATE_UNREQ);
	 }
	 if (curr_state) {
		 *curr_state = (c_state == MSG_CLOCK_HW_STATE_NOT_READY);
	 }
	 return 0;
 }
 
 int ti_sci_cmd_clk_get_match_freq(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
				   uint64_t min_freq, uint64_t target_freq, uint64_t max_freq,
				   uint64_t *match_freq)
 {
	 struct ti_sci_msg_resp_query_clock_freq *resp;
	 struct ti_sci_msg_req_query_clock_freq req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_QUERY_CLOCK_FREQ,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
 
	 req.dev_id = dev_id;
	 req.clk_id = clk_id;
	 req.min_freq_hz = min_freq;
	 req.target_freq_hz = target_freq;
	 req.max_freq_hz = max_freq;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 return ret;
	 }
 
	 resp = (struct ti_sci_msg_resp_query_clock_freq *)rx_message.buf;
	 *match_freq = resp->freq_hz;
 
	 return ret;
 }
 
 int ti_sci_cmd_clk_set_freq(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
				 uint64_t min_freq, uint64_t target_freq, uint64_t max_freq)
 {
	 struct ti_sci_msg_req_set_clock_freq req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_SET_CLOCK_FREQ,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 req.dev_id = dev_id;
	 req.clk_id = clk_id;
	 req.min_freq_hz = min_freq;
	 req.target_freq_hz = target_freq;
	 req.max_freq_hz = max_freq;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_clk_get_freq(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
				 uint64_t *freq)
 {
	 struct ti_sci_msg_resp_get_clock_freq *resp;
	 struct ti_sci_msg_req_get_clock_freq req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_GET_CLOCK_FREQ,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 req.dev_id = dev_id;
	 req.clk_id = clk_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 return ret;
	 }
 
	 resp = (struct ti_sci_msg_resp_get_clock_freq *)rx_message.buf;
	 *freq = resp->freq_hz;
 
	 return ret;
 }
 
 int ti_sci_set_clock_state(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
				uint32_t flags, uint8_t state)
 {
	 struct ti_sci_msg_req_set_clock_state req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_SET_CLOCK_STATE,
					  flags | TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.dev_id = dev_id;
	 req.clk_id = clk_id;
	 req.request_state = state;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to set clock state (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_clk_set_parent(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
				   uint8_t parent_id)
 {
	 struct ti_sci_msg_req_set_clock_parent req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_SET_CLOCK_PARENT,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
 
	 req.dev_id = dev_id;
	 req.clk_id = clk_id;
	 req.parent_id = parent_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to set clock parent (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_clk_get_parent(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
				   uint8_t *parent_id)
 {
	 struct ti_sci_msg_resp_get_clock_parent *resp;
	 struct ti_sci_msg_req_get_clock_parent req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_GET_CLOCK_PARENT,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.dev_id = dev_id;
	 req.clk_id = clk_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to get clock parent (ret=%d)", ret);
		 return ret;
	 }
 
	 resp = (struct ti_sci_msg_resp_get_clock_parent *)rx_message.buf;
	 *parent_id = resp->parent_id;
 
	 return ret;
 }
 
 int ti_sci_cmd_clk_get_num_parents(const struct device *dev, uint32_t dev_id, uint8_t clk_id,
					uint8_t *num_parents)
 {
	 struct ti_sci_msg_resp_get_clock_num_parents *resp;
	 struct ti_sci_msg_req_get_clock_num_parents req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_GET_NUM_CLOCK_PARENTS,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
 
	 req.dev_id = dev_id;
	 req.clk_id = clk_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to get number of clock parents (ret=%d)", ret);
		 return ret;
	 }
 
	 resp = (struct ti_sci_msg_resp_get_clock_num_parents *)rx_message.buf;
	 *num_parents = resp->num_parents;
 
	 return ret;
 }
 
 int ti_sci_cmd_get_clock(const struct device *dev, uint32_t dev_id, uint8_t clk_id, bool needs_ssc,
			  bool can_change_freq, bool enable_input_term)
 {
	 uint32_t flags = 0;
 
	 flags |= needs_ssc ? MSG_FLAG_CLOCK_ALLOW_SSC : 0;
	 flags |= can_change_freq ? MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE : 0;
	 flags |= enable_input_term ? MSG_FLAG_CLOCK_INPUT_TERM : 0;
 
	 return ti_sci_set_clock_state(dev, dev_id, clk_id, flags, MSG_CLOCK_SW_STATE_REQ);
 }
 
 int ti_sci_cmd_idle_clock(const struct device *dev, uint32_t dev_id, uint8_t clk_id)
 {
	 return ti_sci_set_clock_state(dev, dev_id, clk_id, 0, MSG_CLOCK_SW_STATE_UNREQ);
 }
 
 int ti_sci_cmd_put_clock(const struct device *dev, uint32_t dev_id, uint8_t clk_id)
 {
	 return ti_sci_set_clock_state(dev, dev_id, clk_id, 0, MSG_CLOCK_SW_STATE_UNREQ);
 }
 
 /* Device Management Functions */
 int ti_sci_set_device_state(const struct device *dev, uint32_t dev_id, uint32_t flags,
				 uint8_t state)
 {
	 struct ti_sci_msg_req_set_device_state req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_SET_DEVICE_STATE,
					  flags | TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
 
	 req.id = dev_id;
	 req.state = state;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_set_device_state_no_wait(const struct device *dev, uint32_t dev_id, uint32_t flags,
					 uint8_t state)
 {
	 struct ti_sci_msg_req_set_device_state req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_SET_DEVICE_STATE,
					  flags | TI_SCI_FLAG_REQ_GENERIC_NORESPONSE, (uint32_t *)&req,
					  sizeof(req), 0);
 
	 req.id = dev_id;
	 req.state = state;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to set device state without wait (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 /* Remaining Device Management Functions */
 int ti_sci_get_device_state(const struct device *dev, uint32_t dev_id, uint32_t *clcnt,
				 uint32_t *resets, uint8_t *p_state, uint8_t *c_state)
 {
	 struct ti_sci_msg_resp_get_device_state *resp;
	 struct ti_sci_msg_req_get_device_state req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 if (!clcnt && !resets && !p_state && !c_state) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_GET_DEVICE_STATE,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
 
	 req.id = dev_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to get device state (ret=%d)", ret);
		 return ret;
	 }
 
	 resp = (struct ti_sci_msg_resp_get_device_state *)rx_message.buf;
 
	 if (clcnt) {
		 *clcnt = resp->context_loss_count;
	 }
	 if (resets) {
		 *resets = resp->resets;
	 }
	 if (p_state) {
		 *p_state = resp->programmed_state;
	 }
	 if (c_state) {
		 *c_state = resp->current_state;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_get_device(const struct device *dev, uint32_t dev_id)
 {
	 return ti_sci_set_device_state(dev, dev_id, 0, MSG_DEVICE_SW_STATE_ON);
 }
 
 int ti_sci_cmd_get_device_exclusive(const struct device *dev, uint32_t dev_id)
 {
	 return ti_sci_set_device_state(dev, dev_id, MSG_FLAG_DEVICE_EXCLUSIVE,
						MSG_DEVICE_SW_STATE_ON);
 }
 
 int ti_sci_cmd_idle_device(const struct device *dev, uint32_t dev_id)
 {
	 return ti_sci_set_device_state(dev, dev_id, 0, MSG_DEVICE_SW_STATE_RETENTION);
 }
 
 int ti_sci_cmd_idle_device_exclusive(const struct device *dev, uint32_t dev_id)
 {
	 return ti_sci_set_device_state(dev, dev_id, MSG_FLAG_DEVICE_EXCLUSIVE,
						MSG_DEVICE_SW_STATE_RETENTION);
 }
 
 int ti_sci_cmd_put_device(const struct device *dev, uint32_t dev_id)
 {
	 return ti_sci_set_device_state(dev, dev_id, 0, MSG_DEVICE_SW_STATE_AUTO_OFF);
 }
 
 int ti_sci_cmd_dev_is_valid(const struct device *dev, uint32_t dev_id)
 {
	 uint8_t unused;
	 return ti_sci_get_device_state(dev, dev_id, NULL, NULL, NULL, &unused);
 }
 
 int ti_sci_cmd_dev_get_clcnt(const struct device *dev, uint32_t dev_id, uint32_t *count)
 {
	 return ti_sci_get_device_state(dev, dev_id, count, NULL, NULL, NULL);
 }
 
 int ti_sci_cmd_dev_is_idle(const struct device *dev, uint32_t dev_id, bool *r_state)
 {
	 int ret;
	 uint8_t state;
 
	 if (!r_state) {
		 return -EINVAL;
	 }
 
	 ret = ti_sci_get_device_state(dev, dev_id, NULL, NULL, &state, NULL);
	 if (ret) {
		 return ret;
	 }
 
	 *r_state = (state == MSG_DEVICE_SW_STATE_RETENTION);
 
	 return 0;
 }
 
 int ti_sci_cmd_dev_is_stop(const struct device *dev, uint32_t dev_id, bool *r_state,
				bool *curr_state)
 {
	 int ret;
	 uint8_t p_state, c_state;
 
	 if (!r_state && !curr_state) {
		 return -EINVAL;
	 }
 
	 ret = ti_sci_get_device_state(dev, dev_id, NULL, NULL, &p_state, &c_state);
	 if (ret) {
		 return ret;
	 }
 
	 if (r_state) {
		 *r_state = (p_state == MSG_DEVICE_SW_STATE_AUTO_OFF);
	 }
	 if (curr_state) {
		 *curr_state = (c_state == MSG_DEVICE_HW_STATE_OFF);
	 }
 
	 return 0;
 }
 
 int ti_sci_cmd_dev_is_on(const struct device *dev, uint32_t dev_id, bool *r_state, bool *curr_state)
 {
	 int ret;
	 uint8_t p_state, c_state;
 
	 if (!r_state && !curr_state) {
		 return -EINVAL;
	 }
 
	 ret = ti_sci_get_device_state(dev, dev_id, NULL, NULL, &p_state, &c_state);
	 if (ret) {
		 return ret;
	 }
 
	 if (r_state) {
		 *r_state = (p_state == MSG_DEVICE_SW_STATE_ON);
	 }
	 if (curr_state) {
		 *curr_state = (c_state == MSG_DEVICE_HW_STATE_ON);
	 }
 
	 return 0;
 }
 
 int ti_sci_cmd_dev_is_trans(const struct device *dev, uint32_t dev_id, bool *curr_state)
 {
	 int ret;
	 uint8_t state;
 
	 if (!curr_state) {
		 return -EINVAL;
	 }
 
	 ret = ti_sci_get_device_state(dev, dev_id, NULL, NULL, NULL, &state);
	 if (ret) {
		 return ret;
	 }
 
	 *curr_state = (state == MSG_DEVICE_HW_STATE_TRANS);
 
	 return 0;
 }
 
 int ti_sci_cmd_set_device_resets(const struct device *dev, uint32_t dev_id, uint32_t reset_state)
 {
	 struct ti_sci_msg_req_set_device_resets req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_SET_DEVICE_RESETS,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.id = dev_id;
	 req.resets = reset_state;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to set device resets (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_get_device_resets(const struct device *dev, uint32_t dev_id, uint32_t *reset_state)
 {
	 return ti_sci_get_device_state(dev, dev_id, NULL, reset_state, NULL, NULL);
 }
 
 /* Processor Management Functions */
 int ti_sci_cmd_proc_request(const struct device *dev, uint8_t proc_id)
 {
	 struct ti_sci_msg_req_proc_request req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TISCI_MSG_PROC_REQUEST, TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
					  (uint32_t *)&req, sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.processor_id = proc_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to request processor control (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_proc_release(const struct device *dev, uint8_t proc_id)
 {
	 struct ti_sci_msg_req_proc_release req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TISCI_MSG_PROC_RELEASE, TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
					  (uint32_t *)&req, sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.processor_id = proc_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to release processor control (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_proc_handover(const struct device *dev, uint8_t proc_id, uint8_t host_id)
 {
	 struct ti_sci_msg_req_proc_handover req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TISCI_MSG_PROC_HANDOVER, TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
					  (uint32_t *)&req, sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.processor_id = proc_id;
	 req.host_id = host_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to handover processor control (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_set_proc_boot_cfg(const struct device *dev, uint8_t proc_id, uint64_t bootvector,
				  uint32_t config_flags_set, uint32_t config_flags_clear)
 {
	 struct ti_sci_msg_req_set_proc_boot_config req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TISCI_MSG_SET_PROC_BOOT_CONFIG,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.processor_id = proc_id;
	 req.bootvector_low = bootvector & TISCI_ADDR_LOW_MASK;
	 req.bootvector_high = (bootvector & TISCI_ADDR_HIGH_MASK) >> TISCI_ADDR_HIGH_SHIFT;
	 req.config_flags_set = config_flags_set;
	 req.config_flags_clear = config_flags_clear;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to set processor boot configuration (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_set_proc_boot_ctrl(const struct device *dev, uint8_t proc_id,
				   uint32_t control_flags_set, uint32_t control_flags_clear)
 {
	 struct ti_sci_msg_req_set_proc_boot_ctrl req;
	 struct ti_sci_msg_hdr *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TISCI_MSG_SET_PROC_BOOT_CTRL,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.processor_id = proc_id;
	 req.control_flags_set = control_flags_set;
	 req.control_flags_clear = control_flags_clear;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to set processor boot control (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_proc_auth_boot_image(const struct device *dev, uint64_t *image_addr,
					 uint32_t *image_size)
 {
	 struct ti_sci_msg_req_proc_auth_boot_image req;
	 struct ti_sci_msg_resp_proc_auth_boot_image *resp;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TISCI_MSG_PROC_AUTH_BOOT_IMAGE,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.cert_addr_low = *image_addr & TISCI_ADDR_LOW_MASK;
	 req.cert_addr_high = (*image_addr & TISCI_ADDR_HIGH_MASK) >> TISCI_ADDR_HIGH_SHIFT;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to authenticate boot image (ret=%d)", ret);
		 return ret;
	 }
 
	 resp = (struct ti_sci_msg_resp_proc_auth_boot_image *)rx_message.buf;
 
	 *image_addr =
		 (resp->image_addr_low & TISCI_ADDR_LOW_MASK) |
		 (((uint64_t)resp->image_addr_high << TISCI_ADDR_HIGH_SHIFT) & TISCI_ADDR_HIGH_MASK);
	 *image_size = resp->image_size;
 
	 return ret;
 }
 
 int ti_sci_cmd_get_proc_boot_status(const struct device *dev, uint8_t proc_id, uint64_t *bv,
					 uint32_t *cfg_flags, uint32_t *ctrl_flags, uint32_t *sts_flags)
 {
	 struct ti_sci_msg_resp_get_proc_boot_status *resp;
	 struct ti_sci_msg_req_get_proc_boot_status req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TISCI_MSG_GET_PROC_BOOT_STATUS,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.processor_id = proc_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to get processor boot status (ret=%d)", ret);
		 return ret;
	 }
 
	 resp = (struct ti_sci_msg_resp_get_proc_boot_status *)rx_message.buf;
 
	 *bv = (resp->bootvector_low & TISCI_ADDR_LOW_MASK) |
		   (((uint64_t)resp->bootvector_high << TISCI_ADDR_HIGH_SHIFT) & TISCI_ADDR_HIGH_MASK);
	 *cfg_flags = resp->config_flags;
	 *ctrl_flags = resp->control_flags;
	 *sts_flags = resp->status_flags;
 
	 return ret;
 }
 
 int ti_sci_proc_wait_boot_status_no_wait(const struct device *dev, uint8_t proc_id,
					  uint8_t num_wait_iterations, uint8_t num_match_iterations,
					  uint8_t delay_per_iteration_us,
					  uint8_t delay_before_iterations_us,
					  uint32_t status_flags_1_set_all_wait,
					  uint32_t status_flags_1_set_any_wait,
					  uint32_t status_flags_1_clr_all_wait,
					  uint32_t status_flags_1_clr_any_wait)
 {
	 struct ti_sci_msg_req_wait_proc_boot_status req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TISCI_MSG_WAIT_PROC_BOOT_STATUS,
					  TI_SCI_FLAG_REQ_GENERIC_NORESPONSE, (uint32_t *)&req,
					  sizeof(req), 0);
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.processor_id = proc_id;
	 req.num_wait_iterations = num_wait_iterations;
	 req.num_match_iterations = num_match_iterations;
	 req.delay_per_iteration_us = delay_per_iteration_us;
	 req.delay_before_iterations_us = delay_before_iterations_us;
	 req.status_flags_1_set_all_wait = status_flags_1_set_all_wait;
	 req.status_flags_1_set_any_wait = status_flags_1_set_any_wait;
	 req.status_flags_1_clr_all_wait = status_flags_1_clr_all_wait;
	 req.status_flags_1_clr_any_wait = status_flags_1_clr_any_wait;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to wait for processor boot status (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 int ti_sci_cmd_proc_shutdown_no_wait(const struct device *dev, uint8_t proc_id)
 {
	 int ret;
 
	 ret = ti_sci_proc_wait_boot_status_no_wait(
		 dev, proc_id, UINT8_MAX, 100, UINT8_MAX, UINT8_MAX, 0,
		 PROC_BOOT_STATUS_FLAG_R5_WFE | PROC_BOOT_STATUS_FLAG_R5_WFI, 0, 0);
	 if (ret) {
		 LOG_ERR("Failed to wait for processor boot status (ret=%d)", ret);
		 return ret;
	 }
 
	 ret = ti_sci_set_device_state_no_wait(dev, proc_id, 0, MSG_DEVICE_SW_STATE_AUTO_OFF);
	 if (ret) {
		 LOG_ERR("Failed to shutdown processor (ret=%d)", ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 /* Resource Management Functions */
 int ti_sci_get_resource_range(const struct device *dev, uint32_t dev_id, uint8_t subtype,
				   uint8_t s_host, uint16_t *range_start, uint16_t *range_num)
 {
	 struct ti_sci_msg_resp_get_resource_range *resp;
	 struct ti_sci_msg_req_get_resource_range req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_GET_RESOURCE_RANGE,
					  TI_SCI_FLAG_REQ_ACK_ON_PROCESSED, (uint32_t *)&req,
					  sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.secondary_host = s_host;
	 req.type = dev_id & MSG_RM_RESOURCE_TYPE_MASK;
	 req.subtype = subtype & MSG_RM_RESOURCE_SUBTYPE_MASK;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 return ret;
	 }
 
	 resp = (struct ti_sci_msg_resp_get_resource_range *)rx_message.buf;
	 if (!resp->range_start && !resp->range_num) {
		 return -ENODEV;
	 }
 
	 *range_start = resp->range_start;
	 *range_num = resp->range_num;
 
	 return ret;
 }
 
 int ti_sci_cmd_get_resource_range(const struct device *dev, uint32_t dev_id, uint8_t subtype,
				   uint16_t *range_start, uint16_t *range_num)
 {
	 return ti_sci_get_resource_range(dev, dev_id, subtype, TI_SCI_IRQ_SECONDARY_HOST_INVALID,
					  range_start, range_num);
 }
 
 int ti_sci_cmd_get_resource_range_from_shost(const struct device *dev, uint32_t dev_id,
						  uint8_t subtype, uint8_t s_host, uint16_t *range_start,
						  uint16_t *range_num)
 {
	 return ti_sci_get_resource_range(dev, dev_id, subtype, s_host, range_start, range_num);
 }
 
 /* Ring Configuration Function */
 int ti_sci_cmd_ring_config(const struct device *dev, uint32_t valid_params, uint16_t nav_id,
				uint16_t index, uint32_t addr_lo, uint32_t addr_hi, uint32_t count,
				uint8_t mode, uint8_t size, uint8_t order_id)
 {
	 struct ti_sci_msg_rm_ring_cfg_resp *resp;
	 struct ti_sci_msg_rm_ring_cfg_req req;
	 struct ti_sci_xfer *xfer;
	 int ret = 0;
 
	 if (!dev) {
		 return -EINVAL;
	 }
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_RM_RING_CFG, TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
					  (uint32_t *)&req, sizeof(req), sizeof(*resp));
	 if (!xfer) {
		 LOG_ERR("Failed to setup transfer");
		 return -EINVAL;
	 }
 
	 req.valid_params = valid_params;
	 req.nav_id = nav_id;
	 req.index = index;
	 req.addr_lo = addr_lo;
	 req.addr_hi = addr_hi;
	 req.count = count;
	 req.mode = mode;
	 req.size = size;
	 req.order_id = order_id;
 
	 ret = ti_sci_do_xfer(dev, xfer);
	 if (ret) {
		 LOG_ERR("Failed to configure ring %u (ret=%d)", index, ret);
		 return ret;
	 }
 
	 return ret;
 }
 
 /* Version/Revision Function */
 int ti_sci_cmd_get_revision(const struct device *dev)
 {
	 struct tisci_data *data = dev->data;
	 struct ti_sci_msg_hdr hdr;
	 struct ti_sci_version_info *ver;
	 struct ti_sci_msg_resp_version *rev_info;
	 struct ti_sci_xfer *xfer;
 
	 xfer = ti_sci_setup_one_xfer(dev, TI_SCI_MSG_VERSION, TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
					  &hdr, sizeof(struct ti_sci_msg_hdr), sizeof(*rev_info));
 
	 ti_sci_do_xfer(dev, xfer);
 
	 rev_info = (struct ti_sci_msg_resp_version *)rx_message.buf;
	 ver = &data->version;
	 ver->abi_major = rev_info->abi_major;
	 ver->abi_minor = rev_info->abi_minor;
	 ver->firmware_revision = rev_info->firmware_revision;
	 printf("%s", rev_info->firmware_description);
	 strncpy(ver->firmware_description, rev_info->firmware_description,
		 sizeof(ver->firmware_description));
	 return 0;
 }
 
 /* Device Tree Instantiation */
 #define TISCI_DEFINE(_n)                                                                           \
	 static struct tisci_data tisci_data_##_n;                                                  \
	 static const struct tisci_config tisci_config_##_n = {                                     \
		 .mbox_tx = MBOX_DT_SPEC_INST_GET(_n, tx),                                          \
		 .mbox_rx = MBOX_DT_SPEC_INST_GET(_n, rx),                                          \
		 .host_id = DT_INST_PROP(_n, ti_host_id),                                           \
	 };                                                                                         \
	 DEVICE_DT_INST_DEFINE(_n, tisci_init, NULL, &tisci_data_##_n, &tisci_config_##_n,          \
				   PRE_KERNEL_1, CONFIG_TISCI_INIT_PRIORITY, NULL);
 
 DT_INST_FOREACH_STATUS_OKAY(TISCI_DEFINE)
 